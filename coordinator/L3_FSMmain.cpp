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
static uint8_t hasPendingTxn = 0;

void L3_initFSM(void) {
}

static uint8_t L3_signalConditionPassed(int16_t rssi) {
  return rssi >= L3_MIN_RSSI;
}

static uint8_t L3_pairConditionPassed(L3_txnInfo_t* firstTxn,
                                      L3_txnInfo_t* secondTxn) {
  return L3_signalConditionPassed(firstTxn->signal) &&
         L3_signalConditionPassed(secondTxn->signal) &&
         firstTxn->isSeller != secondTxn->isSeller &&
         firstTxn->goods == secondTxn->goods;
}

static void L3_resetPendingTxn(void) {
  memset(&pendingTxn, 0, sizeof(L3_txnInfo_t));
  hasPendingTxn = 0;
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
  hasPendingTxn = 1;

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
      // WAIT_PAIR 상태에서는 다음 3가지를 기다린다.
      // 1) 또 다른 TXN이 와서 pair 조건을 만족하는지
      // 2) 예상 밖의 CNF / unknown PDU가 들어오는지
      // 3) pair timeout이 발생하는지
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        // 하위 계층에서 새로운 PDU가 도착했으므로, 내용을 읽어서
        // TXN / CNF / unknown 여부를 다시 판별한다.
        uint8_t* dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        L3_txnInfo_t txnInfo;

        int16_t rssi = L3_LLI_getRssi();

        if (L3_msg_decodeTxn(dataPtr, size, &txnInfo)) {
          // TXN으로 해석 가능하면 상대 trader의 거래 정보라고 보고,
          // 수신 신호 세기까지 포함해서 pair 가능 여부를 판단한다.
          txnInfo.signal = rssi;

          if (!L3_signalConditionPassed(txnInfo.signal)) {
            // 신호가 너무 약하면 이 TXN은 매칭 후보로 보지 않는다.
            debug_if(DBGMSG_L3,
                     "[L3] TXN ignored in WAIT_PAIR, RSSI %i is lower than minimum %i\n",
                     rssi, L3_MIN_RSSI);
          } else if (!hasPendingTxn) {
            // 원래 대기 중이어야 할 첫 TXN이 없다면 상태가 꼬인 것이므로
            // 안전하게 IDLE로 되돌린다.
            debug_if(DBGMSG_L3,
                     "[L3][WARNING] WAIT_PAIR has no pending TXN, reset to IDLE\n");
            main_state = L3STATE_IDLE;
          } else if (txnInfo.id == pendingTxn.id) {
            // 같은 trader가 같은 요청을 다시 보낸 경우는 중복으로 보고 무시한다.
            debug_if(DBGMSG_L3,
                     "[L3] duplicated TXN from trader %i ignored in WAIT_PAIR\n",
                     txnInfo.id);
          } else if (L3_pairConditionPassed(&pendingTxn, &txnInfo)) {
            // seller/buyer 조합, goods 일치, RSSI 조건까지 모두 통과하면
            // pair 후보가 성립한 것으로 보고 타이머를 멈춘다.
            L3_timer_stopTimer();

            debug_if(DBGMSG_L3,
                     "[L3] pair candidate found: trader %i <-> trader %i\n",
                     pendingTxn.id, txnInfo.id);

            // 현재 구현에서는 pair 성립 후 다음 단계 로직이 아직 없으므로,
            // 보관 중이던 TXN을 정리하고 IDLE로 복귀한다.
            L3_resetPendingTxn();
            main_state = L3STATE_IDLE;
          } else {
            // TXN이긴 하지만 seller/buyer 방향 또는 goods 조건이 맞지 않으면
            // 현재 pending 거래와는 pair로 묶지 않고 그냥 무시한다.
            debug_if(DBGMSG_L3,
                     "[L3] TXN from trader %i does not match pending trader %i\n",
                     txnInfo.id, pendingTxn.id);
          }
        } else if (L3_msg_checkIfCnf(dataPtr, size)) {
          // WAIT_PAIR 상태에서는 CNF가 먼저 오는 시나리오를 정상 흐름으로 보지 않는다.
          // 로그만 남기고 상태는 유지한다.
          debug_if(DBGMSG_L3, "[L3] CNF ignored in WAIT_PAIR state\n");
        } else {
          // 타입을 판별할 수 없는 PDU는 잘못된 메시지로 보고 무시한다.
          debug_if(DBGMSG_L3, "[L3] unknown PDU ignored in WAIT_PAIR state\n");
        }

        // msgRcvd 이벤트는 이번 수신 패킷에 대한 처리가 끝났으므로 지운다.
        L3_event_clearEventFlag(L3_event_msgRcvd);
      } else if (L3_event_checkEventFlag(L3_event_dataSendCnf)) {
        // WAIT_PAIR 상태에서는 우리가 새로 보내는 데이터가 거의 없지만,
        // 하위 계층 완료 이벤트가 들어오면 일단 소비만 하고 넘어간다.
        L3_event_clearEventFlag(L3_event_dataSendCnf);
      } else if (!L3_timer_getTimerStatus()) {
        // 타이머가 종료되면 pair 대기 실패로 보고 pending 정보를 정리한 뒤
        // 다시 처음 상태로 돌아간다.
        debug_if(DBGMSG_L3,
                 "[L3] WAIT_PAIR timeout for trader %i, reset to IDLE\n",
                 pendingTxn.id);

        L3_resetPendingTxn();
        main_state = L3STATE_IDLE;
      }
      break;

    default:
      break;
  }
}
