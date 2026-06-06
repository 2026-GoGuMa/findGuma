#pragma once
#include "mbed.h"

// Coordinator L3 message type
#define L3_MSG_TYPE_MCH       0
#define L3_MSG_TYPE_REC       1
#define L3_MSG_TYPE_WAIT_PAIR 2
// Trader L3 message type
#define L3_MSG_TYPE_TXN       3
#define L3_MSG_TYPE_CNF       4

// L3 message format (coordinator와 동일한 오프셋 이름 사용)
#define L3_MSG_OFFSET_TYPE    0
#define L3_MSG_OFFSET_SEQ     1
#define L3_MSG_OFFSET_SRCID   2
#define L3_MSG_OFFSET_DESTID  3
#define L3_MSG_OFFSET_PAYLOAD 4   // coordinator의 L3_MSG_OFFSET_PAYLOAD와 동일

#define L3_MSG_MAX_SEQNUM  1024

// TXN 페이로드 오프셋 — coordinator의 L3_msg_decodeTxn 기준에 맞춤
// (signal 필드 제거: coordinator가 읽지 않으므로 포맷 일치 필요)
#define L3_TXN_OFFSET_ID       0  // 1바이트
#define L3_TXN_OFFSET_ISSELLER 1  // 1바이트
#define L3_TXN_OFFSET_GOODS    2  // 1바이트
#define L3_TXN_OFFSET_PRICE    3  // 2바이트 (big-endian)

// TXN 패킷 만들기 (Trader → Coordinator)
uint8_t L3_msg_buildTxn(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, uint8_t isSeller,
                         uint8_t goods, uint16_t price);

// CNF 패킷 만들기 (Trader → Coordinator): accept=1(수락) or 0(거절)
uint8_t L3_msg_buildCnf(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, uint8_t accept);

// 수신 버퍼에서 패킷 타입만 꺼내기
uint8_t  L3_msg_getPduType(uint8_t* buf);

// REC 패킷 파싱 — coordinator는 가격 REC와 위치 REC를 각각 2바이트로 따로 전송
uint16_t L3_msg_parseRec(uint8_t* buf);

// MCH 패킷 파싱 — coordinator의 encodeMch와 동일한 1바이트 accept 필드
uint8_t  L3_msg_parseMch(uint8_t* buf);
