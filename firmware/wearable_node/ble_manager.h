// ============================================================
//  MYOSA Node Firmware — BLE Manager
//  File    : ble_manager.h
//  Purpose : BLE advertising (MYOSA-XXX identity) and periodic
//            scanning for other MYOSA nodes.  RSSI per detected
//            peer is made available to ProximityManager.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace BleManager {

    /** Initialise BLE stack, start advertising, and arm scanner.
     *  Registers FAULT_BLE_INIT if stack fails. */
    bool init(const char* nodeId);

    /** Call every loop() — triggers periodic BLE scans. */
    void update();

    /** Returns true if BLE is operational. */
    bool isBleOk();

    /** Returns true if at least one MYOSA-prefixed device has
     *  been seen in the last BLE_SCAN_INTERVAL_MS window. */
    bool myosaPeerSeen();

    /** Returns the RSSI of the nearest MYOSA peer, or 0 if none. */
    int8_t nearestPeerRssi();

    /** Stop advertising and scanning (call before sleep). */
    void suspend();

    /** Resume advertising and scanning (call after wake). */
    void resume();

} // namespace BleManager
