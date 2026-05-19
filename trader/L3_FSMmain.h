#pragma once
#include "mbed.h"

// FSM 초기화 
void L3_initFSM(uint8_t myId, uint8_t coordId, uint8_t isSeller,
                uint8_t goods, uint16_t price);
void L3_FSMrun(void);
