// ============================================================
//  MYOSA Node Firmware — Telemetry Manager
//  File    : telemetry_manager.h
//  Purpose : Builds typed MyosaPacket bodies from sensor
//            snapshots and dispatches them via MeshManager
//            with rate-limiting.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace TelemetryManager {

    /** Initialise rate-limit timer. */
    void init();

    /** Call every loop() — sends SENSOR_DATA when due. */
    void update();

    /** Force an immediate SENSOR_DATA broadcast (e.g. on request). */
    void sendNow();

} // namespace TelemetryManager
