// 패킷을 만들고(직렬화), 분해하는(역직렬화) 함수들의 구현 파일
// buildTxn, buildCnf(보낼때): 변수 → 바이트 배열로 패킹
// parseRec, parseMch(받을 때): 바이트 배열 → 구조체로 언패킹

#include "L3_msg.h"

#include "mbed.h"

// TXN 패킷 만들기 (Trader가 Coordinator에게 보내는 거래 정보 패킷)
uint8_t L3_msg_buildTxn(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, int8_t signal, uint8_t isSeller,
                         uint8_t goods, uint16_t price) {
  buf[L3_MSG_OFFSET_TYPE]                        = L3_MSG_TYPE_TXN;
  buf[L3_MSG_OFFSET_SEQ]                         = seq;
  buf[L3_MSG_OFFSET_SRCID]                       = myId;
  buf[L3_MSG_OFFSET_DESTID]                      = coordId;
  buf[L3_MSG_OFFSET_DATA + L3_TXN_OFFSET_ID]     = myId;
  buf[L3_MSG_OFFSET_DATA + L3_TXN_OFFSET_SIGNAL] = (uint8_t)signal;
  buf[L3_MSG_OFFSET_DATA + L3_TXN_OFFSET_ISSELLER] = isSeller;
  buf[L3_MSG_OFFSET_DATA + L3_TXN_OFFSET_GOODS]  = goods;
  buf[L3_MSG_OFFSET_DATA + L3_TXN_OFFSET_PRICE]  = (uint8_t)(price >> 8);
  buf[L3_MSG_OFFSET_DATA + L3_TXN_OFFSET_PRICE + 1] = (uint8_t)(price & 0xFF);
  return L3_MSG_OFFSET_DATA + 6;
}

// CNF 패킷 만들기 (Trader가 Coordinator에게 보내는 확인 응답 패킷)
uint8_t L3_msg_buildCnf(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, uint16_t price_cnf, uint16_t loc_cnf) {
  buf[L3_MSG_OFFSET_TYPE]        = L3_MSG_TYPE_CNF;
  buf[L3_MSG_OFFSET_SEQ]         = seq;
  buf[L3_MSG_OFFSET_SRCID]       = myId;
  buf[L3_MSG_OFFSET_DESTID]      = coordId;
  buf[L3_MSG_OFFSET_DATA + 0]    = (uint8_t)(price_cnf >> 8);
  buf[L3_MSG_OFFSET_DATA + 1]    = (uint8_t)(price_cnf & 0xFF);
  buf[L3_MSG_OFFSET_DATA + 2]    = (uint8_t)(loc_cnf >> 8);
  buf[L3_MSG_OFFSET_DATA + 3]    = (uint8_t)(loc_cnf & 0xFF);
  return L3_MSG_OFFSET_DATA + 4;
}

// 수신 버퍼가 어떤 종류의 패킷인지 판별할 때 사용
uint8_t L3_msg_getPduType(uint8_t* buf) { return buf[L3_MSG_OFFSET_TYPE]; }


// REC 패킷 파싱
rec_payload_t L3_msg_parseRec(uint8_t* buf) {
  rec_payload_t rec;
  rec.avg_price = ((uint16_t)buf[L3_MSG_OFFSET_DATA + 0] << 8)
                |             buf[L3_MSG_OFFSET_DATA + 1];
  rec.avg_loc   = ((uint16_t)buf[L3_MSG_OFFSET_DATA + 2] << 8)
                |             buf[L3_MSG_OFFSET_DATA + 3];
  return rec;
}

// MCH 패킷 파싱
mch_payload_t L3_msg_parseMch(uint8_t* buf) {
  mch_payload_t mch;
  mch.match_success = buf[L3_MSG_OFFSET_DATA];
  return mch;
}
