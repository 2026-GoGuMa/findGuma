#include "mbed.h"

// Coordinator L3 message type
#define L3_MSG_TYPE_MCH 0
#define L3_MSG_TYPE_REC 1
#define L3_MSG_TYPE_WAIT_PAIR 2
// Trader L3 message type
#define L3_MSG_TYPE_TXN 3
#define L3_MSG_TYPE_CNF 4

// L3 message format
#define L3_MSG_OFFSET_TYPE 0  // 0번째 칸: 패킷 타입 (MCH, TXN 등)
#define L3_MSG_OFFSET_SEQ 1   // 1번째 칸: 시퀀스 넘버 (SEQ NUM)
#define L3_MSG_OFFSET_SRCID 2
#define L3_MSG_OFFSET_DESTID 3
#define L3_MSG_OFFSET_DATA 4  // 4번째 칸: 실제 데이터 내용(Payload)

// TXN payload offset
#define L3_TXN_OFFSET_ID 0        // 1 byte
#define L3_TXN_OFFSET_ISSELLER 1  // 1 byte
#define L3_TXN_OFFSET_GOODS 2     // 1 byte
#define L3_TXN_OFFSET_PRICE 3     // 2 bytes
#define L3_TXN_PAYLOAD_SIZE 5

#define L3_MSG_WAIT_PAIR_SIZE L3_MSG_OFFSET_DATA
#define L3_MSG_TXN_SIZE (L3_MSG_OFFSET_DATA + L3_TXN_PAYLOAD_SIZE)
#define L3_MSG_CNF_SIZE 6
#define L3_MSG_MAXDATASIZE 1024
#define L3_MSG_MAX_SEQNUM 1024

typedef struct L3_txnInfo {
  uint8_t id;
  int16_t signal;
  uint8_t isSeller;
  uint8_t goods;
  uint16_t price;
} L3_txnInfo_t;

int L3_msg_checkMsgType(uint8_t* msg);
uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* data, int len);
uint8_t L3_msg_getSeq(uint8_t* msg);
uint8_t* L3_msg_getData(uint8_t* msg);

int L3_msg_checkIfTxn(uint8_t* msg, uint8_t size);
int L3_msg_checkIfCnf(uint8_t* msg, uint8_t size);
uint8_t L3_msg_encodeWaitPair(uint8_t* msg, uint8_t seq, uint8_t srcId,
                              uint8_t dstId);
int L3_msg_decodeTxn(uint8_t* msg, uint8_t size, L3_txnInfo_t* txnInfo);
