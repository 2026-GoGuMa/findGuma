#include "L3_FSMevent.h"
#include "mbed.h"
#include "protocol_parameters.h"

// ARQ retransmission timer
static Timeout timer;
static uint8_t timerStatus = 0;

// timer event : ARQ timeout
void L3_timer_timeoutHandler(void) {
  timerStatus = 0;
  L3_event_setEventFlag(L3_event_timeout);
}

// timer related functions ---------------------------
void L3_timer_startTimer(uint8_t waitTime) {
  timer.attach(L3_timer_timeoutHandler, waitTime);
  timerStatus = 1;
}

void L3_timer_stopTimer() {
  timer.detach();
  timerStatus = 0;
}

uint8_t L3_timer_getTimerStatus() { return timerStatus; }
