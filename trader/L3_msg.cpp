#include "L3_msg.h"

#include "mbed.h"

// TXN 패킷 만들기 — coordinator의 L3_msg_decodeTxn이 읽는 순서와 정확히 일치
// coordinator: payload[0]=id, [1]=isSeller, [2]=goods, [3:4]=price (5바이트)
uint8_t L3_msg_buildTxn(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, uint8_t isSeller,
                         uint8_t goods, uint16_t price) {
  buf[L3_MSG_OFFSET_TYPE]                              = L3_MSG_TYPE_TXN;
  buf[L3_MSG_OFFSET_SEQ]                               = (uint8_t)seq;
  buf[L3_MSG_OFFSET_SRCID]                             = myId;
  buf[L3_MSG_OFFSET_DESTID]                            = coordId;
  buf[L3_MSG_OFFSET_PAYLOAD + L3_TXN_OFFSET_ID]        = myId;
  buf[L3_MSG_OFFSET_PAYLOAD + L3_TXN_OFFSET_ISSELLER]  = isSeller;
  buf[L3_MSG_OFFSET_PAYLOAD + L3_TXN_OFFSET_GOODS]     = goods;
  buf[L3_MSG_OFFSET_PAYLOAD + L3_TXN_OFFSET_PRICE]     = (uint8_t)(price >> 8);
  buf[L3_MSG_OFFSET_PAYLOAD + L3_TXN_OFFSET_PRICE + 1] = (uint8_t)(price & 0xFF);
  return L3_MSG_OFFSET_PAYLOAD + 5;
}

// CNF 패킷 만들기 — coordinator의 L3_CNF_OFFSET_ACCPT(=0) 위치에 accept 1바이트
// coordinator 기대값: payload[0] == 1 이면 수락, 0 이면 거절
uint8_t L3_msg_buildCnf(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, uint8_t accept) {
  buf[L3_MSG_OFFSET_TYPE]    = L3_MSG_TYPE_CNF;
  buf[L3_MSG_OFFSET_SEQ]     = (uint8_t)seq;
  buf[L3_MSG_OFFSET_SRCID]   = myId;
  buf[L3_MSG_OFFSET_DESTID]  = coordId;
  buf[L3_MSG_OFFSET_PAYLOAD] = accept;
  return L3_MSG_OFFSET_PAYLOAD + 1;
}

// 타입 필드만 꺼내기
uint8_t L3_msg_getPduType(uint8_t* buf) { return buf[L3_MSG_OFFSET_TYPE]; }

// REC 파싱 — coordinator의 L3_msg_encodeRec는 L3_MSG_OFFSET_PAYLOAD 위치에
// uint16_t 한 값(2바이트)만 넣습니다. 가격 REC와 위치 REC 모두 같은 형식이며,
// FSM이 현재 상태(WAIT_PRICE_REC / WAIT_LOC_REC)로 의미를 구분합니다.
uint16_t L3_msg_parseRec(uint8_t* buf) {
  return ((uint16_t)buf[L3_MSG_OFFSET_PAYLOAD] << 8)
        |            buf[L3_MSG_OFFSET_PAYLOAD + 1];
}

// MCH 파싱 — coordinator의 L3_msg_encodeMch는 accept 1바이트만 넣습니다.
uint8_t L3_msg_parseMch(uint8_t* buf) {
  return buf[L3_MSG_OFFSET_PAYLOAD];
}
