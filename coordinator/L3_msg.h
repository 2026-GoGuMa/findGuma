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

#define L3_MSG_MAXDATASIZE 1024
#define L3_MSG_MAX_SEQNUM 1024

// TXN 페이로드 오프셋 (L3_MSG_OFFSET_DATA 기준)
#define L3_TXN_OFFSET_ID       0  // 1바이트
#define L3_TXN_OFFSET_SIGNAL   1  // 1바이트
#define L3_TXN_OFFSET_ISSELLER 2  // 1바이트
#define L3_TXN_OFFSET_GOODS    3  // 1바이트
#define L3_TXN_OFFSET_PRICE    4  // 2바이트 (4번, 5번)

int L3_msg_checkMsgType(uint8_t* msg);
uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* data, int len);
uint8_t L3_msg_getSeq(uint8_t* msg);
uint8_t* L3_msg_getData(uint8_t* msg);