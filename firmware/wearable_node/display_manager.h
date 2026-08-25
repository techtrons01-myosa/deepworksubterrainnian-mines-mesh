// ============================================================
//  MYOSA Node Firmware — Display Manager
//  File    : display_manager.h
//  Purpose : SSD1306 OLED UI with a 6-screen gesture-driven
//            menu.  Reads live data from SensorManager and
//            MeshManager to populate each screen.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace DisplayManager {

    /** Initialise SSD1306 and render the Home screen.
     *  Registers FAULT_OLED_INIT if the display is absent. */
    bool init();

    /** Call every loop():
     *  - Consumes gesture events from SensorManager.
     *  - Refreshes the active screen at ~4 Hz.
     *  - Returns to Home after OLED_IDLE_TIMEOUT_MS. */
    void update();

    /** Force a specific screen (e.g. show Alerts on node-loss). */
    void setScreen(MenuScreen screen);

    /** Returns the currently active screen. */
    MenuScreen getScreen();

    /** Push a one-line alert message that appears on the Alerts
     *  screen and briefly on the Home screen ticker. */
    void pushAlert(const char* msg);

    /** Turn the display off (sleep mode). */
    void displayOff();

    /** Turn the display on and refresh. */
    void displayOn();

    /** Returns true if OLED display is currently ON. */
    bool isDisplayOn();

    /** Reset the 5-second idle auto-sleep timer. */
    void resetIdleTimer();

    /** Returns true if Locator screen is in Manual Cycle Selection mode. */
    bool isLocatorCycleMode();

    /** Returns timestamp (millis) when display was turned off. */
    uint32_t getLastSleepMs();

} // namespace DisplayManager
