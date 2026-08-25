// ============================================================
//  MYOSA Node Firmware — Power Manager Implementation
//  File    : power_manager.cpp
//
//  Display control (CPU always stays awake):
//
//  LONG PRESS (5s) → toggle display OFF / ON
//  SHORT PRESS (display off) → wake display
//  RAISE-TO-WAKE  → MPU6050 motion wakes display automatically
//
//  KEY ARCHITECTURE NOTE:
//  PowerManager runs BEFORE DisplayManager in loop().
//  PowerManager peeks the event queue for LONG_PRESS first.
//  If it IS a LONG_PRESS it pops and handles it.
//  If it is NOT a LONG_PRESS it does NOT pop — leaving SHORT_PRESS
//  for DisplayManager to consume.
//
//  WAKE BUG FIX:
//  Previously DisplayManager::update() called popEvent()
//  unconditionally, potentially consuming LONG_PRESS before
//  PowerManager processed it (when display was already toggled
//  correctly the PREVIOUS loop but the flag state was wrong).
//  Now DisplayManager only peeks then pops SHORT_PRESS explicitly,
//  never touching LONG_PRESS events.
// ============================================================
#include "power_manager.h"
#include "input_manager.h"
#include "display_manager.h"
#include "alert_manager.h"
#include "sensor_manager.h"

namespace PowerManager {

static bool     s_displayOn        = true;
static uint32_t s_lastDisplayOffMs = 0;

void init() {
    s_lastDisplayOffMs = 0;
    DBG("PowerManager: init (CPU always awake, display-blanking mode)");
}

void update() {
    // ---- 1. LONG_PRESS → toggle display (unless in Locator Cycle Select mode) ----
    if (InputManager::peekEvent() == TouchEvent::LONG_PRESS) {
        if (DisplayManager::isDisplayOn() &&
            DisplayManager::getScreen() == MenuScreen::LOCATOR &&
            DisplayManager::isLocatorCycleMode()) {
            // Passthrough: DisplayManager will consume LONG_PRESS to lock in the target node!
            return;
        }

        InputManager::popEvent();  // consume

        if (!DisplayManager::isDisplayOn()) {
            DisplayManager::displayOn();
            AlertManager::play(BuzzerPattern::NODE_DISCOVERED);  // single short beep
            DBG("PowerManager: LONG_PRESS -> display ON");
        } else {
            DisplayManager::displayOff();
            SensorManager::isMotionDetected(); // flush lingering motion
            DBG("PowerManager: LONG_PRESS -> display OFF");
        }
        return;
    }

    // ---- 2. Raise-to-wake via MPU6050 motion / gyro ----
    // Only when display is OFF, and at least 3.0s after the screen turned off
    if (!DisplayManager::isDisplayOn() && SensorManager::isMpuOk()) {
        uint32_t now = millis();
        if ((now - DisplayManager::getLastSleepMs()) > 3000UL) {  // 3s cooldown after sleep
            if (SensorManager::isMotionDetected()) {
                DisplayManager::displayOn();
                AlertManager::play(BuzzerPattern::NODE_DISCOVERED);
                DBG("PowerManager: RAISE-TO-WAKE -> display ON");
            }
        } else {
            // Drain motion during cooldown
            SensorManager::isMotionDetected();
        }
    }
}

PowerState getState() {
    return PowerState::ACTIVE;
}

bool isSleeping() {
    return false;
}

} // namespace PowerManager
