// #pragma once
#include "mbed.h"

static void L3_action_sendTxn(void);
static void L3_action_sendPriceCnf(uint8_t accept);
static void L3_action_sendLocCnf(uint8_t accept);
static void L3_action_reset(uint8_t success);
static uint8_t L3_getNextSeqNum(void);

// FSM 초기화
void L3_initFSM(uint8_t myId, uint8_t coordId, uint8_t isSeller, uint8_t goods,
                uint16_t price);
void L3_FSMrun(void);
