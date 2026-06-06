#include "L2_FSMmain.h"
#include "L3_FSMmain.h"
#include "L3_FSMevent.h"
#include "mbed.h"
#include "protocol_parameters.h"
#include "string.h"

// ?쒕━???ы듃 (PC? ?듭떊??
Serial pc(USBTX, USBRX);

// ?꾩뿭 蹂??(?섏젙 湲덉?) ------------------------------------------
uint8_t input_thisId  = 1;  // ???몃뱶??ID

// ?꾨줈洹몃옩 ?쒖옉??------------------------------------------------
// 시리얼 인터럽트 콜백: 사용자가 1 또는 0 입력 시 이벤트 세팅
void onSerialRx(void) {
  char c = pc.getc();
  if (c == '1') L3_event_setEventFlag(L3_event_userAccept);
  else if (c == '0') L3_event_setEventFlag(L3_event_userReject);
}

int main(void) {
  pc.printf(
      "------------------ protocol stack starts! --------------------------\n");

  // ?ъ슜???낅젰 諛쏄린
  pc.printf(":: ID for this node : ");
  pc.scanf("%d", &input_thisId);

  uint8_t  input_isSeller = 0;  // ?먮ℓ???щ? (0: 援щℓ?? 1: ?먮ℓ??
  uint8_t  input_goods    = 0;  // ?곹뭹 醫낅쪟
  uint16_t input_price    = 0;  // ?щ쭩 媛寃?

  pc.printf(":: isSeller (0=buyer / 1=seller) : ");
  pc.scanf("%d", &input_isSeller);

  pc.printf(":: goods type : ");
  pc.scanf("%d", &input_goods);

  pc.printf(":: price ($) : ");
  pc.scanf("%d", &input_price);

  pc.getc();

  // ?낅젰媛??뺤씤 異쒕젰
  pc.printf("Trader id=%u  coord=%u  isSeller=%u  goods=%u  price=$%u\n",
             input_thisId, L3_COORDINATOR_ID, input_isSeller,
             input_goods, input_price);

  // FSM 珥덇린??
  L2_initFSM(input_thisId);
  L3_initFSM(input_thisId, L3_COORDINATOR_ID, input_isSeller,
              input_goods, input_price);

  pc.attach(&onSerialRx, Serial::RxIrq);  // 시리얼 인터럽트 등록

  // 硫붿씤 猷⑦봽 : L2 ??L3 ?쒖꽌濡?諛섎났 ?ㅽ뻾
  while (1) {
    L2_FSMrun();   // L2 FSM (臾댁꽑 ?≪닔??泥섎━)
    L3_FSMrun();   // L3 FSM (嫄곕옒 ?묒긽 泥섎━)
  }
}
