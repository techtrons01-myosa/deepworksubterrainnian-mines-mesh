// ============================================================
//  MYOSA Node Firmware — Input Manager
//  File    : input_manager.h
//  Purpose : HW-763 capacitive touch sensor interrupt handler.
//            Classifies presses as SHORT or LONG and delivers
//            them to other managers through a polled event queue.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"
#include "types.h"

namespace InputManager {

    /** Attach interrupt to PIN_HW763 and initialise state. */
    void init();

    /** Call every loop() — advances press-timing state machine. */
    void update();

    /** Returns (and clears) the most recent touch event.
     *  Returns TouchEvent::NONE when the queue is empty. */
    TouchEvent popEvent();

    /** Returns the next queued event WITHOUT removing it.
     *  Returns TouchEvent::NONE when the queue is empty.
     *  Use this to inspect the event type before deciding to pop. */
    TouchEvent peekEvent();

} // namespace InputManager
