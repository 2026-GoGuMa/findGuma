#include "mbed.h"

extern void (*L3_LLI_dataReqFunc)(uint8_t* msg, uint8_t size, uint8_t destId);
extern void (*L3_LLI_reconfigSrcIdReqFunc)(uint8_t myId);

void L3_LLI_dataInd(uint8_t* dataPtr, uint8_t srcId, uint8_t size, int8_t snr,
                    int16_t rssi);
uint8_t* L3_LLI_getMsgPtr();  // 수신된 메시지 내용 포인터 반환
uint8_t L3_LLI_getSize();     // 수신된 메시지 길이 반환
uint8_t L3_LLI_getSrcId();    // 송신 노드 ID 반환
int16_t L3_LLI_getRssi();     // 수신 신호 세기 반환
void L3_LLI_setDataReqFunc(void (*funcPtr)(uint8_t*, uint8_t, uint8_t));
void L3_LLI_setReconfigSrcIdReqFunc(void (*funcPtr)(uint8_t));
void L3_LLI_dataCnf(uint8_t res);           // LL→L3 전송 완료 알림
void L3_LLI_reconfigSrcIdCnf(uint8_t res);  // LL→L3 SrcId 재설정 완료 알림
