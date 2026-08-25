// ============================================================
//  MYOSA Node Firmware — Wi-Fi Manager
//  File    : wifi_manager.h
//  Purpose : Conditional Wi-Fi activation with a full anti-flap
//            state machine.  Wi-Fi stays OFF during normal mesh
//            operation; it activates only when both the ESP-NOW
//            condition AND the BLE condition are simultaneously
//            satisfied.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace WiFiManager {

    /** Initialise state machine (Wi-Fi stays OFF). */
    void init();

    /** Call every loop() — evaluates conditions, drives state machine. */
    void update();

    /** Returns true if Wi-Fi is currently ON. */
    bool isWifiOn();

    /** Returns true if actively connected to an AP. */
    bool isConnected();

    /** External trigger: force Wi-Fi ON (e.g. gateway command).
     *  Still subject to anti-flap delay. */
    void requestActivation();

    /** Force Wi-Fi OFF immediately. */
    void forceOff();

    /** Returns current state machine state (for diagnostics). */
    WiFiState getState();

} // namespace WiFiManager
