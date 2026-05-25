#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"

// FSM state -------------------------------------------------
#define L3STATE_IDLE 0
#define L3STATE_WAIT_PAIR 1

#define L3_COORDINATOR_ID 0
#define L3_MIN_RSSI -60 // 임의 설정 

// state variables
static uint8_t main_state = L3STATE_IDLE;  // protocol state
static uint8_t prev_state = main_state;

// SDU (input)
static uint8_t originalWord[1030];
static uint8_t wordLen = 0;

static uint8_t sdu[1030];
static uint8_t l3SeqNum = 0;
static L3_txnInfo_t pendingTxn;

// serial port interface
static Serial pc(USBTX, USBRX);
static uint8_t myDestId;

// application event handler : generating SDU from keyboard input
static void L3service_processInputWord(void) {
  char c = pc.getc();
  if (!L3_event_checkEventFlag(L3_event_dataToSend)) {
    if (c == '\n' || c == '\r') {
      originalWord[wordLen++] = '\0';
      L3_event_setEventFlag(L3_event_dataToSend);
      debug_if(DBGMSG_L3, "word is ready! ::: %s\n", originalWord);
    } else {
      originalWord[wordLen++] = c;
      if (wordLen >= L3_MAXDATASIZE - 1) {
        originalWord[wordLen++] = '\0';
        L3_event_setEventFlag(L3_event_dataToSend);
        pc.printf("\n max reached! word forced to be ready :::: %s\n",
                  originalWord);
      }
    }
  }
}

void L3_initFSM(uint8_t destId) {
  myDestId = destId;
  // initialize service layer
  pc.attach(&L3service_processInputWord, Serial::RxIrq);

  pc.printf("Give a word to send : ");
}

static uint8_t L3_signalConditionPassed(int16_t rssi) {
  return rssi >= L3_MIN_RSSI;
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
  uint8_t pduSize = L3_msg_encodeWaitPair(waitPair, l3SeqNum++,
                                          L3_COORDINATOR_ID, traderId);

  L3_LLI_dataReqFunc(waitPair, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] WAIT_PAIR sent to trader %i\n", traderId);
}

void L3_FSMrun(void) {
  if (prev_state != main_state) {
    debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state,
             main_state);
    prev_state = main_state;
  }

  // FSM should be implemented here! ---->>>>
  switch (main_state) {
    case L3STATE_IDLE:  // IDLE state description

      if (L3_event_checkEventFlag(
              L3_event_msgRcvd))  // if data reception event happens
      {
        // Retrieving data info.
        uint8_t* dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        L3_txnInfo_t txnInfo;

        int16_t rssi = L3_LLI_getRssi();

        // 메시지가 TXN이라면 내용을 꺼내서 TXNinfo에 담음 
        if (L3_msg_decodeTxn(dataPtr, size, &txnInfo)) {
          // TXN 메시지 처리 : RSSI 조건 확인 -> 조건 통과 시 TXN 저장, WAIT_PAIR 송신, 타이머 시작, 상태 전이
          if (L3_signalConditionPassed(rssi)) {
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

        pc.printf("Give a word to send : ");

        L3_event_clearEventFlag(L3_event_msgRcvd);
      } else if (L3_event_checkEventFlag(
                     L3_event_dataToSend))  // if data needs to be sent
                                            // (keyboard input)
      {
        // msg header setting
        strcpy((char*)sdu, (char*)originalWord);
        debug("[L3] msg length : %i\n", wordLen);
        L3_LLI_dataReqFunc(sdu, wordLen, myDestId);

        debug_if(DBGMSG_L3, "[L3] sending msg....\n");
        wordLen = 0;

        pc.printf("Give a word to send : ");

        L3_event_clearEventFlag(L3_event_dataToSend);
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
