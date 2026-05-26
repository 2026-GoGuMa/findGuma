#include "L2_FSMmain.h"
#include "L3_FSMmain.h"
#include "mbed.h"
#include "protocol_parameters.h"
#include "string.h"

// 시리얼 포트 (PC와 통신용)
Serial pc(USBTX, USBRX);

// 전역 변수 (수정 금지) ------------------------------------------
uint8_t input_thisId  = 1;  // 이 노드의 ID

// 프로그램 시작점 ------------------------------------------------
int main(void) {
  pc.printf(
      "------------------ protocol stack starts! --------------------------\n");

  // 사용자 입력 받기
  pc.printf(":: ID for this node : ");
  pc.scanf("%d", &input_thisId);

  uint8_t  input_isSeller = 0;  // 판매자 여부 (0: 구매자, 1: 판매자)
  uint8_t  input_goods    = 0;  // 상품 종류
  uint16_t input_price    = 0;  // 희망 가격

  pc.printf(":: isSeller (0=buyer / 1=seller) : ");
  pc.scanf("%d", &input_isSeller);

  pc.printf(":: goods type : ");
  pc.scanf("%d", &input_goods);

  pc.printf(":: price : ");
  pc.scanf("%d", &input_price);

  pc.getc();

  // 입력값 확인 출력
  pc.printf("Trader id=%u  coord=%u  isSeller=%u  goods=%u  price=%u\n",
             input_thisId, L3_COORDINATOR_ID, input_isSeller,
             input_goods, input_price);

  // FSM 초기화
  L2_initFSM(input_thisId);
  L3_initFSM(input_thisId, L3_COORDINATOR_ID, input_isSeller,
              input_goods, input_price);

  // 메인 루프 : L2 → L3 순서로 반복 실행
  while (1) {
    L2_FSMrun();   // L2 FSM (무선 송수신 처리)
    L3_FSMrun();   // L3 FSM (거래 협상 처리)
  }
}
