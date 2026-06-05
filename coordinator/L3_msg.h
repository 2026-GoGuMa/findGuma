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
#define L3_MSG_OFFSET_PAYLOAD 4  // 4번째 칸: 실제 데이터 내용(Payload)

// L3 message payload offsets
// TXN payload offset
#define L3_TXN_OFFSET_ID 0        // 1 byte
#define L3_TXN_OFFSET_ISSELLER 1  // 1 byte
#define L3_TXN_OFFSET_GOODS 2     // 1 byte
#define L3_TXN_OFFSET_PRICE 3     // 2 bytes

// CNF payload offset
#define L3_CNF_OFFSET_ACCPT 0  // 1 byte (0: reject, 1: accept)

// MCH payload offset
#define L3_MCH_OFFSET_ACCPT 0  // 1 byte (0: reject, 1: accept)

// REC payload offset
#define L3_REC_OFFSET_INF \
  0  // 2 bytes (price인 경우 0~6만까지, loc인 경우 우편번호로 010**~636**까지
     // 표현 가능)

// L3 Message Size
#define L3_TXN_PAYLOAD_SIZE 5

#define L3_MSG_MCH_SIZE (L3_MSG_OFFSET_PAYLOAD + 1)
#define L3_MSG_REC_SIZE (L3_MSG_OFFSET_PAYLOAD + 2)
#define L3_MSG_WAIT_PAIR_SIZE (L3_MSG_OFFSET_PAYLOAD + 0)
#define L3_MSG_TXN_SIZE (L3_MSG_OFFSET_PAYLOAD + L3_TXN_PAYLOAD_SIZE)
#define L3_MSG_CNF_SIZE (L3_MSG_OFFSET_PAYLOAD + 1)

#define L3_MSG_MAX_SEQNUM 1024

typedef struct L3_txnInfo {
  uint8_t id;
  uint8_t isSeller;
  uint8_t goods;
  uint16_t price;
} L3_txnInfo_t;

static uint16_t L3_msg_readUint16(uint8_t* dataPtr);
static void L3_msg_writeUint16(uint8_t* dataPtr, uint16_t data);

int L3_msg_checkMsgType(uint8_t* msg);
uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* data, int len);
uint8_t L3_msg_getSeq(uint8_t* msg);
uint8_t* L3_msg_getPayload(uint8_t* msg);
uint8_t L3_msg_getSrcId(uint8_t* msg);
uint8_t L3_msg_getDstId(uint8_t* msg);

int L3_msg_checkIfTxn(uint8_t* msg, uint8_t size);
int L3_msg_checkIfCnf(uint8_t* msg, uint8_t size);
uint8_t L3_msg_encodeWaitPair(uint8_t* msg, uint8_t seq, uint8_t srcId,
                              uint8_t dstId);
uint8_t L3_msg_encodeMch(uint8_t* msg, uint8_t seq, uint8_t srcId,
                         uint8_t dstId, uint8_t accept);
uint8_t L3_msg_encodeRec(uint8_t* msg, uint8_t seq, uint8_t srcId,
                         uint8_t dstId, uint16_t info);

int L3_msg_decodeTxn(uint8_t* msg, uint8_t size, L3_txnInfo_t* txnInfo);
