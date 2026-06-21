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
#define MAX_INDEX 65536

// state variables
static uint8_t main_state = L3STATE_IDLE;  // 현재 상태
static uint8_t prev_state = main_state;    // 이전 상태(상태 전환 로그용)

// FSM context variables
static uint8_t seqNum[MAX_TRADERID] = {0};
static L3_txnInfo_t pendingTxn;
static bool hasPendingTxn = false;
static L3_txnInfo_t matchingTxn;  // A와 매칭되는 B의 txn 정보

#define L3_MAX_RETRY 3
uint8_t rtylst[MAX_INDEX / 4] = {
    0,
};  // 재시도 횟수 리스트 초기화, 크기 16348

static uint16_t avg_price = 0;  // A와 B의 가격 평균, 임의 초기값 0

static bool cnf_p_rcvd = false;  // pendingTxn(A) 한테서 CNF 받았나
static bool cnf_m_rcvd = false;  // matchingTxn(B) 한테서 CNF 받았나

// 양쪽에 price rec 전송 후
static bool cnf_p_accpt = false;
static bool cnf_m_accpt = false;

// serial port interface
static Serial pc(USBTX, USBRX);

// 로그 관련
static uint8_t log_loop_cnt = 100000;

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

// c4, c5. 재시도 횟수 검사 및 업데이트 함수
static uint8_t L3_getTraderPairRetryCnt(uint8_t pendingTxn_id,
                                        uint8_t matchingTxn_id) {
  uint8_t a = (pendingTxn_id < matchingTxn_id) ? pendingTxn_id : matchingTxn_id;
  uint8_t b = (pendingTxn_id > matchingTxn_id) ? pendingTxn_id : matchingTxn_id;

  // a를 왼쪽으로 8비트 밀고, b를 하위 8비트에 붙입니다.
  // 작은 거 먼저 앞으로 쓰는 법칙
  uint16_t index = ((uint16_t)a << 8) | b;

  uint16_t array_idx = index / 4;
  uint8_t bit_shift =
      (index % 4) * 2;  // 바이트 내부에서 몇번째 비트인지 (0,2,4,6)

  // 재시도 횟수 비교 후 증가시키기
  uint8_t try_cnt = (rtylst[array_idx] >> bit_shift) & L3_MAX_RETRY;
  if (try_cnt < L3_MAX_RETRY) {
    rtylst[array_idx] &= ~(L3_MAX_RETRY << bit_shift);  // 2비트 클리어
    rtylst[array_idx] |= ((try_cnt + 1) << bit_shift);  // 1 더해서 업데이트
  }
  return try_cnt;
}

// 시퀀스 번호 생성 함수
static uint8_t L3_getNextSeqNum(uint8_t id) {
  seqNum[id] = (seqNum[id] + 1) % L3_MAX_SEQNUM;
  return seqNum[id];
}

// action 1: TXN 메시지 내용을 pendingTxn에 저장하는 함수
static void L3_storeTxn(L3_txnInfo_t* txnInfo) {
  pendingTxn = *txnInfo;
  hasPendingTxn = true;
}

// action 3-1: Trader에게 PRICE REC 메시지 보내는 함수
static void L3_sendRecPrice(uint8_t traderId, uint16_t avg_price) {
  uint8_t rec[L3_MSG_REC_SIZE];
  uint8_t pduSize = L3_msg_encodeRec(rec, L3_getNextSeqNum(traderId),
                                     L3_COORDINATOR_ID, traderId, avg_price);
  L3_LLI_dataReqFunc(rec, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] PRICE REC sent to trader %i with price %i\n",
           traderId, avg_price);
}

// action 3-2: Trader에게 LOC REC 메시지 보내는 함수
static void L3_sendRecLoc(uint8_t traderId) {
  uint8_t rec[L3_MSG_REC_SIZE];
  uint16_t avg_loc = AVG_LOC;
  uint8_t pduSize = L3_msg_encodeRec(rec, L3_getNextSeqNum(traderId),
                                     L3_COORDINATOR_ID, traderId, avg_loc);
  L3_LLI_dataReqFunc(rec, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] LOC REC sent to trader %i with location %05i\n",
           traderId, avg_loc);
}

// action 4: Trader에게 MCH 메시지 보내는 함수
static void L3_sendMch(uint8_t traderId, uint8_t accept) {
  uint8_t mch[L3_MSG_MCH_SIZE];
  uint8_t pduSize = L3_msg_encodeMch(mch, L3_getNextSeqNum(traderId),
                                     L3_COORDINATOR_ID, traderId, accept);
  L3_LLI_dataReqFunc(mch, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] MCH sent to trader %i with accept %i\n", traderId,
           accept);
}

// action 5: Trader에게 WAIT_PAIR 메시지 보내는 함수
static void L3_sendWaitPair(uint8_t traderId) {
  uint8_t waitPair[L3_MSG_WAIT_PAIR_SIZE];
  uint8_t pduSize = L3_msg_encodeWaitPair(waitPair, seqNum[traderId],
                                          L3_COORDINATOR_ID, traderId);
  L3_LLI_dataReqFunc(waitPair, pduSize, traderId);
  debug_if(DBGMSG_L3, "[L3] WAIT_PAIR sent to trader %i\n", traderId);
}

// action 8. 재시도 리스트를 제외한 모든 정보 리셋
void L3_resetAll() {
  hasPendingTxn = false;
  cnf_p_rcvd = false;
  cnf_m_rcvd = false;
  cnf_p_accpt = false;
  cnf_m_accpt = false;
  memset(&pendingTxn, 0, sizeof(L3_txnInfo_t));
  memset(&matchingTxn, 0, sizeof(L3_txnInfo_t));
  L3_timer_stopTimer();
  L3_event_clearAllEventFlag();
}

const char* L3_returnTraderRole(uint8_t isSeller) {
  return (isSeller == 1) ? "판매자" : "구매자";
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
        uint8_t* msg =
            L3_LLI_getMsgPtr();  // 수신된 메시지 바이트 배열의 시작 주소 가져옴
        uint8_t size = L3_LLI_getSize();  // 수신된 메시지가 몇 바이트인지

        // Event A. TXN 수신 - 메시지가 TXN이라면 내용을 꺼내서 TXNinfo에 담음
        if (L3_msg_checkIfTxn(msg, size)) {
          L3_txnInfo_t txnInfo;             // txn 파싱 결과 담을 구조체 선언
          int16_t rssi = L3_LLI_getRssi();  // 신호 세기 가져오기
          if (L3_msg_decodeTxn(msg, size, &txnInfo, rssi)) {  // 크기가 0 아님
            // c1. 신호 세기 확인
            if (L3_signalConditionPassed(txnInfo.signal)) {
              L3_storeTxn(&txnInfo);

              // 품목 번호가 배열 사이즈(4)를 넘어가면 안전하게 0번 출력
              uint8_t g_idx = (pendingTxn.goods < 4) ? pendingTxn.goods : 0;
              pc.printf(
                  "저장된 거래자 [ID %i]: %s\n"
                  "  > 거래 품목: %s\n"
                  "  > 가격: %i$\n",
                  pendingTxn.id, L3_returnTraderRole(pendingTxn.isSeller),
                  goods_name[g_idx], pendingTxn.price);

              // debug_if(DBGMSG_L3, "[L3] 현재 SEQ_NUM: %i\n",
              // seqNum[txnInfo.id]);
              L3_sendWaitPair(txnInfo.id);
              L3_timer_startTimer(L3_PAIR_TIMEOUT);
              main_state = L3STATE_WAIT_PAIR;
              debug_if(DBGMSG_L3, "[L3] 현재 %i의 SEQ_NUM: %i\n", txnInfo.id,
                       seqNum[txnInfo.id]);
            } else {
              debug_if(DBGMSG_L3,
                       "[L3] TXN ignored, RSSI %i is lower than minimum %i\n",
                       txnInfo.signal, L3_MIN_RSSI);
            }
          } else {  // 크기가 0인 TXN
            debug_if(DBGMSG_L3, "[L3] invalid TXN received in IDLE state\n");
          }
        } else if (L3_msg_checkIfCnf(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] CNF ignored in IDLE state\n");
        } else {
          // debug_if(DBGMSG_L3, "[L3] unknown PDU ignored in IDLE state\n");
        }
        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      // 발생할 리 없는 Timeout이 발생했을 때
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        debug_if(DBGMSG_L3, "[L3] invalid timeout occured in IDLE state\n");
        L3_event_clearEventFlag(L3_event_timeout);
      }
      break;

    case L3STATE_WAIT_PAIR:
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        if (!hasPendingTxn) {
          // 원래 대기 중이어야 할 첫 TXN이 없다면 상태가 꼬인 것이므로
          // 안전하게 IDLE로 되돌린다.
          pc.printf("거래자 탐색을 재시작합니다!\n");
          debug_if(DBGMSG_L3,
                   "[L3][WARNING] WAIT_PAIR has no pending TXN, reset to "
                   "IDLE\n");
          L3_resetAll();
          main_state = L3STATE_IDLE;
          break;
        }

        // 하위 계층에서 새로운 PDU가 도착했으므로, 내용을 읽어서
        // TXN / CNF / unknown 여부를 다시 판별한다.
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        // 같은 trader가 같은 요청을 다시 보낸 경우는 중복으로 보고 무시
        if (L3_LLI_getSrcId() == pendingTxn.id) {
          // 정적 변수로 카운터 선언 (함수가 끝나도 값이 유지됨)
          static uint32_t ignore_log_cnt = 0;

          // {log_loop_cnt}번 루프 돌 때 1번만 로그 출력
          if (ignore_log_cnt++ % log_loop_cnt == 0)
            debug_if(
                DBGMSG_L3,
                "[L3] duplicated TXN from trader %i ignored in WAIT_PAIR\n",
                pendingTxn.id);
          break;
        }

        // Event A. TXN 수신 - 메시지가 TXN이라면 내용을 꺼내서 TXNinfo에 담음
        if (L3_msg_checkIfTxn(msg, size)) {
          L3_txnInfo_t txnInfo;
          int16_t rssi = L3_LLI_getRssi();
          // Event A. TXN 수신 - 메시지가 TXN이라면 내용을 꺼내서 TXNinfo에 담음
          if (L3_msg_decodeTxn(msg, size, &txnInfo, rssi)) {
            // c1. 새로운 trader 신호 검사
            if (!L3_signalConditionPassed(txnInfo.signal)) {
              // 신호가 너무 약하면 이 TXN은 매칭 후보로 보지 않는다.
              debug_if(DBGMSG_L3,
                       "[L3] TXN ignored in WAIT_PAIR, RSSI %i is lower than "
                       "minimum %i\n",
                       txnInfo.signal, L3_MIN_RSSI);
            }
            // c2. 두 trader의 상보성 조건 검사 (seller/buyer 조합, goods 일치)
            else if (L3_pairConditionPassed(&pendingTxn, &txnInfo)) {
              L3_timer_stopTimer();
              // debug_if(DBGMSG_L3,
              //          "[L3] pair candidate found: trader %i <-> trader
              //          %i\n", pendingTxn.id, txnInfo.id);

              // 두 Trader 간의 재시도 횟수(조건 c4, c5)
              uint8_t try_cnt =
                  L3_getTraderPairRetryCnt(pendingTxn.id, txnInfo.id);
              // !c4, !c5. 재시도 횟수 초과 시 WAIT_PAIR 복귀
              if (try_cnt >= L3_MAX_RETRY) {
                debug_if(
                    DBGMSG_L3,
                    "[L3] retry count (%i) exceeded for trader %i and %i\n",
                    try_cnt, pendingTxn.id, txnInfo.id);
                main_state = L3STATE_WAIT_PAIR;
              }
              // c4, c5 만족 시
              else {
                matchingTxn = txnInfo;
                pc.printf(
                    "페어 후보가 지정되었습니다! [ID %i] %s <-> [ID %i] %s\n"
                    "\n======================\n"
                    "  > [ID %i] %s 제시가격: %i$\n  > [ID %i] %s 제시가격: "
                    "%i$"
                    "\n======================\n: ",
                    pendingTxn.id, L3_returnTraderRole(pendingTxn.isSeller),
                    matchingTxn.id, L3_returnTraderRole(matchingTxn.isSeller),
                    pendingTxn.id, L3_returnTraderRole(pendingTxn.isSeller),
                    pendingTxn.price, matchingTxn.id,
                    L3_returnTraderRole(matchingTxn.isSeller),
                    matchingTxn.price);

                // action 8: 두 번째로 매칭된 trader에게 WAIT_PAIR 전송
                L3_sendWaitPair(matchingTxn.id);
                debug_if(DBGMSG_L3,
                         "[L3] WAIT_PAIR sent to matching trader %i, "
                         "waiting for send CNF\n",
                         matchingTxn.id);

                // action 3: Send REC
                avg_price = (pendingTxn.price + matchingTxn.price) / 2;
                pc.printf("=> 중개 가격: %i$\n", avg_price);

                L3_sendRecPrice(pendingTxn.id, avg_price);
                L3_sendRecPrice(matchingTxn.id, avg_price);

                debug_if(
                    DBGMSG_L3,
                    "[L3] REC sent to both traders (%i, %i) with avg price %i, "
                    "waiting for CNF\n",
                    pendingTxn.id, matchingTxn.id, avg_price);

                // action 6 & 7: 타이머 재시작 (앞서 stop했으므로 다시 start)
                L3_timer_startTimer(L3_CNF_TIMEOUT);

                main_state = L3STATE_WAIT_PRICE_CNF;
              }
            }
            // !c2의 경우 WAIT_PAIR로 돌아간다.
          }
        } else {
          // 타입을 판별할 수 없는 PDU는 잘못된 메시지로 보고 무시한다.
          // debug_if(DBGMSG_L3, "[L3] unknown PDU ignored in WAIT_PAIR
          // state\n");
        }

        // msgRcvd 이벤트는 이번 수신 패킷에 대한 처리가 끝났으므로 지운다.
        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      // 이벤트 D: Pair timeout 발생 시 IDLE 복귀
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        L3_event_clearEventFlag(L3_event_timeout);
        // debug_if(DBGMSG_L3, "[L3] timeout event in WAIT_PAIR, reset to
        // IDLE\n");
        pc.printf(
            "[ID %i] %s의 페어 탐색에 실패했습니다. (%i초 페어 탐색 시간 "
            "초과)\n",
            pendingTxn.id, L3_returnTraderRole(pendingTxn.isSeller),
            L3_PAIR_TIMEOUT);
        if (hasPendingTxn) {
          L3_sendMch(pendingTxn.id, 0);
          debug_if(DBGMSG_L3, "[L3] Sending trader %i MCH=0(fail) msg\n",
                   pendingTxn.id);
        }

        // 새로운 PDU를 보내는 로직 추가 필요
        L3_resetAll();
        pc.printf("거래자 탐색을 재시작합니다!\n");
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
            uint8_t accpt = L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT];
            pc.printf("> [ID %i] %s: %s\n", pendingTxn.id,
                      L3_returnTraderRole(pendingTxn.isSeller),
                      (accpt == 1) ? "수락" : "거절");
            if (accpt == 1) {
              cnf_p_accpt = true;
            } else {
              cnf_p_accpt = false;
            }
          } else if (srcId == matchingTxn.id) {
            cnf_m_rcvd = true;
            uint8_t accpt = L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT];
            pc.printf("> [ID %i] %s: %s\n", matchingTxn.id,
                      L3_returnTraderRole(matchingTxn.isSeller),
                      (accpt == 1) ? "수락" : "거절");
            if (accpt == 1) {
              cnf_m_accpt = true;
            } else {
              cnf_m_accpt = false;
            }
          } else {
            // debug_if(DBGMSG_L3,
            //          "[L3][WARNING] CNF received from unknown trader %i\n",
            //          srcId);
          }

          // Event B. 둘 다에게서 CNF를 수신했을 경우
          if (cnf_p_rcvd && cnf_m_rcvd) {
            // c3. 둘 다에게서 accept 받았을 경우
            if (cnf_p_accpt && cnf_m_accpt) {
              pc.printf(
                  "모든 거래자가 중개가격 %i$을 수락하였습니다!\n"
                  "위치 조정을 시작합니다.\n"
                  "중개 위치: [우편번호 %05i]",
                  avg_price, AVG_LOC);
              debug_if(DBGMSG_L3, "[L3] Sending Both traders LOC REC %i\n",
                       AVG_LOC);

              // 양측 Trader에게 LOC REC 메시지 보내기
              L3_sendRecLoc(pendingTxn.id);
              L3_sendRecLoc(matchingTxn.id);

              // 타이머 정지 및 MCH 메시지 타이머 재시작
              L3_timer_stopTimer();
              L3_timer_startTimer(L3_CNF_TIMEOUT);

              // 값 초기화
              cnf_p_accpt = false;
              cnf_m_accpt = false;
              cnf_p_rcvd = false;
              cnf_m_rcvd = false;

              main_state = L3STATE_WAIT_LOC_CNF;
            } else {
              // !c3. 둘 중 적어도 한 명이 reject 했을 경우
              pc.printf(
                  "중개가격 %i$ 협상에 실패하였습니다!\n"
                  "거래를 무효화합니다.\n",
                  avg_price);
              debug_if(DBGMSG_L3,
                       "[L3] Sending Both traders MCH=0(fail) msg\n");

              // 양측 Trader에게 MCH 메시지 보내기 (reject)
              L3_sendMch(pendingTxn.id, 0);
              L3_sendMch(matchingTxn.id, 0);

              // reset & go to idle
              L3_resetAll();
              pc.printf("거래자 탐색을 재시작합니다!\n");
              main_state = L3STATE_IDLE;
            }
          }
        } else {
          // CNF 메시지가 아닌 TXN 메시지나 알 수 없는 메시지가 온 경우
          // debug_if(DBGMSG_L3,
          //          "[L3] unknown PDU ignored in WAIT_PRICE_CNF state\n");
        }
        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      // Event D. CNF 송신 timeout
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        pc.printf(
            "수락 메시지가 %i초 내에 오지 않았습니다!\n"
            "거래를 무효화합니다.\n",
            L3_CNF_TIMEOUT);
        debug_if(DBGMSG_L3, "[L3] Sending Both traders MCH=0(fail) msg\n");

        // 양측 Trader에게 MCH 메시지 보내기 (reject)
        L3_sendMch(pendingTxn.id, 0);
        L3_sendMch(matchingTxn.id, 0);

        L3_event_clearEventFlag(L3_event_timeout);
        // reset & go to idle
        L3_resetAll();
        pc.printf("거래자 탐색을 재시작합니다!\n");
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
            uint8_t accpt = L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT];
            pc.printf("> [ID %i] %s: %s\n", pendingTxn.id,
                      L3_returnTraderRole(pendingTxn.isSeller),
                      (accpt == 1) ? "수락" : "거절");
            if (accpt == 1) {
              cnf_p_accpt = true;
            } else {
              cnf_p_accpt = false;
            }
          } else if (srcId == matchingTxn.id) {
            cnf_m_rcvd = true;
            uint8_t accpt = L3_msg_getPayload(msg)[L3_CNF_OFFSET_ACCPT];
            pc.printf("> [ID %i] %s: %s\n", matchingTxn.id,
                      L3_returnTraderRole(matchingTxn.isSeller),
                      (accpt == 1) ? "수락" : "거절");
            if (accpt == 1) {
              cnf_m_accpt = true;
            } else {
              cnf_m_accpt = false;
            }
          } else {
            // debug_if(DBGMSG_L3,
            //          "[L3][WARNING] CNF received from unknown trader %i\n",
            //          srcId);
          }

          // Event B. 둘 다에게서 CNF를 수신했을 경우
          if (cnf_p_rcvd && cnf_m_rcvd) {
            // c3. 둘 다에게서 accept 받았을 경우
            if (cnf_p_accpt && cnf_m_accpt) {
              pc.printf(
                  "모든 거래자가 중개 위치 [우편번호 %05i]을 수락하였습니다!\n"
                  "[ID %i] %s와 [ID %i] %s의 거래가 성사되었습니다!\n",
                  AVG_LOC, pendingTxn.id,
                  L3_returnTraderRole(pendingTxn.isSeller), matchingTxn.id,
                  L3_returnTraderRole(matchingTxn.isSeller));
              debug_if(DBGMSG_L3,
                       "[L3] Sending Both traders MCH=1(success) msg\n");
              // 양측 Trader에게 MCH 메시지 보내기 (success)
              L3_sendMch(pendingTxn.id, 1);
              L3_sendMch(matchingTxn.id, 1);

              // reset & go to idle
              L3_resetAll();
              pc.printf("거래자 탐색을 재시작합니다!\n");
              main_state = L3STATE_IDLE;
            } else {
              // !c3. 둘 중 적어도 한 명이 reject 했을 경우
              pc.printf(
                  "중개 위치 [우편번호 %05i] 협상에 실패하였습니다!\n"
                  "거래를 무효화합니다.\n",
                  AVG_LOC);
              // 양측 Trader에게 MCH 메시지 보내기 (reject)
              debug_if(DBGMSG_L3,
                       "[L3] Sending Both traders MCH=0(fail) msg\n");
              L3_sendMch(pendingTxn.id, 0);
              L3_sendMch(matchingTxn.id, 0);

              // reset & go to idle
              L3_resetAll();
              pc.printf("거래자 탐색을 재시작합니다!\n");
              main_state = L3STATE_IDLE;
            }
          }
        } else {
          // CNF 메시지가 아닌 TXN 메시지나 알 수 없는 메시지가 온 경우
          // debug_if(DBGMSG_L3,
          //          "[L3] unknown PDU ignored in WAIT_LOC_CNF state\n");
        }
        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      // Event D. CNF 송신 timeout
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        pc.printf(
            "수락 메시지가 %i초 내에 오지 않았습니다!\n"
            "거래를 무효화합니다.\n",
            L3_CNF_TIMEOUT);

        // 양측 Trader에게 MCH 메시지 보내기 (reject)
        debug_if(DBGMSG_L3, "[L3] Sending Both traders MCH=0(fail) msg\n");
        L3_sendMch(pendingTxn.id, 0);
        L3_sendMch(matchingTxn.id, 0);

        L3_event_clearEventFlag(L3_event_timeout);
        // reset & go to idle
        L3_resetAll();
        pc.printf("거래자 탐색을 재시작합니다!\n");
        main_state = L3STATE_IDLE;
      }
      break;
  }
}