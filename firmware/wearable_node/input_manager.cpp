// ============================================================
//  MYOSA Node Firmware — Input Manager Implementation
//  File    : input_manager.cpp
//
//  HW-763 capacitive touch module output polarity:
//    NOT touched  →  pin HIGH  (idle, held up by INPUT_PULLUP)
//    Touched      →  pin LOW   (active-low output from HW-763)
//
//  Edge mapping:
//    FALLING (HIGH→LOW) = finger DOWN  (touch start)
//    RISING  (LOW→HIGH) = finger UP    (touch end / release)
//
//  KEY FIX: NO debounce in the ISR.
//    HW-763 is a clean capacitive sensor, not a mechanical switch.
//    Adding ISR debounce was blocking the RISING (release) edge
//    for quick taps, leaving s_held=true permanently which caused:
//      - Spurious 2.5s LONG_PRESS timeouts turning display OFF
//      - All subsequent touches being silently discarded
//    Debounce is now done in software inside update() only.
// ============================================================
#include "input_manager.h"
#include "display_manager.h"

namespace InputManager {

// ---- ISR-shared state (volatile) ----
static volatile bool     s_isrDown    = false;   // set on FALLING edge
static volatile bool     s_isrUp      = false;   // set on RISING  edge
static volatile uint32_t s_isrDownAt  = 0;       // millis() of last FALLING

// ---- Main-loop state ----
static bool     s_held           = false;
static uint32_t s_heldSince      = 0;
static bool     s_longPressFired = false;

// ---- Multi-tap state machine ----
static uint8_t        s_tapCount     = 0;
static uint32_t       s_lastTapUpMs  = 0;
static const uint32_t TAP_TIMEOUT_MS = 220UL; // crisp 220ms gap for single tap delivery

// ---- Software debounce: ignore edges within this window ----
static const uint32_t SW_DEBOUNCE_MS = 30UL;
static uint32_t s_lastFallMs = 0;   // millis() of last accepted FALLING
static uint32_t s_lastRiseMs = 0;   // millis() of last accepted RISING

// ---- Event queue (depth 6) ----
static const uint8_t EV_QUEUE_DEPTH = 6;
static TouchEvent    s_queue[EV_QUEUE_DEPTH];
static uint8_t       s_qHead  = 0;
static uint8_t       s_qTail  = 0;
static uint8_t       s_qCount = 0;

static void _enqueue(TouchEvent ev) {
    if (s_qCount >= EV_QUEUE_DEPTH) return;
    s_queue[s_qTail] = ev;
    s_qTail = (s_qTail + 1) % EV_QUEUE_DEPTH;
    s_qCount++;
}

// ---- ISR — NO debounce, NO millis() call, just set flags ----
static void IRAM_ATTR _touchISR() {
    if (digitalRead(PIN_HW763) == LOW) {
        s_isrDown   = true;
        s_isrDownAt = millis();
    } else {
        s_isrUp = true;
    }
}

// ---- Public API ----

void init() {
    pinMode(PIN_HW763, INPUT_PULLUP);
    s_held           = false;
    s_heldSince      = 0;
    s_longPressFired = false;
    s_tapCount       = 0;
    s_lastTapUpMs    = 0;
    s_lastFallMs     = 0;
    s_lastRiseMs     = 0;
    s_isrDown        = false;
    s_isrUp          = false;
    attachInterrupt(digitalPinToInterrupt(PIN_HW763), _touchISR, CHANGE);
    DBG("InputManager: HW-763 GPIO%d INPUT_PULLUP attached", PIN_HW763);
}

void update() {
    uint32_t now = millis();

    // --- Atomically snapshot and clear ISR flags ---
    bool gotDown, gotUp;
    uint32_t downAt;
    noInterrupts();
    gotDown     = s_isrDown;
    gotUp       = s_isrUp;
    downAt      = s_isrDownAt;
    s_isrDown   = false;
    s_isrUp     = false;
    interrupts();

    // Direct hardware check if ISR was missed
    if (digitalRead(PIN_HW763) == LOW && !s_held) {
        gotDown = true;
        downAt  = now;
    }

    // --- Process FALLING edge (touch start) ---
    if (gotDown) {
        if ((now - s_lastFallMs) >= SW_DEBOUNCE_MS) {
            s_lastFallMs     = downAt;
            s_held           = true;
            s_heldSince      = downAt;
            s_longPressFired = false;
            DBG("InputManager: DOWN at %lu", downAt);
        }
    }

    // --- Check if LONG_PRESS threshold reached WHILE holding ---
    if (s_held) {
        DisplayManager::resetIdleTimer();
        if (!s_longPressFired) {
            if ((now - s_heldSince) >= SLEEP_LONG_PRESS_MS) {
                s_longPressFired = true;
                s_tapCount       = 0; // cancel any partial multi-taps
                DBG("InputManager: -> LONG_PRESS (%lu ms)", now - s_heldSince);
                _enqueue(TouchEvent::LONG_PRESS);
            }
        }
    }

    // --- Process RISING edge (touch released) ---
    if (gotUp && s_held) {
        if ((now - s_lastRiseMs) >= SW_DEBOUNCE_MS) {
            if (digitalRead(PIN_HW763) == LOW) {
                // Pin still LOW — glitch, ignore
            } else {
                s_lastRiseMs = now;
                uint32_t held_ms = now - s_heldSince;
                s_held = false;
                DBG("InputManager: UP held=%lu ms (longFired=%d)", held_ms, s_longPressFired);

                if (!s_longPressFired && held_ms >= SW_DEBOUNCE_MS) {
                    s_tapCount++;
                    s_lastTapUpMs = now;

                    // Check if 3 taps reached immediately
                    if (s_tapCount >= 3) {
                        DBG("InputManager: -> TRIPLE_PRESS");
                        _enqueue(TouchEvent::TRIPLE_PRESS);
                        s_tapCount = 0;
                    }
                }
                s_longPressFired = false;
            }
        }
    }

    // --- Deliver pending taps after gap timeout ---
    if (s_tapCount > 0 && !s_held) {
        if ((now - s_lastTapUpMs) >= TAP_TIMEOUT_MS) {
            DBG("InputManager: -> SHORT_PRESS (tapCount=%d)", s_tapCount);
            _enqueue(TouchEvent::SHORT_PRESS);
            s_tapCount = 0;
        }
    }

    // Safety clear if pin released but UP was missed
    if (s_held && digitalRead(PIN_HW763) == HIGH && (now - s_heldSince > 3000UL)) {
        s_held           = false;
        s_longPressFired = false;
        s_tapCount       = 0;
    }
}

TouchEvent popEvent() {
    if (s_qCount == 0) return TouchEvent::NONE;
    TouchEvent ev = s_queue[s_qHead];
    s_qHead = (s_qHead + 1) % EV_QUEUE_DEPTH;
    s_qCount--;
    return ev;
}

TouchEvent peekEvent() {
    if (s_qCount == 0) return TouchEvent::NONE;
    return s_queue[s_qHead];
}

} // namespace InputManager
