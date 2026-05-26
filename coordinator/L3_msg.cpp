#include "L3_msg.h"

#include "mbed.h"

int L3_msg_checkMsgType(uint8_t* msg) { return msg[L3_MSG_OFFSET_TYPE]; }

uint8_t L3_msg_encodeMsg(uint8_t* msg, uint8_t type, uint8_t seq, uint8_t srcId,
                         uint8_t destId, uint8_t* data, int len) {
  msg[L3_MSG_OFFSET_TYPE] = type;
  msg[L3_MSG_OFFSET_SEQ] = seq;
  msg[L3_MSG_OFFSET_SRCID] = srcId;
  msg[L3_MSG_OFFSET_DESTID] = destId;
  memcpy(&msg[L3_MSG_OFFSET_DATA], data, len * sizeof(uint8_t));

  return len + L3_MSG_OFFSET_DATA;
}
uint8_t L3_msg_getSeq(uint8_t* msg) { return msg[L3_MSG_OFFSET_SEQ]; }

uint8_t* L3_msg_getData(uint8_t* msg) { return &msg[L3_MSG_OFFSET_DATA]; }