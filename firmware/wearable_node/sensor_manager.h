// ============================================================
//  MYOSA Node Firmware — Sensor Manager
//  File    : sensor_manager.h
//  Purpose : Periodic, non-blocking acquisition from BMP180,
//            MPU6050, and APDS9960.  Consumers call
//            getSensorData() and popGesture() to retrieve
//            the latest values.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace SensorManager {

    /** Initialise all three sensors.  Registers faults via
     *  SystemManager if any sensor fails.  Returns false if
     *  ALL sensors failed (node cannot sense anything). */
    bool init();

    /** Call every loop() iteration.  Acquires new readings on
     *  SENSOR_READ_INTERVAL_MS cadence; gestures are always
     *  polled (latency-sensitive). */
    void update();

    /** Returns a const reference to the latest sensor snapshot.
     *  The snapshot is updated atomically under a critical section. */
    const SensorData& getSensorData();

    /** Returns the next pending gesture event (FIFO, depth=4)
     *  and removes it from the queue.  Returns GestureEvent::NONE
     *  when the queue is empty. */
    GestureEvent popGesture();

    /** Returns true if any sensor is operational. */
    bool isAnyOnline();

    /** Returns true if MPU6050 initialised successfully. */
    bool isMpuOk();

    /** Returns true if the MPU6050 detected significant motion
     *  since the last call (auto-clears after read).
     *  Used by PowerManager to implement raise-to-wake. */
    bool isMotionDetected();

    /** Returns true if BURIED condition is active:
     *  APDS9960 proximity HIGH + darkness + BMP180 pressure elevated.
     *  Latches until conditions clear for one full read cycle. */
    bool isBuriedDetected();

} // namespace SensorManager
