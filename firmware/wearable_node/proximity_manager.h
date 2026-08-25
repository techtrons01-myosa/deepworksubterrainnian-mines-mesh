// ============================================================
//  MYOSA Node Firmware — Proximity Manager
//  File    : proximity_manager.h
//  Purpose : Fuses BLE RSSI and APDS9960 proximity readings
//            into a single normalised distance estimate per
//            known peer.  Updates NodeRecord.proximityEstimate.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace ProximityManager {

    /** Initialise (no hardware — just state). */
    void init();

    /** Call every loop() — fuses BLE + APDS data, updates records. */
    void update();

    /** Returns a 0–255 proximity estimate for the nearest peer.
     *  255 = touching, 0 = out of range. */
    uint8_t nearestProximity();

} // namespace ProximityManager
