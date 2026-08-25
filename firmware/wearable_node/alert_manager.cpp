// ============================================================
//  MYOSA Node Firmware — Alert / Buzzer Manager Implementation
//  File    : alert_manager.cpp
// ============================================================
#include "alert_manager.h"

namespace AlertManager {

// ---- Buzzer tone step ----
struct Step {
    uint16_t onMs;    // 0 = silent step (pause)
    uint16_t offMs;   // gap between steps
};

// ---- Pattern definitions ----
// Each pattern is a null-terminated array of Steps.
// Final sentinel: {0, 0}

static const Step PAT_BOOT[] = {
    { BUZZ_SHORT_MS, BUZZ_PAUSE_MS },
    { BUZZ_SHORT_MS, BUZZ_PAUSE_MS },
    { BUZZ_LONG_MS,  0 },
    { 0, 0 }
};

static const Step PAT_NODE_DISCOVERED[] = {
    { BUZZ_SHORT_MS, 0 },
    { 0, 0 }
};

// NODE_LOST: long-long-long beep — unmistakable "disconnect" sound
static const Step PAT_NODE_LOST[] = {
    { BUZZ_DISCONNECT_MS, BUZZ_PAUSE_MS },
    { BUZZ_DISCONNECT_MS, BUZZ_PAUSE_MS },
    { BUZZ_DISCONNECT_MS, 0 },
    { 0, 0 }
};

static const Step PAT_NETWORK_CRITICAL[] = {
    { BUZZ_SHORT_MS, BUZZ_PAUSE_MS },
    { BUZZ_SHORT_MS, BUZZ_PAUSE_MS },
    { BUZZ_SHORT_MS, BUZZ_PAUSE_MS },
    { BUZZ_SHORT_MS, BUZZ_PAUSE_MS },
    { BUZZ_SHORT_MS, 0 },
    { 0, 0 }
};

static const Step PAT_FAULT[] = {
    { BUZZ_LONG_MS, BUZZ_PAUSE_MS },
    { BUZZ_LONG_MS, BUZZ_PAUSE_MS },
    { BUZZ_LONG_MS, 0 },
    { 0, 0 }
};

// MINE_ALERT: rapid 5-beep burst — critical environment safety alarm
static const Step PAT_MINE_ALERT[] = {
    { BUZZ_SHORT_MS, 80 },
    { BUZZ_SHORT_MS, 80 },
    { BUZZ_SHORT_MS, 80 },
    { BUZZ_SHORT_MS, 80 },
    { BUZZ_SHORT_MS, 0 },
    { 0, 0 }
};

// DISCONNECTED: 5-second continuous tone followed by a 2-second break
static const Step PAT_DISCONNECTED[] = {
    { 5000, 2000 },  // 5.0s ON, 2.0s OFF
    { 0, 0 }
};

// EVACUATION: urgent pulsating industrial evacuation siren
static const Step PAT_EVACUATION[] = {
    { 300, 100 },
    { 300, 100 },
    { 300, 100 },
    { 600, 200 },
    { 0, 0 }
};

// Lookup table indexed by BuzzerPattern value
static const Step* const PATTERNS[] = {
    nullptr,              // NONE
    PAT_BOOT,             // BOOT
    PAT_NODE_DISCOVERED,  // NODE_DISCOVERED
    PAT_NODE_LOST,        // NODE_LOST
    PAT_NETWORK_CRITICAL, // NETWORK_CRITICAL
    PAT_FAULT,            // FAULT
    PAT_MINE_ALERT,       // MINE_ALERT
    PAT_DISCONNECTED,     // DISCONNECTED
    PAT_EVACUATION,       // EVACUATION (8)
};

static const uint8_t PATTERN_PRIORITY[] = {
    0,  // NONE
    1,  // BOOT
    2,  // NODE_DISCOVERED
    4,  // NODE_LOST
    5,  // NETWORK_CRITICAL
    6,  // FAULT
    7,  // MINE_ALERT
    3,  // DISCONNECTED
    9,  // EVACUATION       ← absolute highest
};

// ---- Playback state ----
static BuzzerPattern  s_current      = BuzzerPattern::NONE;
static BuzzerPattern  s_queued       = BuzzerPattern::NONE;
static const Step*    s_steps        = nullptr;
static uint8_t        s_stepIndex    = 0;
static bool           s_buzzOn       = false;
static uint32_t       s_stepTimer    = 0;

// ---- Internal helpers ----
// Active Buzzer: pure DC HIGH activates internal oscillator at maximum loudness
static void _buzzerOn()  { 
    digitalWrite(PIN_BUZZER, HIGH); 
    s_buzzOn = true;  
}
static void _buzzerOff() { 
    digitalWrite(PIN_BUZZER, LOW);  
    s_buzzOn = false; 
}

static void _startPattern(BuzzerPattern p) {
    uint8_t idx = static_cast<uint8_t>(p);
    if (idx == 0 || idx >= sizeof(PATTERNS)/sizeof(PATTERNS[0])) {
        s_current = BuzzerPattern::NONE;
        s_steps   = nullptr;
        _buzzerOff();
        return;
    }
    s_current   = p;
    s_steps     = PATTERNS[idx];
    s_stepIndex = 0;

    // Start the first ON phase immediately
    _buzzerOn();
    s_stepTimer = millis();
}

// ---- Public API ----

void init() {
    pinMode(PIN_BUZZER, OUTPUT);
    _buzzerOff();
    DBG("AlertManager: init OK (buzzer GPIO=%d)", PIN_BUZZER);
}

void update() {
    // ---- Promote a queued pattern if nothing is playing ----
    if (s_current == BuzzerPattern::NONE && s_queued != BuzzerPattern::NONE) {
        BuzzerPattern next = s_queued;
        s_queued = BuzzerPattern::NONE;
        _startPattern(next);
    }

    if (s_current == BuzzerPattern::NONE || s_steps == nullptr) return;

    const Step& step = s_steps[s_stepIndex];

    // Sentinel reached — pattern finished
    if (step.onMs == 0 && step.offMs == 0) {
        _buzzerOff();
        s_current   = BuzzerPattern::NONE;
        s_steps     = nullptr;

        // Play queued pattern if any
        if (s_queued != BuzzerPattern::NONE) {
            BuzzerPattern next = s_queued;
            s_queued = BuzzerPattern::NONE;
            _startPattern(next);
        }
        return;
    }

    uint32_t now     = millis();
    uint32_t elapsed = now - s_stepTimer;

    if (s_buzzOn) {
        // Currently in the ON phase
        if (elapsed >= step.onMs) {
            _buzzerOff();
            s_stepTimer = now;
            // If there is no off gap, advance immediately
            if (step.offMs == 0) {
                s_stepIndex++;
                if (s_steps[s_stepIndex].onMs != 0 || s_steps[s_stepIndex].offMs != 0) {
                    _buzzerOn();
                    s_stepTimer = now;
                }
            }
        }
    } else {
        // Currently in the OFF/gap phase
        if (elapsed >= step.offMs) {
            s_stepIndex++;
            const Step& next = s_steps[s_stepIndex];
            if (next.onMs == 0 && next.offMs == 0) {
                // Finished
                s_current = BuzzerPattern::NONE;
                s_steps   = nullptr;
            } else {
                _buzzerOn();
                s_stepTimer = now;
            }
        }
    }
}

void play(BuzzerPattern pattern) {
    if (pattern == BuzzerPattern::NONE) return;

    uint8_t newPri = PATTERN_PRIORITY[static_cast<uint8_t>(pattern)];
    uint8_t curPri = PATTERN_PRIORITY[static_cast<uint8_t>(s_current)];

    if (s_current == BuzzerPattern::NONE) {
        // Nothing playing — start immediately
        _startPattern(pattern);
    } else if (newPri > curPri) {
        // Higher priority — interrupt current, queue new
        _buzzerOff();
        _startPattern(pattern);
    } else {
        // Lower/equal priority — store in queue (only one queued slot)
        uint8_t quePri = PATTERN_PRIORITY[static_cast<uint8_t>(s_queued)];
        if (newPri > quePri) {
            s_queued = pattern;
        }
    }
}

void silence() {
    _buzzerOff();
    s_current   = BuzzerPattern::NONE;
    s_queued    = BuzzerPattern::NONE;
    s_steps     = nullptr;
}

bool isPlaying() {
    return s_current != BuzzerPattern::NONE;
}

} // namespace AlertManager
