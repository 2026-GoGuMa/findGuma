typedef enum L3_event {
  L3_event_msgRcvd = 2,        // 메시지 수신됨
  L3_event_timeout = 3,        // 타이머 만료
  L3_event_dataToSend = 4,     // 실질적 사용 x
  L3_event_dataSendCnf = 5,    // 실질적 사용 x
  L3_event_recfgSrcIdCnf = 6,  // 실질적 사용 x
  // L3_event_waitPairRcvd = 7,
  // L3_event_recRcvd = 8,
  // L3_event_mchRcvd = 9,
  L3_event_userAccept = 10,  // 사용자가 수락
  L3_event_userReject = 11   // 사용자가 거절
} L3_event_e;

void L3_event_setEventFlag(L3_event_e event);
void L3_event_clearEventFlag(L3_event_e event);
void L3_event_clearAllEventFlag(void);
int L3_event_checkEventFlag(L3_event_e event);
