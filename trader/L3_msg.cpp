// 패킷을 만들고(직렬화), 분해하는(역직렬화) 함수들의 구현 파일 
// buildTxn, buildCnf(보낼때): 변수 → 바이트 배열로 패킹 
// parseRec, parseMch(받을 때): 바이트 배열 → 구조체로 언패킹

#include "L3_msg.h"

#include "mbed.h"

// TXN 패킷 만들기 (Trader가 Coordinator에게 보내는 거래 정보 패킷)
uint8_t L3_msg_buildTxn(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint8_t seq, int8_t signal, uint8_t isSeller,
                         uint8_t goods, uint16_t price) {
  buf[PDU_OFFSET_TYPE]        = PDU_TYPE_TXN;
  buf[PDU_OFFSET_SEQ]         = seq;
  buf[PDU_OFFSET_SRC]         = myId;
  buf[PDU_OFFSET_DST]         = coordId;
  buf[PDU_OFFSET_PAYLOAD + 0] = (uint8_t)signal;
  buf[PDU_OFFSET_PAYLOAD + 1] = isSeller;
  buf[PDU_OFFSET_PAYLOAD + 2] = goods;
  buf[PDU_OFFSET_PAYLOAD + 3] = (uint8_t)(price >> 8);
  buf[PDU_OFFSET_PAYLOAD + 4] = (uint8_t)(price & 0xFF);
  return PDU_HEADER_SIZE + 5;
}

// CNF 패킷 만들기 (Trader가 Coordinator에게 보내는 확인 응답 패킷)
uint8_t L3_msg_buildCnf(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint8_t seq, uint16_t price_cnf, uint16_t loc_cnf) {
  buf[PDU_OFFSET_TYPE]        = PDU_TYPE_CNF;
  buf[PDU_OFFSET_SEQ]         = seq;
  buf[PDU_OFFSET_SRC]         = myId;
  buf[PDU_OFFSET_DST]         = coordId;
  buf[PDU_OFFSET_PAYLOAD + 0] = (uint8_t)(price_cnf >> 8);
  buf[PDU_OFFSET_PAYLOAD + 1] = (uint8_t)(price_cnf & 0xFF);
  buf[PDU_OFFSET_PAYLOAD + 2] = (uint8_t)(loc_cnf >> 8);
  buf[PDU_OFFSET_PAYLOAD + 3] = (uint8_t)(loc_cnf & 0xFF);
  return PDU_HEADER_SIZE + 4;
}

// 수신 버퍼가 어떤 종류의 패킷인지 판별할 때 사용 
uint8_t L3_msg_getPduType(uint8_t* buf) { return buf[PDU_OFFSET_TYPE]; }


// REC 패킷 파싱 
rec_payload_t L3_msg_parseRec(uint8_t* buf) {
  rec_payload_t rec;
  rec.avg_price = ((uint16_t)buf[PDU_OFFSET_PAYLOAD + 0] << 8)
                |             buf[PDU_OFFSET_PAYLOAD + 1];
  rec.avg_loc   = ((uint16_t)buf[PDU_OFFSET_PAYLOAD + 2] << 8)
                |             buf[PDU_OFFSET_PAYLOAD + 3];
  return rec;
}

// MCH 패킷 파싱 
mch_payload_t L3_msg_parseMch(uint8_t* buf) {
  mch_payload_t mch;
  mch.match_success = buf[PDU_OFFSET_PAYLOAD];
  return mch;
}
