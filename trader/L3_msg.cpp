#include "L3_msg.h"

#include "mbed.h"

// ================= 데이터 인코딩 함수들 =================
// 2byte REC Payload(price, loc) 구성
// 8비트 2칸을 합쳐서 16비트로 해석하는 함수
static uint16_t L3_msg_readUint16(uint8_t* dataPtr) {
  return ((uint16_t)dataPtr[0] << 8) | dataPtr[1];
}
// 16비트 데이터를 8비트 2칸으로 나눠서 저장하는 함수
static void L3_msg_writeUint16(uint8_t* dataPtr, uint16_t data) {
  dataPtr[0] = (uint8_t)(data >> 8);
  dataPtr[1] = (uint8_t)(data & 0xFF);
}

// ================= 수신 메시지의 헤더 및 페이로드 참조 함수들
// =================
int L3_msg_checkMsgType(uint8_t* msg) { return msg[L3_MSG_OFFSET_TYPE]; }

uint8_t L3_msg_getSeq(uint8_t* msg) { return msg[L3_MSG_OFFSET_SEQ]; }

uint8_t L3_msg_getSrcId(uint8_t* msg) { return msg[L3_MSG_OFFSET_SRCID]; }

uint8_t L3_msg_getDstId(uint8_t* msg) { return msg[L3_MSG_OFFSET_DESTID]; }

uint8_t* L3_msg_getPayload(uint8_t* msg) { return &msg[L3_MSG_OFFSET_PAYLOAD]; }

// ================= 수신 MSG 타입 체크 함수들 =================
// MCH인지 확인하고 해석하는 함수 (메시지 타입과 크기 체크)
int L3_msg_checkIfMch(uint8_t* msg, uint8_t size) {
  return size >= L3_MSG_MCH_SIZE && msg[L3_MSG_OFFSET_TYPE] == L3_MSG_TYPE_MCH;
}
// REC인지 확인하고 해석하는 함수 (메시지 타입과 크기 체크)
int L3_msg_checkIfRec(uint8_t* msg, uint8_t size) {
  return size >= L3_MSG_REC_SIZE && msg[L3_MSG_OFFSET_TYPE] == L3_MSG_TYPE_REC;
}
// WAIT_PAIR인지 확인하고 해석하는 함수 (메시지 타입과 크기 체크)
int L3_msg_checkIfWaitPair(uint8_t* msg, uint8_t size) {
  return size >= L3_MSG_WAIT_PAIR_SIZE &&
         msg[L3_MSG_OFFSET_TYPE] == L3_MSG_TYPE_WAIT_PAIR;
}

// ================= 송신 MSG 생성 함수들 =================
// 메시지를 바이트 배열로 조립하는 제너럴 함수 (Coord -> Trader)
uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* payload_data, int len) {
  msg[L3_MSG_OFFSET_TYPE] = type;
  msg[L3_MSG_OFFSET_SEQ] = seq;
  msg[L3_MSG_OFFSET_SRCID] = srcId;
  msg[L3_MSG_OFFSET_DESTID] = destId;

  // payload 채우는 로직
  if (payload_data != NULL && len > 0)
    // payload_data가 NULL이 아니고 길이가 0보다 크면 payload에 payload_data
    // 복사 - payload_data=NULL 처리한 경우는 복사를 이미 한 경우
    memcpy(&msg[L3_MSG_OFFSET_PAYLOAD], payload_data, len * sizeof(uint8_t));

  return len + L3_MSG_OFFSET_PAYLOAD;
}
// TXN 메시지 생성 함수
// coordinator: payload[0]=id, [1]=isSeller, [2]=goods, [3:4]=price (5바이트)
uint8_t L3_msg_encodeTxn(uint8_t* msg, uint8_t myId, uint8_t coordId,
                         uint8_t seq, uint8_t isSeller, uint8_t goods,
                         uint16_t price) {
  L3_msg_getPayload(msg)[L3_TXN_OFFSET_ID] = myId;  // trader
  L3_msg_getPayload(msg)[L3_TXN_OFFSET_ISSELLER] = isSeller;
  L3_msg_getPayload(msg)[L3_TXN_OFFSET_GOODS] = goods;
  L3_msg_writeUint16(&L3_msg_getPayload(msg)[L3_TXN_OFFSET_PRICE], price);
  return L3_msg_encodeMsg(msg, L3_MSG_TYPE_TXN, seq, myId, coordId,
                          L3_msg_getPayload(msg), 5);
}
// CNF 패킷 생성 함수
// coordinator의 L3_CNF_OFFSET_ACCPT(=0) 위치에 accept 1바이트
// coordinator 기대값: payload[0] == 1 이면 수락, 0 이면 거절
uint8_t L3_msg_encodeCnf(uint8_t* msg, uint8_t myId, uint8_t coordId,
                         uint8_t seq, uint8_t accept) {
  L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT] = accept;
  return L3_msg_encodeMsg(msg, L3_MSG_TYPE_CNF, seq, myId, coordId,
                          L3_msg_getPayload(msg), 1);
}

// ================= 수신 MSG 해석 함수들 =================
// coordinator에게서 수신받은 메시지에서 꺼낸 payload를 해석하는 함수
uint16_t L3_msg_decodeRec(uint8_t* msg) {
  return ((uint16_t)msg[L3_MSG_OFFSET_PAYLOAD] << 8) |
         msg[L3_MSG_OFFSET_PAYLOAD + 1];
}

uint8_t L3_msg_decodeMch(uint8_t* msg) { return msg[L3_MSG_OFFSET_PAYLOAD]; }
