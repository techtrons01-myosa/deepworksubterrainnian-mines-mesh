// ============================================================
//  MYOSA Node Firmware — Alert / Buzzer Manager
//  File    : alert_manager.h
//  Purpose : Non-blocking buzzer pattern engine.
//            Patterns are queued and played using millis()
//            timers so that the main loop is never blocked.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace AlertManager {

    /** Initialise GPIO and silence the buzzer. */
    void init();

    /** Call every loop() iteration to advance pattern playback. */
    void update();

    /** Queue a pattern.  Higher-priority patterns interrupt lower ones.
     *  Priority order (highest→lowest): FAULT > NETWORK_CRITICAL > NODE_LOST
     *                                   > NODE_DISCOVERED > BOOT > NONE     */
    void play(BuzzerPattern pattern);

    /** Immediately silence the buzzer and clear the queue. */
    void silence();

    /** Returns true while a pattern is actively playing. */
    bool isPlaying();

} // namespace AlertManager
