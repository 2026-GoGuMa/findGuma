#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"

// FSM state
#define L3STATE_BROADCASTING 0    // coordinator의 WAIT_PAIR 메시지 기다리는 중
#define L3STATE_WAIT_PRICE_REC 1  // TXN 보냄 → 가격 REC 기다리는 중
#define L3STATE_WAIT_LOC_REC 2    // 가격 CNF 보냄 → 위치 REC 기다리는 중
#define L3STATE_WAIT_LOC_MCH 3    // 위치 CNF 보냄 → 매칭 결과 기다리는 중

// state variables
static uint8_t main_state = L3STATE_BROADCASTING;
static uint8_t prev_state = main_state;

// trader 노드 정보 (L3_initFSM에서 설정)
static uint8_t myId;
static uint8_t coordId;
static uint8_t isSeller;
static uint8_t goods;
static uint16_t price;
static uint16_t seq_num = 0;

// 사용자 입력 대기 플래그
static uint8_t waiting_price_cnf = 0;
static uint8_t waiting_loc_cnf = 0;

// coordinator로부터 수신한 값
// REC는 가격·위치 각각 2바이트로 따로 전송되므로 변수를 분리 유지
static uint16_t rcvd_avg_price = 0;
static uint16_t rcvd_avg_loc = 0;
static uint8_t rcvd_match_success = 0;

static uint8_t sdu[L3_MAXDATASIZE];
extern Serial pc;

// --- action 함수 ---

// TXN 메시지를 만들어 coordinator에게 전송
static void L3_action_sendTxn(void) {
  // TXN 메시지를 sdu 버퍼에 조립
  uint8_t size =
      L3_msg_encodeTxn(sdu, myId, coordId, seq_num++, isSeller, goods, price);
  // 조립된 메시지를 LL 레이어를 통해 coordId 에게 전송
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] TXN sent\n");
}

// 가격 CNF 전송: 사용자 입력값(accept)을 그대로 전달
static void L3_action_sendPriceCnf(uint8_t accept) {
  uint8_t size = L3_msg_buildCnf(sdu, myId, coordId, seq_num++, accept);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] price CNF sent (accept=%u), avg_price=%u\n", accept,
           rcvd_avg_price);
}

// 위치 CNF 전송: 사용자 입력값(accept)을 그대로 전달
static void L3_action_sendLocCnf(uint8_t accept) {
  uint8_t size = L3_msg_buildCnf(sdu, myId, coordId, seq_num++, accept);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] loc CNF sent (accept=%u), avg_loc=%u\n", accept,
           rcvd_avg_loc);
}

// 리셋: 수신 값 초기화 + 결과 출력 후 BROADCASTING으로 복귀 준비
static void L3_action_reset(uint8_t success) {
  waiting_price_cnf = 0;
  waiting_loc_cnf = 0;
  rcvd_avg_price = 0;
  rcvd_avg_loc = 0;
  rcvd_match_success = 0;
  L3_event_clearAllEventFlag();
  pc.printf("[Trader] Trade %s\n", success ? "succeeded!" : "failed.");
}

// --- init / run ---

void L3_initFSM(uint8_t id, uint8_t cId, uint8_t seller, uint8_t g,
                uint16_t p) {
  myId = id;
  coordId = cId;
  isSeller = seller;
  goods = g;
  price = p;
  pc.printf("[Trader] id=%u coord=%u isSeller=%u goods=%u price=$%u\n", myId,
            coordId, isSeller, goods, price);
}

void L3_FSMrun(void) {
  // 이전 상태와 현재 상태가 다른 경우, 로그 찍음
  if (prev_state != main_state) {
    debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state,
             main_state);
    prev_state = main_state;
  }

  switch (main_state) {
    case L3STATE_BROADCASTING:
      // Event A. coordinator로부터 WAIT_PAIR 를 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();  // 메시지 보낸 노드 ID
        uint8_t* msg = L3_LLI_getMsgPtr();  // 메시지 내용
        uint8_t size = L3_LLI_getSize();    // 메시지 길이

        if (L3_msg_checkIfWaitPair(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] wait pair received from %i\n", srcId);
          if (srcId == coordId) {
            L3_action_sendTxn();                  // TXN 전송
            L3_timer_startTimer();                // 타이머 시작
            main_state = L3STATE_WAIT_PRICE_REC;  // 상태 전환
          }
        } else {
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in BROADCASTING state\n");
        }
        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      break;

    // 가격 평균 기다리는 중
    case L3STATE_WAIT_PRICE_REC:
      if (!waiting_price_cnf && L3_event_checkEventFlag(L3_event_recRcvd)) {
        // Event B: 가격 REC 수신 → 타이머 stop + 사용자 입력 요청
        L3_timer_stopTimer();
        pc.printf("[Trader] avg_price=%u. Accept? (1=yes / 0=no): ",
                  rcvd_avg_price);
        waiting_price_cnf = 1;
        L3_event_clearEventFlag(L3_event_recRcvd);
      }
      if (waiting_price_cnf) {
        if (L3_event_checkEventFlag(L3_event_userAccept)) {
          waiting_price_cnf = 0;
          L3_action_sendPriceCnf(1);
          L3_timer_startTimer(L3_PAIR_TIMEOUT);
          L3_event_clearEventFlag(L3_event_userAccept);
          main_state = L3STATE_WAIT_LOC_REC;
        } else if (L3_event_checkEventFlag(L3_event_userReject)) {
          waiting_price_cnf = 0;
          L3_action_sendPriceCnf(0);
          L3_action_reset(0);
          L3_event_clearEventFlag(L3_event_userReject);
          main_state = L3STATE_BROADCASTING;
        } else if (L3_event_checkEventFlag(L3_event_timeout)) {
          // 입력 없이 타임아웃 → 실패 처리
          L3_timer_stopTimer();
          L3_action_reset(0);
          main_state = L3STATE_BROADCASTING;
        }
      } else if (L3_event_checkEventFlag(L3_event_mchRcvd) ||
                 L3_event_checkEventFlag(L3_event_timeout)) {
        // Event C (match_fail) or D (timeout): 실패 reset → BROADCASTING
        L3_timer_stopTimer();
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    // 위치 평균 기다리는 중
    case L3STATE_WAIT_LOC_REC:
      if (!waiting_loc_cnf && L3_event_checkEventFlag(L3_event_recRcvd)) {
        // Event B: 위치 REC 수신 → 타이머 stop + 사용자 입력 요청
        L3_timer_stopTimer();
        pc.printf("[Trader] avg_loc=%u. Accept? (1=yes / 0=no): ",
                  rcvd_avg_loc);
        waiting_loc_cnf = 1;
        L3_event_clearEventFlag(L3_event_recRcvd);
      }
      if (waiting_loc_cnf) {
        if (L3_event_checkEventFlag(L3_event_userAccept)) {
          waiting_loc_cnf = 0;
          L3_action_sendLocCnf(1);
          L3_timer_startTimer(L3_MCH_TIMEOUT);
          L3_event_clearEventFlag(L3_event_userAccept);
          main_state = L3STATE_WAIT_LOC_MCH;
        } else if (L3_event_checkEventFlag(L3_event_userReject)) {
          waiting_loc_cnf = 0;
          L3_action_sendLocCnf(0);
          L3_action_reset(0);
          L3_event_clearEventFlag(L3_event_userReject);
          main_state = L3STATE_BROADCASTING;
        } else if (L3_event_checkEventFlag(L3_event_timeout)) {
          L3_timer_stopTimer();
          L3_action_reset(0);
          main_state = L3STATE_BROADCASTING;
        }
      } else if (L3_event_checkEventFlag(L3_event_mchRcvd) ||
                 L3_event_checkEventFlag(L3_event_timeout)) {
        // Event C or D: 실패 reset → BROADCASTING
        L3_timer_stopTimer();
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    // 매칭 결과 기다리는 중
    case L3STATE_WAIT_LOC_MCH:
      if (L3_event_checkEventFlag(L3_event_mchRcvd)) {
        // Event C: MCH 수신 → 성공/실패 출력 → BROADCASTING
        L3_timer_stopTimer();
        L3_action_reset(rcvd_match_success);
        main_state = L3STATE_BROADCASTING;
      } else if (L3_event_checkEventFlag(L3_event_waitPairRcvd) ||
                 L3_event_checkEventFlag(L3_event_recRcvd)) {
        // Event A, B: 예상치 못한 패킷 → 실패 reset → BROADCASTING
        L3_timer_stopTimer();
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      } else if (L3_event_checkEventFlag(L3_event_timeout)) {
        // Event D: MCH 타임아웃 → 실패 reset → BROADCASTING
        L3_timer_stopTimer();
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    default:
      break;
  }
}
