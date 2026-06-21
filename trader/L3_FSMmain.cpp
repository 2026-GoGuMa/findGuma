#include "L3_FSMevent.h"
#include "L3_LLinterface.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "mbed.h"
#include "protocol_parameters.h"
#include "../ascii_art.h"

// FSM state
#define L3STATE_BROADCASTING 0    // coordinator의 WAIT_PAIR 메시지 기다리는 중
#define L3STATE_WAIT_PRICE_REC 1  // TXN 보냄 → 가격 REC 기다리는 중
#define L3STATE_WAIT_LOC_REC 2    // 가격 CNF 보냄 → 위치 REC 기다리는 중
#define L3STATE_WAIT_LOC_MCH 3    // 위치 CNF 보냄 → 매칭 결과 기다리는 중

#define L3_BROADCAST_ID 255

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

// serial port interface
extern Serial pc;

// 로그 관련
static uint8_t log_loop_cnt = 100000;

// --- action 함수 ---

// 시퀀스 번호 생성 함수
static uint8_t L3_getNextSeqNum(void) {
  seq_num = (seq_num + 1) % L3_MAX_SEQNUM;
  return seq_num;
}

// action 1. TXN 메시지를 만들어 coordinator에게 전송
static void L3_action_sendTxn(void) {
  uint8_t sdu[L3_MSG_TXN_SIZE];
  // TXN 메시지를 sdu 버퍼에 조립
  uint8_t size = L3_msg_encodeTxn(sdu, myId, L3_BROADCAST_ID, seq_num, isSeller,
                                  goods, price);
  // 조립된 메시지를 LL 레이어를 통해 broadcasting - 모두에게 전송
  L3_LLI_dataReqFunc(sdu, size, L3_BROADCAST_ID);
  // debug_if(DBGMSG_L3, "[L3] TXN sent\n");
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
  debug_if(DBGMSG_L3, "[L3] loc CNF sent (accept=%u), avg_loc=%05u\n", accept,
           rcvd_avg_loc);
}

// action 5. 리셋: 수신 값 초기화 + 결과 출력 후 BROADCASTING으로 복귀 준비
static void L3_action_reset(uint8_t success) {
  waiting_price_cnf = 0;
  waiting_loc_cnf = 0;
  rcvd_avg_price = 0;
  rcvd_avg_loc = 0;
  L3_timer_stopTimer();
  L3_event_clearAllEventFlag();
  log_box(&pc, "[Trader] Trade %s", success ? "succeeded!" : "failed.");
}

// --- init / run ---

void L3_initFSM(uint8_t id, uint8_t cId, uint8_t seller, uint8_t g,
                uint16_t p) {
  myId = id;
  coordId = cId;
  isSeller = seller;
  goods = g;
  price = p;
  log_box(&pc, "[Trader] id=%u coord=%u isSeller=%u goods=%u price=$%u",
          myId, coordId, isSeller, goods, price);
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
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();  // 메시지 보낸 노드 ID
        uint8_t* msg = L3_LLI_getMsgPtr();  // 메시지 내용
        uint8_t size = L3_LLI_getSize();    // 메시지 길이
        // 정적 변수로 카운터 선언 (함수가 끝나도 값이 유지됨)
        static uint32_t ignore_log_cnt = 0;

        // 10만 번 루프 돌 때 1번만 로그 출력 (숫자는 속도에 맞게 조절)
        if (ignore_log_cnt++ % 100000 == 0) {
          debug_if(DBGMSG_L3,
                   "[L3] msg received in broadcasting state, from: %i\n",
                   srcId);
        }

        // Event A. coordinator로부터 WAIT_PAIR 를 수신했을 때
        if (L3_msg_checkIfWaitPair(msg, size)) {
          debug_if(DBGMSG_L3, "[L3] Wait Pair arrived from %i \n", srcId);
          if (srcId == coordId) {
            log_box(&pc, "Currently waiting for pair. . . . . . ");
            L3_timer_startTimer(
                L3_PAIR_TIMEOUT);  // 타이머 시작 (TXN 보내고 REC 기다리는 동안)
            main_state = L3STATE_WAIT_PRICE_REC;  // 상태 전환
          } else {
            debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
          }
        } else {
          // WAIT_PAIR 메시지가 아닌 TXN, CNF 메시지나 알 수 없는 메시지가 오는
          // 경우
          // debug_if(DBGMSG_L3,
          //          "[L3] unknown PDU ignored in BROADCASTING state\n");
        }
        L3_event_clearEventFlag(L3_event_msgRcvd);
      } else {
        // 정적 변수로 카운터 선언 (함수가 끝나도 값이 유지됨)
        static uint32_t ignore_log_cnt = 0;

        // 500만 번 루프 돌 때 1번만 로그 출력 (숫자는 속도에 맞게 조절)
        if (ignore_log_cnt++ % 5000000 == 0) {
          debug_if(DBGMSG_L3, "[L3] Broadcasting . . . . , 현재 SEQ_NUM: %i\n",
                   seq_num);
          pc.printf("중개자에게 거래 정보를 전송하는 중입니다. . . . .");
          L3_action_sendTxn();
        }
      }
      // 발생할 리 없는 Timeout이 발생했을 때
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        debug_if(DBGMSG_L3,
                 "[L3] invalid timeout occurred in BROADCASTING state\n");
        L3_event_clearEventFlag(L3_event_timeout);
      }
      break;

    case L3STATE_WAIT_PRICE_REC:
      // Event B. REC 메시지 수신했을 때
      if (L3_event_checkEventFlag(L3_event_msgRcvd)) {
        uint8_t srcId = L3_LLI_getSrcId();
        uint8_t* msg = L3_LLI_getMsgPtr();
        uint8_t size = L3_LLI_getSize();

        // Coordinator 가 보낸 REC 인지 확인
        if (L3_msg_checkIfRec(msg, size)) {
          // coordinator로 부터 REC가 오면 가격 추출하고 사용자 입력 대기 시작
          if (srcId == coordId) {
            debug_if(DBGMSG_L3, "[L3] Price REC received from coordinator");
            L3_timer_stopTimer();                    // 1번 REC 대기 타이머 정지
            rcvd_avg_price = L3_msg_decodeRec(msg);  // 메시지에서 가격 추출
            pc.printf(
                "중개 가격: %u$. 수락하시겠습니까?: \n"
                "\n======================\n"
                " > 수락 = 1\n"
                " > 거절 = 0\n"
                "\n======================\n",
                rcvd_avg_price);
            waiting_price_cnf = 1;  // 사용자 입력 대기 중
          } else {
            debug_if(DBGMSG_L3, "[L3] Price REC Not from coordinator: %i \n",
                     srcId);
          }
        } else if (L3_msg_checkIfMch(msg, size)) {
          // Event C. 중개자 측에서 페어 매칭 타임아웃 발생 알림 (중개자-거래자
          // 상태 Sync를 위함)
          if (srcId == coordId) {
            L3_action_reset(-1);
            main_state = L3STATE_BROADCASTING;
          }
        } else {
          // coordinator 로부터 받은 메시지가 REC 타입이 아닐 경우 (WAIT_PAIR,
          // MCH PDU 또는 알 수 없는 메시지가 온 경우)
          // debug_if(DBGMSG_L3,
          //          "[L3] unknown PDU ignored in WAIT_PRICE_REC state\n");
        }
        L3_event_clearEventFlag(
            L3_event_msgRcvd);  // 메시지 수신 이벤트 플래그 끄기
      }

      // Event B(action 2). REC 메시지 수신 후, 제안에 대한 수락/거절 응답 송신
      if (waiting_price_cnf) {
        if (L3_event_checkEventFlag(L3_event_userAccept) ||
            L3_event_checkEventFlag(L3_event_userReject)) {
          // 수락 이벤트가 켜져 있으면 1, 아니면 0
          uint8_t accept_flag =
              L3_event_checkEventFlag(L3_event_userAccept) ? 1 : 0;

          debug_if(DBGMSG_L3, "[L3] coord으로 CNF 전송 완료");
          L3_action_sendPriceCnf(accept_flag);

          waiting_price_cnf = 0;  // 사용자 대기 플래그 끄기
          L3_event_clearEventFlag(L3_event_userAccept);
          L3_event_clearEventFlag(L3_event_userReject);

          L3_timer_startTimer(L3_REC_TIMEOUT);  // 2번 위치 REC 타이머 작동
          main_state = L3STATE_WAIT_LOC_REC;
        }
      }

      // D. 자체 페어 매칭 timeout 난 경우
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        log_box(&pc, "No match from coordinator %i... Waiting for pair again", coordId);
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
            L3_timer_stopTimer();                  // 2번 REC 대기 타이머 정지
            rcvd_avg_loc = L3_msg_decodeRec(msg);  // 메시지에서 위치 추출
            pc.printf(
                "거래자와 만날 중간 위치는 우편번호 %05u 입니다. 거래를 "
                "수락하시겠습니까?"
                "\n======================\n"
                " > 수락 = 1\n"
                " > 거절 = 0\n"
                "\n======================\n",
                rcvd_avg_loc);
            waiting_loc_cnf = 1;  // 사용자 입력 대기 중
          } else {
            debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
          }
        } else if (L3_msg_checkIfMch(msg, size)) {
          // Event C. MCH PDU가 온 경우
          if (srcId == coordId) {
            log_box(&pc, "[L3][Trader] Price negotiation failed! Matching fail, broadcasting again. . . . . . ");
            L3_timer_stopTimer();
            L3_action_reset(0);
            main_state = L3STATE_BROADCASTING;
          }
        } else {
          // coordinator 로부터 받은 메시지가 REC 타입이 아닐 경우 (WAIT_PAIR
          // 메시지나 알 수 없는 메시지가 온 경우)
          // debug_if(DBGMSG_L3,
          //          "[L3] unknown PDU ignored in WAIT_LOC_REC state\n");
        }
        L3_event_clearEventFlag(
            L3_event_msgRcvd);  // 메시지 수신 이벤트 플래그 끄기
      }

      // Event B(action 2). REC 메시지 수신 후, 제안에 대한 수락/거절 응답 송신
      if (waiting_loc_cnf) {
        if (L3_event_checkEventFlag(L3_event_userAccept) ||
            L3_event_checkEventFlag(L3_event_userReject)) {
          // 수락 이벤트가 켜져 있으면 1, 아니면 0
          uint8_t accept_flag =
              L3_event_checkEventFlag(L3_event_userAccept) ? 1 : 0;

          debug_if(DBGMSG_L3, "[L3] coord으로 CNF 전송 완료");
          L3_action_sendLocCnf(accept_flag);

          waiting_loc_cnf = 0;  // 사용자 대기 플래그 끄기
          L3_event_clearEventFlag(L3_event_userAccept);
          L3_event_clearEventFlag(L3_event_userReject);

          L3_timer_startTimer(L3_MCH_TIMEOUT);  // 3번 MCH 대기 타이머 동작
          main_state = L3STATE_WAIT_LOC_MCH;
        }
      }
      // D. timeout 난 경우
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        log_box(&pc, "No match from coordinator %i... Waiting for pair again", coordId);
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
            L3_timer_stopTimer();                     // 3번 MCH 타이머 정지
            uint8_t success = L3_msg_decodeMch(msg);  // 매칭 성공 여부 추출
            L3_action_reset(success);
            main_state = L3STATE_BROADCASTING;
          } else {
            debug_if(DBGMSG_L3, "[L3] Not from coordinator: %i \n", srcId);
          }
        } else {
          // MCH 타입이 아닌 메시지가 온 경우
          // debug_if(DBGMSG_L3,
          //          "[L3] unknown PDU ignored in WAIT_LOC_MCH state\n");
        }
        L3_event_clearEventFlag(
            L3_event_msgRcvd);  // 메시지 수신 이벤트 플래그 끄기
      }
      // Event D. timeout 난 경우
      if (L3_event_checkEventFlag(L3_event_timeout)) {
        L3_action_reset(-1);
        main_state = L3STATE_BROADCASTING;
      }
      break;
  }
}
