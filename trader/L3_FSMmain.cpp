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

extern Serial pc;

// --- action 함수 ---

// 시퀀스 번호 생성 함수
static uint8_t L3_getNextSeqNum(void) {
  uint8_t seqNum = seq_num;
  seq_num = (seq_num + 1) % L3_MSG_MAX_SEQNUM;
  if (seq_num == 0) seq_num = 1;
  return seqNum;
}

// action 1. TXN 메시지를 만들어 coordinator에게 전송
static void L3_action_sendTxn(void) {
  uint8_t sdu[L3_MSG_TXN_SIZE];
  // TXN 메시지를 sdu 버퍼에 조립
  uint8_t size = L3_msg_encodeTxn(sdu, myId, coordId, L3_getNextSeqNum(),
                                  isSeller, goods, price);
  // 조립된 메시지를 LL 레이어를 통해 coordId 에게 전송
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] TXN sent\n");
}

// action 2-1. 가격 CNF 전송: 사용자 입력값(accept)을 그대로 전달
static void L3_action_sendPriceCnf(uint8_t accept) {
  uint8_t sdu[L3_MSG_CNF_SIZE];
  uint8_t size =
      L3_msg_encodeCnf(sdu, myId, coordId, L3_getNextSeqNum(), accept);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] price CNF sent (accept=%u), avg_price=%u\n", accept,
           rcvd_avg_price);
}

// action 2-2. 위치 CNF 전송: 사용자 입력값(accept)을 그대로 전달
static void L3_action_sendLocCnf(uint8_t accept) {
  uint8_t sdu[L3_MSG_CNF_SIZE];
  uint8_t size =
      L3_msg_encodeCnf(sdu, myId, coordId, L3_getNextSeqNum(), accept);
  L3_LLI_dataReqFunc(sdu, size, coordId);
  debug_if(DBGMSG_L3, "[L3] loc CNF sent (accept=%u), avg_loc=%u\n", accept,
           rcvd_avg_loc);
}

// action 5. 리셋: 수신 값 초기화 + 결과 출력 후 BROADCASTING으로 복귀 준비
static void L3_action_reset(uint8_t success) {
  waiting_price_cnf = 0;
  waiting_loc_cnf = 0;
  rcvd_avg_price = 0;
  rcvd_avg_loc = 0;
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
          pc.printf("[L3] Waiting for pair. . . . . .\n");
          if (srcId == coordId) {
            L3_timer_startTimer(
                L3_PAIR_TIMEOUT);  // 타이머 시작 (TXN 보내고 REC 기다리는 동안)
            main_state = L3STATE_WAIT_PRICE_REC;  // 상태 전환
          }
          debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
        } else {
          // WAIT_PAIR 메시지가 아닌 TXN, CNF 메시지나 알 수 없는 메시지가 오는
          // 경우
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in BROADCASTING state\n");
        }
        L3_event_clearEventFlag(L3_event_msgRcvd);
      }
      L3_action_sendTxn();
      debug_if(DBGMSG_L3, "[L3] Broadcasting . . . . . ");
      break;

    case L3STATE_WAIT_PRICE_REC:
      // Event B. REC 메시지 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        // Coordinator 가 보낸 REC 인지 확인
        if (L3_msg_checkIfRec(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] Price REC received from coordinator");

          // coordinator로 부터 REC가 오면 가격 추출하고 사용자 입력 대기 시작
          if (srcId == coordId) {
            L3_timer_stopTimer();                    // REC 대기 타이머 정지
            rcvd_avg_price = L3_msg_decodeRec(msg);  // 메시지에서 가격 추출
            pc.printf("[L3][Trader] avg_price=%u. Accept? (1=yes / 0=no): ",
                      rcvd_avg_price);
            waiting_price_cnf = 1;  // 사용자 입력 대기 중
          }
          debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
        } else {
          // coordinator 로부터 받은 메시지가 REC 타입이 아닐 경우 (WAIT_PAIR,
          // MCH PDU 또는 알 수 없는 메시지가 온 경우)
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in WAIT_PRICE_REC state\n");
        }
        L3_event_clearEventFlag(
            L3_event_msgRcvd);  // 메시지 수신 이벤트 플래그 끄기
      }

      // Event B(action 2). REC 메시지 수신 후, 제안에 대한 수락/거절 응답 송신
      if (waiting_price_cnf) {
        if (L3_event_checkEventFlag(L3_event_userAccept)) {
          // 사용자가 가격을 수락한 경우
          waiting_price_cnf = 0;      // 사용자 대기 플래그 끄기
          L3_action_sendPriceCnf(1);  // coordinator에게 수락(1) CNF 전송
          L3_event_clearEventFlag(L3_event_userAccept);  // 이벤트 플래그 끄기
          L3_timer_startTimer(L3_PAIR_TIMEOUT);  // 위치 REC가 안 올 경우 대비
          main_state = L3STATE_WAIT_LOC_REC;

        } else if (L3_event_checkEventFlag(L3_event_userReject)) {
          // 사용자가 가격을 거절한 경우
          waiting_price_cnf = 0;
          L3_action_sendPriceCnf(0);
          L3_action_reset(0);
          L3_event_clearEventFlag(L3_event_userReject);
          main_state = L3STATE_BROADCASTING;
        }
      }
      // D. timeout 난 경우
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        pc.printf("No match from coordinator %i... Waiting for pair again",
                  coordId);
        L3_timer_stopTimer();
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    case L3STATE_WAIT_LOC_REC:
      // Event B. REC 메시지 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        // Coordinator가 보낸 REC 인지 확인
        if (L3_msg_checkIfRec(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] Location REC received from coordinator");

          // coordinator로 부터 REC가 오면 위치 추출하고 사용자 입력 대기 시작
          if (srcId == coordId) {
            L3_timer_stopTimer();                  // REC 대기 타이머 정지
            rcvd_avg_loc = L3_msg_decodeRec(msg);  // 메시지에서 위치 추출
            pc.printf("[L3][Trader] avg_loc=%u. Accept? (1=yes / 0=no): ",
                      rcvd_avg_loc);
            waiting_loc_cnf = 1;  // 사용자 입력 대기 중
          }
          debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
        } else if (L3_msg_checkIfMch(msg, size)) {
          // Event C. MCH PDU가 온 경우
          if (srcId == coordId) {
            pc.printf(
                "[L3][Trader] Price negotiation failed! Matching fail, "
                "broadcasting again. . . . . . ");
            L3_timer_stopTimer();
            L3_action_reset(0);
            main_state = L3STATE_BROADCASTING;
          }
        } else {
          // coordinator 로부터 받은 메시지가 REC 타입이 아닐 경우 (WAIT_PAIR
          // 메시지나 알 수 없는 메시지가 온 경우)
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in WAIT_LOC_REC state\n");
        }
        L3_event_clearEventFlag(
            L3_event_msgRcvd);  // 메시지 수신 이벤트 플래그 끄기
      }

      // Event B(action 2). REC 메시지 수신 후, 제안에 대한 수락/거절 응답 송신
      if (waiting_loc_cnf) {
        if (L3_event_checkEventFlag(L3_event_userAccept)) {
          // 사용자가 위치를 수락한 경우
          waiting_loc_cnf = 0;      // 사용자 대기 플래그 끄기
          L3_action_sendLocCnf(1);  // coordinator에게 수락(1) CNF 전송
          L3_event_clearEventFlag(L3_event_userAccept);  // 이벤트 플래그 끄기
          L3_timer_startTimer(L3_MCH_TIMEOUT);
          main_state = L3STATE_WAIT_LOC_MCH;

        } else if (L3_event_checkEventFlag(L3_event_userReject)) {
          // 사용자가 위치를 거절한 경우
          waiting_loc_cnf = 0;
          L3_action_sendLocCnf(0);
          L3_action_reset(0);
          L3_event_clearEventFlag(L3_event_userReject);
          main_state = L3STATE_BROADCASTING;
        }
      }
      // D. timeout 난 경우
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        pc.printf("No match from coordinator %i... Waiting for pair again",
                  coordId);
        L3_timer_stopTimer();
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;

    case L3STATE_WAIT_LOC_MCH:
      // Event C. MCH 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        if (L3_msg_checkIfMch(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] MCH received from %i\n", srcId);
          // coordinator가 보낸 MCH 인지 확인
          if (srcId == coordId) {
            L3_timer_stopTimer();                     // MCH 대기 타이머 정지
            uint8_t success = L3_msg_decodeMch(msg);  // 매칭 성공 여부 추출
            L3_action_reset(success);
            main_state = L3STATE_BROADCASTING;
          }
          debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
        } else {
          // MCH 타입이 아닌 메시지가 온 경우
          debug_if(DBGMSG_L3,
                   "[L3] unknown PDU ignored in WAIT_LOC_MCH state\n");
        }
        L3_event_clearEventFlag(
            L3_event_msgRcvd);  // 메시지 수신 이벤트 플래그 끄기
      }
      // Event D. timeout 난 경우
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        L3_timer_stopTimer();  // MCH 대기 타이머 정지
        L3_action_reset(0);
        main_state = L3STATE_BROADCASTING;
      }
      break;
  }
}
