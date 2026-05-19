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
  L3_event_msgRcvd      = 0,  // L2로부터 메시지 수신됨(하위 계층 → L3)
  L3_event_dataSendCnf  = 1,  // 데이터 전송 완료 확인 
  L3_event_waitPairRcvd = 2,  // Event A: Coordinator로부터 WAIT_PAIR 수신
  L3_event_recRcvd      = 3,  // Event B: Coordinator로부터 REC PDU 수신
  L3_event_mchRcvd      = 4,  // Event C: Coordinator로부터 MCH PDU 수신
  L3_event_timeout      = 5,  // Event D: 타이머 만료
  L3_event_recfgSrcIdCnf = 6, // SrcId 재설정 완료 확인
} L3_event_e;

void L3_event_setEventFlag(L3_event_e event);     // 이벤트 발생 표시 (1로 세팅)  
void L3_event_clearEventFlag(L3_event_e event);   // 특정 이벤트 처리 완료 (0으로 클리어) 
void L3_event_clearAllEventFlag(void);            // 모든 이벤트 초기화  
int L3_event_checkEventFlag(L3_event_e event);    // 해당 이벤트가 발생했는지 확인 (0 또는 1 반환)
