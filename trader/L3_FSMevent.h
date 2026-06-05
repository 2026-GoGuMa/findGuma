#pragma once
// 이벤트 종류를 정의하는 헤더 
/*
  [인터럽트/타이머]         [FSM 루프]
  setEventFlag(X)   →→→   checkEventFlag(X) == 1
                                ↓
                           동작 수행
                                ↓
                           clearEventFlag(X)
*/

typedef enum L3_event {
  L3_event_msgRcvd = 2,
  L3_event_timeout = 3,
  L3_event_dataToSend = 4,
  L3_event_dataSendCnf = 5,
  L3_event_recfgSrcIdCnf = 6
} L3_event_e;

void L3_event_setEventFlag(L3_event_e event);     // 이벤트 발생 표시 (1로 세팅)  
void L3_event_clearEventFlag(L3_event_e event);   // 특정 이벤트 처리 완료 (0으로 클리어) 
void L3_event_clearAllEventFlag(void);            // 모든 이벤트 초기화  
int L3_event_checkEventFlag(L3_event_e event);    // 해당 이벤트가 발생했는지 확인 (0 또는 1 반환)
