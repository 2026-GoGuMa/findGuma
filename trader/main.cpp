#include "L2_FSMmain.h"
#include "L3_FSMevent.h"
#include "L3_FSMmain.h"
#include "mbed.h"
#include "protocol_parameters.h"
#include "string.h"

// serial port interface
Serial pc(USBTX, USBRX);

uint8_t input_thisId = 1;  // 초기화

// 시리얼 인터럽트 콜백: 사용자가 1 또는 0 입력 시 이벤트 세팅
void onSerialRx(void) {
  char c = pc.getc();
  if (c == '1')
    L3_event_setEventFlag(L3_event_userAccept);
  else if (c == '0')
    L3_event_setEventFlag(L3_event_userReject);
}

int main(void) {
  pc.printf(
      "------------------ protocol stack starts! --------------------------\n");

  // 아이디 세팅
  pc.printf(":: 거래자 ID 설정 : ");
  pc.scanf("%d", &input_thisId);

  uint8_t input_isSeller = 0;
  uint8_t input_goods = 0;
  uint16_t input_price = 0;
  int temp_input = 0;

  pc.printf(":: 구매자/판매자 설정 (0=buyer / 1=seller) : ");
  pc.scanf("%d", &input_isSeller);

  while (1) {
    pc.printf(
        "=========== 상품 품목 종류 ===========\n"
        " > %i. %s\n"
        " > %i. %s\n"
        " > %i. %s\n"
        "====================================\n",
        SWTPOTATO, goods_name[SWTPOTATO], POTATO, goods_name[POTATO], CORN,
        goods_name[CORN]);
    pc.printf(":: 품목 번호 (1, 2, 3 중 선택) : ");
    pc.scanf("%d", &temp_input);
    if (temp_input > 3 || temp_input < 1) {
      pc.printf("유효하지 않은 품목 번호입니다. 다시 선택해주세요.\n\n");
    } else {
      input_goods = (uint8_t)temp_input;
      break;
    }
  }

  pc.printf(":: 제안 가격 (65536$ 이하 입력) : ");
  pc.scanf("%d", &input_price);

  pc.getc();

  // FSM 초기화
  L2_initFSM(input_thisId, L3_COORDINATOR_ID);
  L3_initFSM(input_thisId, L3_COORDINATOR_ID, input_isSeller, input_goods,
             input_price);

  pc.attach(&onSerialRx, Serial::RxIrq);  // 시리얼 인터럽트 등록

  while (1) {
    L2_FSMrun();  // L2 FSM
    L3_FSMrun();  // L3 FSM
  }
}
