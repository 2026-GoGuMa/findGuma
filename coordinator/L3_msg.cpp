#include "L3_msg.h"

#include "mbed.h"

static uint16_t L3_msg_readUint16(uint8_t* dataPtr) {
  return ((uint16_t)dataPtr[0] << 8) | dataPtr[1];
}

int L3_msg_checkMsgType(uint8_t* msg) { return msg[L3_MSG_OFFSET_TYPE]; }

uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* data, int len) {
  msg[L3_MSG_OFFSET_TYPE] = type;
  msg[L3_MSG_OFFSET_SEQ] = seq;
  msg[L3_MSG_OFFSET_SRCID] = srcId;
  msg[L3_MSG_OFFSET_DESTID] = destId;
  if (len > 0) memcpy(&msg[L3_MSG_OFFSET_DATA], data, len * sizeof(uint8_t));

  return len + L3_MSG_OFFSET_DATA;
}

uint8_t L3_msg_getSeq(uint8_t* msg) { return msg[L3_MSG_OFFSET_SEQ]; }

uint8_t* L3_msg_getData(uint8_t* msg) { return &msg[L3_MSG_OFFSET_DATA]; }

// TXN인지 확인하고 해석하는 함수 (메시지 타입과 크기 체크 후, 구조체에 데이터 채워넣음)
int L3_msg_checkIfTxn(uint8_t* msg, uint8_t size) {
  return size >= L3_MSG_TXN_SIZE && msg[L3_MSG_OFFSET_TYPE] == L3_MSG_TYPE_TXN;
}

int L3_msg_checkIfCnf(uint8_t* msg, uint8_t size) {
  return size >= L3_MSG_CNF_SIZE && msg[L3_MSG_OFFSET_TYPE] == L3_MSG_TYPE_CNF;
}

uint8_t L3_msg_getSrcId(uint8_t* msg) { return msg[L3_MSG_OFFSET_SRCID]; }

uint8_t L3_msg_getDstId(uint8_t* msg) { return msg[L3_MSG_OFFSET_DESTID]; }

// Coord이 Trader에게 보낼 WAIT_PAIR 메시지 생성 함수 
uint8_t L3_msg_encodeWaitPair(uint8_t* msg, uint8_t seq, uint8_t srcId,
                              uint8_t dstId) {
  return L3_msg_encodeMsg(msg, L3_MSG_TYPE_WAIT_PAIR, seq, srcId, dstId, 0, 0);
}

// pdu에서 데이터 꺼내는 함수 
int L3_msg_decodeTxn(uint8_t* msg, uint8_t size, L3_txnInfo_t* txnInfo) {
  if (!L3_msg_checkIfTxn(msg, size)) return 0;

  uint8_t* data = L3_msg_getData(msg);
  txnInfo->id = data[L3_TXN_OFFSET_ID];
  txnInfo->isSeller = data[L3_TXN_OFFSET_ISSELLER];
  txnInfo->goods = data[L3_TXN_OFFSET_GOODS];
  txnInfo->price = L3_msg_readUint16(&data[L3_TXN_OFFSET_PRICE]);

  return 1;
}
