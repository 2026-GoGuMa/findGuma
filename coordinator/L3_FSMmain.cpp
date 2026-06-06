#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"

// FSM state -------------------------------------------------
#define L3STATE_IDLE 0            // 초기 상태, TXN 수신 대기
#define L3STATE_WAIT_PAIR 1       // 첫 번째 trader 등록 후 두 번째 trader 대기
#define L3STATE_WAIT_PRICE_CNF 2  // 양측에 가격 REC 보내고 CNF 대기
#define L3STATE_WAIT_LOC_CNF 3    // 양측에 위치 REC 보내고 CNF 대기

// state variables
static uint8_t main_state = L3STATE_IDLE;  // 현재 상태
static uint8_t prev_state = main_state;    // 이전 상태(상태 전환 로그용)

// FSM context variables
static uint8_t L3SeqNum = 1;
static L3_txnInfo_t pendingTxn;   // 먼저 등록한 trader A의 거래(txn) 정보
static L3_txnInfo_t matchingTxn;  // A와 매칭되는 B의 txn 정보

static uint16_t avg_price = 0;  // A와 B의 가격 평균, 임의 초기값 0

static bool cnf_p_rcvd = false;  // pendingTxn(A) 한테서 CNF 받았나
static bool cnf_m_rcvd = false;  // matchingTxn(B) 한테서 CNF 받았나

// 양쪽에 price rec 전송 후
static bool cnf_p_accpt = false;
static bool cnf_m_accpt = false;

// serial port interface
static Serial pc(USBTX, USBRX);

// c1. 신호 조건 검사 함수 (RSSI 기반)
static uint8_t L3_signalConditionPassed(int16_t rssi) {
  return rssi >= L3_MIN_RSSI;
}

// 메시지마다 순서 번호 붙임 (중복 메시지 거르기 위해)
static uint8_t L3_getNextSeqNum(void) {
  uint8_t seqNum = L3SeqNum;
  L3SeqNum = (L3SeqNum + 1) % L3_MSG_MAX_SEQNUM;
  if (L3SeqNum == 0) L3SeqNum = 1;
  return seqNum;
}

// action 1: TXN 메시지 내용을 pendingTxn에 저장하는 함수
static void L3_storeTxn(L3_txnInfo_t* txnInfo) {
  pendingTxn = *txnInfo;

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
  // 이전 상태와 현재 상태가 다른 경우, 로그찍음
  if (prev_state != main_state) {
    debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state,
             main_state);
    prev_state = main_state;
  }

  switch (main_state) {
    // IDLE 상태에서 메시지 수신 이벤트 왔을 경우
    case L3STATE_IDLE:
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t* dataPtr =
            L3_LLI_getMsgPtr();  // 수신된 메시지 바이트 배열의 시작 주소 가져옴
        uint8_t size = L3_LLI_getSize();  // 수신된 메시지가 몇 바이트인지
        L3_txnInfo_t txnInfo;             // txn 파싱 결과 담을 구조체 선언

        int16_t rssi = L3_LLI_getRssi();  // 신호 세기 가져오기

        // 메시지가 TXN이라면 내용을 꺼내서 TXNinfo에 담음
        if (L3_msg_decodeTxn(dataPtr, size, &txnInfo)) {
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

        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      break;

    case L3STATE_WAIT_PAIR:
      if (L3_event_checkEventFlag(L3_event_dataSendCnf)) {
        L3_event_clearEventFlag(L3_event_dataSendCnf);
        // 개발 더
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