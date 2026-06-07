// #pragma once
#include "mbed.h"

// Coordinator L3 message type
#define L3_MSG_TYPE_MCH 0
#define L3_MSG_TYPE_REC 1
#define L3_MSG_TYPE_WAIT_PAIR 2
// Trader L3 message type
#define L3_MSG_TYPE_TXN 3
#define L3_MSG_TYPE_CNF 4

// L3 message format (coordinator와 동일한 오프셋 이름 사용)
#define L3_MSG_OFFSET_TYPE 0
#define L3_MSG_OFFSET_SEQ 1
#define L3_MSG_OFFSET_SRCID 2
#define L3_MSG_OFFSET_DESTID 3
#define L3_MSG_OFFSET_PAYLOAD 4  // coordinator의 L3_MSG_OFFSET_PAYLOAD와 동일

// TXN 페이로드 오프셋 — coordinator의 L3_msg_decodeTxn 기준에 맞춤
// (signal 필드 제거: coordinator가 읽지 않으므로 포맷 일치 필요)
#define L3_TXN_OFFSET_ID 0        // 1바이트
#define L3_TXN_OFFSET_ISSELLER 1  // 1바이트
#define L3_TXN_OFFSET_GOODS 2     // 1바이트
#define L3_TXN_OFFSET_PRICE 3     // 2바이트 (big-endian)

// CNF payload offset
#define L3_CNF_OFFSET_ACCPT 0  // 1 byte (0: reject, 1: accept)

// MCH payload offset
#define L3_MCH_OFFSET_ACCPT 0  // 1 byte (0: reject, 1: accept)

// REC payload offset
#define L3_REC_OFFSET_INF \
  0  // 2 bytes (price인 경우 0~6만까지, loc인 경우 우편번호로 010**~636**까지
     // 표현 가능)

// L3 Message Size (페이로드 크기)
#define L3_TXN_PAYLOAD_SIZE 5

// pdu 전체 = 헤더 + 페이로드 크기
#define L3_MSG_MCH_SIZE (L3_MSG_OFFSET_PAYLOAD + 1)
#define L3_MSG_REC_SIZE (L3_MSG_OFFSET_PAYLOAD + 2)
#define L3_MSG_WAIT_PAIR_SIZE (L3_MSG_OFFSET_PAYLOAD + 0)
#define L3_MSG_TXN_SIZE (L3_MSG_OFFSET_PAYLOAD + L3_TXN_PAYLOAD_SIZE)
#define L3_MSG_CNF_SIZE (L3_MSG_OFFSET_PAYLOAD + 1)

#define L3_MSG_MAX_SEQNUM 1024

// TXN 메시지의 내용을 담는 구조체
typedef struct L3_txnInfo {
  uint8_t id;
  uint8_t isSeller;
  uint8_t goods;
  uint16_t price;
  int16_t signal;
} L3_txnInfo_t;

// ================= 데이터 인코딩 함수들 =================
static uint16_t L3_msg_readUint16(uint8_t* dataPtr);
static void L3_msg_writeUint16(uint8_t* dataPtr, uint16_t data);

// ================= 수신 메시지의 헤더 및 페이로드 참조 함수들
// =================
int L3_msg_checkMsgType(uint8_t* msg);
uint8_t L3_msg_getSeq(uint8_t* msg);
uint8_t L3_msg_getSrcId(uint8_t* msg);
uint8_t L3_msg_getDstId(uint8_t* msg);
uint8_t* L3_msg_getPayload(uint8_t* msg);

// ================= 수신 MSG 타입 체크 함수들 =================
int L3_msg_checkIfMch(uint8_t* msg, uint8_t size);
int L3_msg_checkIfRec(uint8_t* msg, uint8_t size);
int L3_msg_checkIfWaitPair(uint8_t* msg, uint8_t size);

// ================= 송신 MSG 생성 함수들 =================
uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* data, int len);
uint8_t L3_msg_encodeTxn(uint8_t* buf, uint8_t myId, uint8_t coordId,
                         uint16_t seq, uint8_t isSeller, uint8_t goods,
                         uint16_t price);
uint8_t L3_msg_encodeCnf(uint8_t* msg, uint8_t myId, uint8_t coordId,
                         uint8_t seq, uint8_t accept);

// ================= 수신 MSG 해석 함수들 =================
uint16_t L3_msg_decodeRec(uint8_t* msg);
uint8_t L3_msg_decodeMch(uint8_t* msg);