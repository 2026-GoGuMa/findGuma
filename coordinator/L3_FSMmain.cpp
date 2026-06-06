#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"

// FSM state -------------------------------------------------
#define L3STATE_IDLE 0
#define L3STATE_WAIT_PAIR 1
#define L3STATE_WAIT_PRICE_CNF 2
#define L3STATE_WAIT_LOC_CNF 3

// state variables
static uint8_t main_state = L3STATE_IDLE;
static uint8_t prev_state = main_state;

// FSM context variables
static uint8_t L3SeqNum = 1;
static L3_txnInfo_t pendingTxn;
static uint8_t hasPendingTxn = 0;
static L3_txnInfo_t matchingTxn;  // A와 매칭되는 B의 txn 정보

#define L3_MAX_RETRY 3
static uint8_t rtylst[256] = {0}; // trader 조합(id_A ^ id_B)에 대한 매칭 시도 횟수

static uint16_t avg_price = 0;  // A와 B의 가격 평균, 임의 초기값 0

static bool cnf_p_rcvd = false;
static bool cnf_m_rcvd = false;

static bool cnf_p_accpt = false;  // pendingTxn으로부터 받은 cnf
static bool cnf_m_accpt = false;  // matchingTxn으로부터 받은 cnf

// serial port interface
static Serial pc(USBTX, USBRX);

// c1. 신호 조건 검사 함수 (RSSI 기반)
static uint8_t L3_signalConditionPassed(int16_t rssi) {
  return rssi >= L3_MIN_RSSI;
}

// c2. pair 조건 검사 함수 (seller/buyer, goods 일치, 신호 세기)
static uint8_t L3_pairConditionPassed(L3_txnInfo_t* firstTxn,
                                      L3_txnInfo_t* secondTxn) {
  return L3_signalConditionPassed(firstTxn->signal) &&
         L3_signalConditionPassed(secondTxn->signal) &&
         firstTxn->isSeller != secondTxn->isSeller &&
         firstTxn->goods == secondTxn->goods;
}

// 시퀀스 번호 생성 함수
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

  debug_if(DBGMSG_L3, "[L3] stored TXN id:%i seller:%i goods:%i price:%i\n",
           pendingTxn.id, pendingTxn.isSeller, pendingTxn.goods,
           pendingTxn.price);
}

// action 3-1: Trader에게 PRICE REC 메시지 보내는 함수
static void L3_sendRecPrice(uint8_t traderId, uint16_t avg_price) {
  uint8_t rec[L3_MSG_REC_SIZE];
  uint8_t pduSize = L3_msg_encodeRec(rec, L3_getNextSeqNum(), L3_COORDINATOR_ID,
                                     traderId, avg_price);
  L3_LLI_dataReqFunc(rec, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] PRICE REC sent to trader %i with price %i\n",
           traderId, avg_price);
}

// action 3-2: Trader에게 LOC REC 메시지 보내는 함수
static void L3_sendRecLoc(uint8_t traderId) {
  uint8_t rec[L3_MSG_REC_SIZE];
  uint16_t avg_loc = AVG_LOC;
  uint8_t pduSize = L3_msg_encodeRec(rec, L3_getNextSeqNum(), L3_COORDINATOR_ID,
                                     traderId, avg_loc);
  L3_LLI_dataReqFunc(rec, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] LOC REC sent to trader %i with location %05i\n",
           traderId, avg_loc);
}

// action 4: Trader에게 MCH 메시지 보내는 함수
static void L3_sendMch(uint8_t traderId, uint8_t accept) {
  uint8_t mch[L3_MSG_MCH_SIZE];
  uint8_t pduSize = L3_msg_encodeMch(mch, L3_getNextSeqNum(), L3_COORDINATOR_ID,
                                     traderId, accept);
  L3_LLI_dataReqFunc(mch, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] MCH sent to trader %i with accept %i\n", traderId,
           accept);
}

// action 5: Trader에게 WAIT_PAIR 메시지 보내는 함수
static void L3_sendWaitPair(uint8_t traderId) {
  uint8_t waitPair[L3_MSG_WAIT_PAIR_SIZE];
  uint8_t pduSize = L3_msg_encodeWaitPair(waitPair, L3_getNextSeqNum(),
                                          L3_COORDINATOR_ID, traderId);
  L3_LLI_dataReqFunc(waitPair, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] WAIT_PAIR sent to trader %i\n", traderId);
}

// action 8. 재시도 리스트를 제외한 모든 정보 리셋
void L3_resetAll() {
  cnf_p_rcvd = false;
  cnf_m_rcvd = false;
  cnf_p_accpt = false;
  cnf_m_accpt = false;
  memset(&pendingTxn, 0, sizeof(L3_txnInfo_t));
  memset(&matchingTxn, 0, sizeof(L3_txnInfo_t));
  L3_timer_stopTimer();
  L3_event_clearAllEventFlag();
}

void L3_initFSM() { L3_resetAll(); }

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
        if (L3_msg_decodeTxn(dataPtr, size, &txnInfo, rssi)) {
          if (L3_signalConditionPassed(txnInfo.signal)) {
            L3_storeTxn(&txnInfo);
            L3_sendWaitPair(txnInfo.id);
            L3_timer_startTimer();
            main_state = L3STATE_WAIT_PAIR;
          } else {
            debug_if(DBGMSG_L3,
                     "[L3] TXN ignored, RSSI %i is lower than minimum %i\n",
                     txnInfo.signal, L3_MIN_RSSI);
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
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        // 하위 계층에서 새로운 PDU가 도착했으므로, 내용을 읽어서
        // TXN / CNF / unknown 여부를 다시 판별한다.
        uint8_t* dataPtr = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();
        L3_txnInfo_t txnInfo;

        int16_t rssi = L3_LLI_getRssi();

        if (L3_msg_decodeTxn(dataPtr, size, &txnInfo, rssi)) {
          // TXN으로 해석 가능하면 상대 trader의 거래 정보라고 보고,
          // 수신 신호 세기까지 포함해서 pair 가능 여부를 판단한다.
          if (!L3_signalConditionPassed(txnInfo.signal)) {
            // 신호가 너무 약하면 이 TXN은 매칭 후보로 보지 않는다.
            debug_if(DBGMSG_L3,
                     "[L3] TXN ignored in WAIT_PAIR, RSSI %i is lower than "
                     "minimum %i\n",
                     txnInfo.signal, L3_MIN_RSSI);
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
            // seller/buyer 조합, goods 일치, RSSI 조건까지 모두 통과 (조건 c2 만족)
            L3_timer_stopTimer();

            debug_if(DBGMSG_L3,
                     "[L3] pair candidate found: trader %i <-> trader %i\n",
                     pendingTxn.id, txnInfo.id);

            // 두 Trader 간의 재시도 횟수 확인 (조건 c4, c5)
            uint8_t pair_key = pendingTxn.id ^ txnInfo.id;
            
            if (rtylst[pair_key] < L3_MAX_RETRY) { // 조건 c4 만족
              if (rtylst[pair_key] == 0) {
                // 조건 c5 만족: 최초 매칭 시 재시도 리스트 업데이트 (action 2)
                rtylst[pair_key] = 1;
              } else {
                rtylst[pair_key]++;
              }
              
              // action 3: Send REC
              avg_price = (pendingTxn.price + txnInfo.price) / 2;
              L3_sendRecPrice(pendingTxn.id, avg_price);
              L3_sendRecPrice(txnInfo.id, avg_price);
              
              // action 6 & 7: 타이머 재시작 (앞서 stop했으므로 다시 start)
              L3_timer_startTimer();
              
              matchingTxn = txnInfo;
              main_state = L3STATE_WAIT_PRICE_CNF;
            } else {
              // 재시도 횟수 초과 시 페어링 실패로 간주하고 IDLE 복귀
              debug_if(DBGMSG_L3,
                       "[L3] retry count exceeded (%i) for trader %i and %i\n",
                       rtylst[pair_key], pendingTxn.id, txnInfo.id);
              L3_resetAll();
              main_state = L3STATE_IDLE;
            }
          } else {
            // TXN이긴 하지만 조건(c2)이 맞지 않으면 (!c2) 아무 액션 없이 IDLE 복귀
            debug_if(DBGMSG_L3,
                     "[L3] TXN does not meet pair conditions (!c2), reset to IDLE\n");
            L3_resetAll();
            main_state = L3STATE_IDLE;
          }
        } else if (L3_msg_checkIfCnf(dataPtr, size)) {
          // WAIT_PAIR 상태에서 CNF가 오는 것(이벤트 B)은 비정상이므로 IDLE 복귀
          debug_if(DBGMSG_L3, "[L3] CNF received in WAIT_PAIR state, reset to IDLE\n");
          L3_resetAll();
          main_state = L3STATE_IDLE;
        } else {
          // 타입을 판별할 수 없는 PDU는 잘못된 메시지로 보고 무시한다.
          debug_if(DBGMSG_L3, "[L3] unknown PDU ignored in WAIT_PAIR state\n");
        }

        // msgRcvd 이벤트는 이번 수신 패킷에 대한 처리가 끝났으므로 지운다.
        L3_event_clearEventFlag(L3_event_msgRcvd);
      } else if (L3_event_checkEventFlag(L3_event_dataSendCnf)) {
        // 하위 계층 완료 이벤트가 들어오면 일단 소비만 하고 넘어간다.
        L3_event_clearEventFlag(L3_event_dataSendCnf);
      } else if (L3_event_checkEventFlag(L3_event_timeout)) {
        // 이벤트 D: CNF 송신 timeout 등 이벤트 기반 타임아웃 발생 시 IDLE 복귀
        L3_event_clearEventFlag(L3_event_timeout);
        debug_if(DBGMSG_L3, "[L3] timeout event in WAIT_PAIR, reset to IDLE\n");
        L3_resetAll();
        main_state = L3STATE_IDLE;
      } else if (!L3_timer_getTimerStatus()) {
        // 이벤트 C: 타이머가 종료되면(PAIR timeout) pending 정보를 정리(Action 8)하고 IDLE 복귀
        debug_if(DBGMSG_L3,
                 "[L3] WAIT_PAIR timeout for trader %i, reset to IDLE\n",
                 pendingTxn.id);

        L3_resetAll();
        main_state = L3STATE_IDLE;
      }
      break;

    case L3STATE_WAIT_PRICE_CNF:
      // Event B. CNF 메시지 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        if (L3_msg_checkIfCnf(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] Price CNF received from %i\n", srcId);
          if (srcId == pendingTxn.id) {
            cnf_p_rcvd = true;
            if (L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT] == 1) {
              cnf_p_accpt = true;
            } else {
              cnf_p_accpt = false;
            }
          } else if (srcId == matchingTxn.id) {
            cnf_m_rcvd = true;
            if (L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT] == 1) {
              cnf_m_accpt = true;
            } else {
              cnf_m_accpt = false;
            }
          } else {
            debug_if(DBGMSG_L3,
                     "[L3][WARNING] CNF received from unknown trader %i\n",
                     srcId);
          }

          // Event B. 둘 다에게서 CNF를 수신했을 경우
          if (cnf_p_rcvd && cnf_m_rcvd) {
            // c3. 둘 다에게서 accept 받았을 경우
            if (cnf_p_accpt && cnf_m_accpt) {
              pc.printf("[L3] Both traders accepted price %i \n", avg_price);
              pc.printf(
                  "[L3] Sending Both traders LOC REC %i to both traders\n",
                  AVG_LOC);

              // 양측 Trader에게 LOC REC 메시지 보내기
              L3_sendRecLoc(pendingTxn.id);
              L3_sendRecLoc(matchingTxn.id);

              // 타이머 정지 및 MCH 메시지 타이머 재시작
              L3_timer_stopTimer();
              L3_timer_startTimer();

              // 값 초기화
              cnf_p_accpt = false;
              cnf_m_accpt = false;
              cnf_p_rcvd = false;
              cnf_m_rcvd = false;

              main_state = L3STATE_WAIT_LOC_CNF;
            } else {
              // !c3. 둘 중 적어도 한 명이 reject 했을 경우
              pc.printf(
                  "[L3] CNFs received, but at least one reject, resetting\n");
              // 양측 Trader에게 MCH 메시지 보내기 (reject)
              L3_sendMch(pendingTxn.id, 0);
              L3_sendMch(matchingTxn.id, 0);

              // reset & go to idle
              L3_resetAll();
              main_state = L3STATE_IDLE;
            }
          }
          L3_event_clearEventFlag(L3_event_msgRcvd);
        } else {
          // CNF 메시지가 아닌 TXN 메시지나 알 수 없는 메시지가 온 경우
          L3_event_clearEventFlag(L3_event_msgRcvd);
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in WAIT_PRICE_CNF state\n");
        }
      }
      // Event D. CNF 송신 timeout
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        pc.printf("[L3] CNF timeout occurred, dropping both traders\n");

        // 양측 Trader에게 MCH 메시지 보내기 (reject)
        L3_sendMch(pendingTxn.id, 0);
        L3_sendMch(matchingTxn.id, 0);

        L3_event_clearEventFlag(L3_event_timeout);
        // reset & go to idle
        L3_resetAll();
        main_state = L3STATE_IDLE;
      }
      break;

    case L3STATE_WAIT_LOC_CNF:
      // Event B. CNF 메시지 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        if (L3_msg_checkIfCnf(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] Location CNF received from %i\n", srcId);
          if (srcId == pendingTxn.id) {
            cnf_p_rcvd = true;
            if (L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT] == 1) {
              cnf_p_accpt = true;
            } else {
              cnf_p_accpt = false;
            }
          } else if (srcId == matchingTxn.id) {
            cnf_m_rcvd = true;
            if (L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT] == 1) {
              cnf_m_accpt = true;
            } else {
              cnf_m_accpt = false;
            }
          } else {
            debug_if(DBGMSG_L3,
                     "[L3][WARNING] CNF received from unknown trader %i\n",
                     srcId);
          }

          // Event B. 둘 다에게서 CNF를 수신했을 경우
          if (cnf_p_rcvd && cnf_m_rcvd) {
            // c3. 둘 다에게서 accept 받았을 경우
            if (cnf_p_accpt && cnf_m_accpt) {
              pc.printf("[L3] Both traders accepted price %i \n", avg_price);
              pc.printf(
                  "[L3] Sending Both traders LOC REC %i to both traders\n",
                  AVG_LOC);

              // 양측 Trader에게 MCH 메시지 보내기 (success)
              L3_sendMch(pendingTxn.id, 1);
              L3_sendMch(matchingTxn.id, 1);

              // reset & go to idle
              pc.printf(
                  "[L3] Transaction between %i & %i completed successfully!\n",
                  pendingTxn.id, matchingTxn.id);
              L3_resetAll();
              main_state = L3STATE_IDLE;
            } else {
              // !c3. 둘 중 적어도 한 명이 reject 했을 경우
              pc.printf(
                  "[L3] CNFs received, but at least one reject, resetting\n");
              // 양측 Trader에게 MCH 메시지 보내기 (reject)
              L3_sendMch(pendingTxn.id, 0);
              L3_sendMch(matchingTxn.id, 0);

              // reset & go to idle
              L3_resetAll();
              main_state = L3STATE_IDLE;
            }
          }
          L3_event_clearEventFlag(L3_event_msgRcvd);
        } else {
          // CNF 메시지가 아닌 TXN 메시지나 알 수 없는 메시지가 온 경우
          L3_event_clearEventFlag(L3_event_msgRcvd);
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in WAIT_LOC_CNF state\n");
        }
      }
      // Event D. CNF 송신 timeout
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        pc.printf("[L3] CNF timeout occurred, dropping both traders\n");

        // 양측 Trader에게 MCH 메시지 보내기 (reject)
        L3_sendMch(pendingTxn.id, 0);
        L3_sendMch(matchingTxn.id, 0);

        L3_event_clearEventFlag(L3_event_timeout);
        // reset & go to idle
        L3_resetAll();
        main_state = L3STATE_IDLE;
      }
      break;
  }
}