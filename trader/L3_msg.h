#pragma once
#include "mbed.h"

// PDU types -------------------------------------------------
#define PDU_TYPE_TXN       0x01  // Trader → Coordinator (거래 정보 전송)
#define PDU_TYPE_CNF       0x02  // Trader → Coordinator (확인(confirm) 응답)
#define PDU_TYPE_WAIT_PAIR 0x03  // Coordinator → Trader (페어링 대기 신호)
#define PDU_TYPE_REC       0x04  // Coordinator → Trader (평균값 전달)
#define PDU_TYPE_MCH       0x05  // Coordinator → Trader (매칭 결과 전달)

// PDU field offsets
#define PDU_OFFSET_TYPE    0
#define PDU_OFFSET_SEQ     1
#define PDU_OFFSET_SRC     2
#define PDU_OFFSET_DST     3
#define PDU_OFFSET_PAYLOAD 4
#define PDU_HEADER_SIZE    4

// REC payload: 코디네이터가 평균값 전달
typedef struct {
  uint16_t avg_price;    // 평균 가격 (2바이트)
  uint16_t avg_loc;      // 평균 위치 (2바이트)
} rec_payload_t;

// MCH payload: 매칭 결과 전달
typedef struct {
  uint8_t match_success;     // 매칭 성공 여부 (1바이트, 0 또는 1)
} mch_payload_t;

// TXN 패킷 만들기 (거래 정보를 바이트 배열로)
uint8_t L3_msg_buildTxn(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint8_t seq, int8_t signal, uint8_t isSeller,
                         uint8_t goods, uint16_t price);

// CNF 패킷 만들기 (확인 응답을 바이트 배열로)
uint8_t L3_msg_buildCnf(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint8_t seq, uint16_t price_cnf, uint16_t loc_cnf);

// 수신 버퍼에서 패킷 타입만 꺼내기 
uint8_t       L3_msg_getPduType(uint8_t* buf);

// REC 패킷에서 avg_price, avg_loc 파싱 
rec_payload_t L3_msg_parseRec(uint8_t* buf);

// MCH 패킷에서 match_success 파싱 
mch_payload_t L3_msg_parseMch(uint8_t* buf);
