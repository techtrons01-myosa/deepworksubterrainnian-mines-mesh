// ============================================================
//  MYOSA Node Firmware — Power Manager
//  File    : power_manager.h
//  Purpose : Sleep/wake lifecycle driven by HW-763 touch events.
//            Coordinates the orderly shutdown and restart of
//            all managers when entering and leaving light sleep.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace PowerManager {

    /** Initialise power state (ACTIVE). */
    void init();

    /** Call every loop() — consumes HW-763 events, drives state. */
    void update();

    /** Returns current power state. */
    PowerState getState();

    /** Returns true if the node is in SLEEPING or SLEEP_PENDING. */
    bool isSleeping();

} // namespace PowerManager
