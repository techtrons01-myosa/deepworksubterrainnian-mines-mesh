// ============================================================
//  MYOSA Node Firmware — System Manager
//  File    : system_manager.h
//  Purpose : Boot sequencing, global node identity, fault
//            registry, and degraded-operation logic.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace SystemManager {

    /** Call once in setup() before any other manager init. */
    void     init();

    /** Periodic housekeeping; call in loop(). */
    void     update();

    // ---- Node Identity ----
    /** Returns the null-terminated node ID string (e.g. "MYO-A1B2C3"). */
    const char* getNodeId();

    /** Returns the raw MAC bytes of this ESP32 (6 bytes). */
    const uint8_t* getMac();

    // ---- Fault Registry ----
    /** Register one or more faults (OR into the global mask). */
    void     registerFault(uint16_t faultBits);

    /** Clear a specific fault once it has been recovered. */
    void     clearFault(uint16_t faultBits);

    /** Returns the current fault bitmask. */
    uint16_t getFaultMask();

    /** Returns true if any fault bit is set. */
    bool     hasFault();

    // ---- Reset / Reboot ----
    /** Graceful reboot after saving critical state. */
    void     reboot(const char* reason);

    // ---- Uptime ----
    /** Returns uptime in seconds. */
    uint32_t uptimeSeconds();

} // namespace SystemManager
