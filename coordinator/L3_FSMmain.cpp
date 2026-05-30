#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"

// FSM state -------------------------------------------------
#define L3STATE_IDLE 0
#define L3STATE_WAIT_PAIR 1

// state variables
static uint8_t main_state = L3STATE_IDLE;
static uint8_t prev_state = main_state;

static uint8_t L3SeqNum = 1;
static L3_txnInfo_t pendingTxn;

void L3_initFSM(void) {
}

static uint8_t L3_signalConditionPassed(int16_t rssi) {
  return rssi >= L3_MIN_RSSI;
}

static uint8_t L3_getNextSeqNum(void) {
  uint8_t seqNum = L3SeqNum;
  L3SeqNum = (L3SeqNum + 1) % L3_MSG_MAX_SEQNUM;
  if (L3SeqNum == 0) L3SeqNum = 1;
  return seqNum;
}

// action 1: TXN 메시지 내용을 pendingTxn에 저장하는 함수
static void L3_storeTxn(L3_txnInfo_t* txnInfo) {
  pendingTxn = *txnInfo;

  debug_if(DBGMSG_L3,
           "[L3] stored TXN id:%i signal:%i seller:%i goods:%i price:%i\n",
           pendingTxn.id, pendingTxn.signal, pendingTxn.isSeller,
           pendingTxn.goods, pendingTxn.price);
}

// action 5: Trader에게 WAIT_PAIR 메시지 보내는 함수
static void L3_sendWaitPair(uint8_t traderId) {
  uint8_t waitPair[L3_MSG_WAIT_PAIR_SIZE];
  uint8_t pduSize =
      L3_msg_encodeWaitPair(waitPair, L3_getNextSeqNum(), L3_COORDINATOR_ID,
                            traderId);

  L3_LLI_dataReqFunc(waitPair, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] WAIT_PAIR sent to trader %i\n", traderId);
}

void L3_FSMrun(void) {
  if (prev_state != main_state) {
    debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state,
             main_state);
    prev_state = main_state;
  }

  switch (main_state) {
    case L3STATE_IDLE:
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t* dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        L3_txnInfo_t txnInfo;

        int16_t rssi = L3_LLI_getRssi();

        // 메시지가 TXN이라면 내용을 꺼내서 TXNinfo에 담음
        if (L3_msg_decodeTxn(dataPtr, size, &txnInfo)) {
          txnInfo.signal = rssi;
          if (L3_signalConditionPassed(txnInfo.signal)) {
            L3_storeTxn(&txnInfo);
            L3_sendWaitPair(txnInfo.id);
            L3_timer_startTimer();
            main_state = L3STATE_WAIT_PAIR;
          } else {
            debug_if(DBGMSG_L3,
                     "[L3] TXN ignored, RSSI %i is lower than minimum %i\n",
                     rssi, L3_MIN_RSSI);
          }
        } else if (L3_msg_checkIfCnf(dataPtr, size)) {
          debug_if(DBGMSG_L3, "[L3] CNF ignored in IDLE state\n");
        } else {
          debug_if(DBGMSG_L3, "[L3] unknown PDU ignored in IDLE state\n");
        }

        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      break;

    case L3STATE_WAIT_PAIR:
      if (L3_event_checkEventFlag(L3_event_dataSendCnf)) {
        L3_event_clearEventFlag(L3_event_dataSendCnf);
      }
      break;

    default:
      break;
  }
}
