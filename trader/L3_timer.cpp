/*
  startTimer(5)
      ↓
    5초 경과
      ↓
  timeoutHandler() 자동 호출  ←── mbed 인터럽트
      ↓
  L3_event_timeout 플래그 세팅
      ↓
  FSM 루프에서 감지 → 실패 처리
*/

#include "L3_FSMevent.h"
#include "mbed.h"
#include "protocol_parameters.h"

static Timeout timer;
static uint8_t timerStatus = 0;    // 타이머 동작 중 여부 (1: 동작, 0: 정지)


// 타임아웃 발생 시 자동 호출 (인터럽트 핸들러)
void L3_timer_timeoutHandler(void) {
  timerStatus = 0;
  L3_event_setEventFlag(L3_event_timeout);   // FSM에 timeout 이벤트 알림
}

// 타이머 시작
void L3_timer_startTimer(uint8_t waitSec) {
  timer.attach(L3_timer_timeoutHandler, waitSec);
  timerStatus = 1;            // waitSec초 후에 timeoutHandler를 자동 호출하도록 등록
}

// 타이머 중단 
void L3_timer_stopTimer() {
  timer.detach();     // 등록된 핸들러 해제 → 타임아웃 이벤트 발생 안 함
  timerStatus = 0;
}

uint8_t L3_timer_getTimerStatus() { return timerStatus; }
