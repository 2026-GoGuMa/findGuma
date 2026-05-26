#pragma once
#include "mbed.h"

// Coordinator L3 message type
#define L3_MSG_TYPE_MCH       0
#define L3_MSG_TYPE_REC       1
#define L3_MSG_TYPE_WAIT_PAIR 2
// Trader L3 message type
#define L3_MSG_TYPE_TXN       3
#define L3_MSG_TYPE_CNF       4

// L3 message format
#define L3_MSG_OFFSET_TYPE   0  // 0번째 칸: 패킷 타입 (MCH, TXN 등)
#define L3_MSG_OFFSET_SEQ    1  // 1번째 칸: 시퀀스 넘버
#define L3_MSG_OFFSET_SRCID  2
#define L3_MSG_OFFSET_DESTID 3
#define L3_MSG_OFFSET_DATA   4  // 4번째 칸: 실제 데이터 내용(Payload)

#define L3_MSG_MAXDATASIZE  1024
#define L3_MSG_MAX_SEQNUM   1024

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
