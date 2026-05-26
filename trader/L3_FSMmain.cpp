#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"

// FSM state 
#define L3STATE_BROADCASTING   0   // 대기중 (WAIT_PAIR 기다리는 중)
#define L3STATE_WAIT_PRICE_REC 1   // TXN 보냄 → 가격 REC 기다리는 중
#define L3STATE_WAIT_LOC_REC   2   // 가격 CNF 보냄 → 위치 REC 기다리는 중
#define L3STATE_WAIT_LOC_MCH   3   // 위치 CNF 보냄 → 매칭 결과 기다리는 중

// state variables
static uint8_t main_state = L3STATE_BROADCASTING;
static uint8_t prev_state = main_state;

// trader 정보
static uint8_t  myId;
static uint8_t  coordId;
static uint8_t  isSeller;
static uint8_t  goods;
static uint16_t price;
static uint8_t  seq_num = 0;

// coordinator로부터 수신한 값 
static uint16_t rcvd_avg_price     = 0;
static uint16_t rcvd_avg_loc       = 0;
static uint8_t  rcvd_match_success = 0;

static uint8_t sdu[L3_MAXDATASIZE];
extern Serial  pc;  // main.cpp에서 선언된 Serial 객체 사용

// actions 
static void action_sendTxn(void) {           // TXN 패킷 빌드 후 전송 
  uint8_t size = L3_msg_buildTxn(sdu, myId, coordId, seq_num++,
                                  0, isSeller, goods, price);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] TXN sent\n");
}

static void action_sendPriceCnf(void) {     // 가격 CNF 패킷 빌드 후 전송
  uint8_t size = L3_msg_buildCnf(sdu, myId, coordId, seq_num++,
                                  rcvd_avg_price, 0);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] price CNF sent: %u\n", rcvd_avg_price);
}

static void action_sendLocCnf(void) {        // 위치 CNF 패킷 빌드 후 전송 
  uint8_t size = L3_msg_buildCnf(sdu, myId, coordId, seq_num++,
                                  0, rcvd_avg_loc);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] loc CNF sent: %u\n", rcvd_avg_loc);
}

static void action_reset(uint8_t success) {   // 변수 초기화 + 결과 출력
  rcvd_avg_price     = 0;
  rcvd_avg_loc       = 0;
  rcvd_match_success = 0;
  L3_event_clearAllEventFlag();
  pc.printf("[Trader] Trade %s\n", success ? "succeeded!" : "failed.");
}

// msgRcvd 이벤트가 발생하면 호출 (수신된 PDU를 분석해 이벤트 플래그 세우는 함수)
static void parse_msg(void) {
  uint8_t* dataPtr = L3_LLI_getMsgPtr();
  uint8_t  type    = L3_msg_getPduType(dataPtr);

  switch (type) {

    // WAIT_PAIR 수신 시
    case L3_MSG_TYPE_WAIT_PAIR:
      L3_event_setEventFlag(L3_event_waitPairRcvd);
      break;

    // REC 수신 시 (파싱 → 값 저장 → 플래그 세팅)
    case L3_MSG_TYPE_REC: {
      rec_payload_t rec  = L3_msg_parseRec(dataPtr);   // 페이로드 파싱
      rcvd_avg_price     = rec.avg_price;              // 전역 변수에 저장
      rcvd_avg_loc       = rec.avg_loc;
      L3_event_setEventFlag(L3_event_recRcvd);
      break;
    }

    // MCH 수신 시
    case L3_MSG_TYPE_MCH: {
      mch_payload_t mch  = L3_msg_parseMch(dataPtr);
      rcvd_match_success = mch.match_success;
      L3_event_setEventFlag(L3_event_mchRcvd);
      break;
    }
    default:
      break;
  }
  L3_event_clearEventFlag(L3_event_msgRcvd);
}

// init / run 
void L3_initFSM(uint8_t id, uint8_t cId, uint8_t seller,
                uint8_t g, uint16_t p) {
  myId     = id;
  coordId  = cId;
  isSeller = seller;
  goods    = g;
  price    = p;
  pc.printf("[Trader] id=%u coord=%u isSeller=%u goods=%u price=%u\n",
             myId, coordId, isSeller, goods, price);
}


void L3_FSMrun(void) {

  // 상태 전이 감지 
  if (prev_state != main_state) {
    debug_if(DBGMSG_L3, "[L3] state %i → %i\n", prev_state, main_state);
    prev_state = main_state;
  }

  // 메시지 수신 확인 
  if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
    parse_msg();     // PDU 분류 → 세부 이벤트 플래그 세팅
  }

  // 상태별 이벤트 처리 
  switch (main_state) {

    // 페어링 대기 중
    case L3STATE_BROADCASTING:
      if (L3_event_checkEventFlag(L3_event_waitPairRcvd)) {

        // Event A: TXN 전송 + PAIR타이머 시작  →  WAIT_PRICE_REC
        action_sendTxn();
        L3_timer_startTimer(L3_PAIR_TIMEOUT);
        L3_event_clearEventFlag(L3_event_waitPairRcvd);
        main_state = L3STATE_WAIT_PRICE_REC;
      } else if (L3_event_checkEventFlag(L3_event_recRcvd) ||
                 L3_event_checkEventFlag(L3_event_mchRcvd)) {

        // Event B, C: (늦게 온 패킷) TXN 재전송, 상태 유지
        action_sendTxn();
        L3_event_clearEventFlag(L3_event_recRcvd);
        L3_event_clearEventFlag(L3_event_mchRcvd);
      }
      break;

    // 가격 평균 기다리는 중 
    case L3STATE_WAIT_PRICE_REC:
      if (L3_event_checkEventFlag(L3_event_recRcvd)) {

        // Event B: 타이머 stop + 가격CNF 전송 + 타이머 재시작  →  WAIT_LOC_REC
        L3_timer_stopTimer();
        action_sendPriceCnf();
        L3_timer_startTimer(L3_PAIR_TIMEOUT);
        L3_event_clearEventFlag(L3_event_recRcvd);
        main_state = L3STATE_WAIT_LOC_REC;
      } else if (L3_event_checkEventFlag(L3_event_mchRcvd) ||
                 L3_event_checkEventFlag(L3_event_timeout)) {

        // Event C or D: 실패 reset  →  BROADCASTING
        L3_timer_stopTimer();
        action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    // 위치 평균 기다리는 중
    case L3STATE_WAIT_LOC_REC:
      if (L3_event_checkEventFlag(L3_event_recRcvd)) {
        // Event B: 타이머 stop + 위치CNF 전송 + MCH타이머 시작  →  WAIT_LOC_MCH
        L3_timer_stopTimer();
        action_sendLocCnf();
        L3_timer_startTimer(L3_MCH_TIMEOUT);
        L3_event_clearEventFlag(L3_event_recRcvd);
        main_state = L3STATE_WAIT_LOC_MCH;
      } else if (L3_event_checkEventFlag(L3_event_mchRcvd) ||
                 L3_event_checkEventFlag(L3_event_timeout)) {
        // Event C (match_fail) or D (timeout): 실패 reset  →  BROADCASTING
        L3_timer_stopTimer();
        action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    // 매칭 결과 기다리는 중
    case L3STATE_WAIT_LOC_MCH:
      if (L3_event_checkEventFlag(L3_event_mchRcvd)) {
        // Event C: MCH 수신 → 성공/실패 출력  →  BROADCASTING
        L3_timer_stopTimer();
        action_reset(rcvd_match_success);
        main_state = L3STATE_BROADCASTING;
      } else if (L3_event_checkEventFlag(L3_event_waitPairRcvd) ||
                 L3_event_checkEventFlag(L3_event_recRcvd)) {
        // Event A, B: (예상 못한 패킷) 실패 reset  →  BROADCASTING
        L3_timer_stopTimer();
        action_reset(0);
        main_state = L3STATE_BROADCASTING;
      } else if (L3_event_checkEventFlag(L3_event_timeout)) {
        // Event D: MCH 타임아웃  →  실패 reset  →  BROADCASTING
        L3_timer_stopTimer();
        action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    default:
      break;
  }
}
