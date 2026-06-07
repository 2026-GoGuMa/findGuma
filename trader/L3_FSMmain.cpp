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

// TXN 전송: coordinator 형식 [id, isSeller, goods, price] (signal 필드 없음)
static void L3_action_sendTxn(void) {
  uint8_t size =
      L3_msg_buildTxn(sdu, myId, coordId, seq_num++, isSeller, goods, price);
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

// --- 메시지 파싱 ---
// L3_event_msgRcvd 발생 시 호출: PDU 타입 확인 → 세부 이벤트 세팅
// REC는 coordinator가 가격·위치를 별도 전송하므로 FSM 현재 상태로 구분해 저장
static void L3_parse_msg(void) {
  uint8_t* dataPtr = L3_LLI_getMsgPtr();
  uint8_t type = L3_msg_getPduType(dataPtr);

  switch (type) {
    case L3_MSG_TYPE_WAIT_PAIR:
      L3_event_setEventFlag(L3_event_waitPairRcvd);
      break;

    case L3_MSG_TYPE_REC: {
      uint16_t val = L3_msg_parseRec(dataPtr);  // 2바이트 uint16_t 한 값
      if (main_state == L3STATE_WAIT_PRICE_REC) {
        rcvd_avg_price = val;
      } else {
        rcvd_avg_loc = val;
      }
      L3_event_setEventFlag(L3_event_recRcvd);
      break;
    }

    case L3_MSG_TYPE_MCH:
      rcvd_match_success = L3_msg_parseMch(dataPtr);
      L3_event_setEventFlag(L3_event_mchRcvd);
      break;

    default:
      break;
  }
  L3_event_clearEventFlag(L3_event_msgRcvd);
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
  if (prev_state != main_state) {
    debug_if(DBGMSG_L3, "[L3] state %i -> %i\n", prev_state, main_state);
    prev_state = main_state;
  }

  // 메시지 수신이면 타입 분류 먼저 처리
  if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
    L3_parse_msg();
  }

  switch (main_state) {
    // WAIT_PAIR 기다리는 중
    case L3STATE_BROADCASTING:
      if (L3_event_checkEventFlag(L3_event_waitPairRcvd)) {
        // Event A: WAIT_PAIR 수신 → TXN 전송 + 타이머 시작 → WAIT_PRICE_REC
        L3_action_sendTxn();
        L3_timer_startTimer(L3_PAIR_TIMEOUT);
        L3_event_clearEventFlag(L3_event_waitPairRcvd);
        main_state = L3STATE_WAIT_PRICE_REC;
      } else if (L3_event_checkEventFlag(L3_event_recRcvd) ||
                 L3_event_checkEventFlag(L3_event_mchRcvd)) {
        // Event B, C: 이전 라운드 잔여 패킷 → TXN 재전송, 상태 유지
        L3_action_sendTxn();
        L3_event_clearEventFlag(L3_event_recRcvd);
        L3_event_clearEventFlag(L3_event_mchRcvd);
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
