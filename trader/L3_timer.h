#pragma once
#include "mbed.h"

// 타이머 시작
void L3_timer_startTimer(uint8_t waitSec);

// REC 수신하면 타이머 멈추고 다음 단계 타이머 시작
void L3_timer_stopTimer();

// 타임아웃 이벤트 발생 시 FSM이 감지
uint8_t L3_timer_getTimerStatus();
