#include "L3_FSMevent.h"
#include "L3_msg.h"
#include "mbed.h"
#include "protocol_parameters.h"
#include "time.h"

// 수신된 패킷을 임시 보관하는 버퍼
static uint8_t rcvdMsg[L3_MAXDATASIZE];  // 패킷 내용 (바이트 배열)
static uint8_t rcvdSize;                 // 패킷 크기
static int16_t rcvdRssi;                 // 수신 신호 세기 (RSSI)
static int8_t  rcvdSnr;                  // 신호 대 잡음비 (SNR)
static uint8_t rcvdSrcId;               // 패킷을 보낸 노드의 ID

// 하향 프리미티브 (L3 → L2 방향 함수 포인터)
void (*L3_LLI_dataReqFunc)(uint8_t* msg, uint8_t size, uint8_t destId);       // 데이터 전송 요청
void (*L3_LLI_reconfigSrcIdReqFunc)(uint8_t myId);                            // SrcId 재설정 요청

// L2 → L3 수신 콜백: 패킷이 도착하면 L2가 이 함수를 호출
void L3_LLI_dataInd(uint8_t* dataPtr, uint8_t srcId, uint8_t size, int8_t snr,
                    int16_t rssi) {
  //debug_if(DBGMSG_L3, "\n[L3] --> DATA IND : size:%i, %s\n", size, dataPtr);

  memcpy(rcvdMsg, dataPtr, size * sizeof(uint8_t));  // 수신 버퍼에 패킷 복사
  rcvdSize  = size;
  rcvdSnr   = snr;
  rcvdRssi  = rssi;
  rcvdSrcId = srcId;

  L3_event_setEventFlag(L3_event_msgRcvd);  // FSM에 메시지 수신 이벤트 알림
}

// L2 → L3 송신 완료 콜백: 데이터 전송이 끝나면 L2가 이 함수를 호출
void L3_LLI_dataCnf(uint8_t res) {
  // debug_if(DBGMSG_L3, "\n --> DATA CNF : res : %i\n", res);
  L3_event_setEventFlag(L3_event_dataSendCnf);
}

// L2 → L3 SrcId 재설정 완료 콜백
void L3_LLI_reconfigSrcIdCnf(uint8_t res) {
  //debug_if(DBGMSG_L3, "\n --> RECONFIG SRCID CNF : res : %i\n", res);
  L3_event_setEventFlag(L3_event_recfgSrcIdCnf);
}

// 수신 버퍼 접근 함수
uint8_t* L3_LLI_getMsgPtr()  { return rcvdMsg;   }  // 패킷 내용 포인터 반환
uint8_t  L3_LLI_getSize()    { return rcvdSize;   }  // 패킷 크기 반환
uint8_t  L3_LLI_getSrcId()   { return rcvdSrcId;  }  // 송신 노드 ID 반환
int16_t  L3_LLI_getRssi()    { return rcvdRssi;   }  // 수신 신호 세기 반환

// 함수 포인터 등록 (main.cpp에서 L2 함수와 연결)
void L3_LLI_setDataReqFunc(void (*funcPtr)(uint8_t*, uint8_t, uint8_t)) {
  L3_LLI_dataReqFunc = funcPtr;
}

void L3_LLI_setReconfigSrcIdReqFunc(void (*funcPtr)(uint8_t)) {
  L3_LLI_reconfigSrcIdReqFunc = funcPtr;
}
