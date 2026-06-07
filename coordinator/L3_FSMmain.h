#include "L3_msg.h"
#include "mbed.h"

static uint8_t L3_signalConditionPassed(int16_t rssi);
static uint8_t L3_pairConditionPassed(L3_txnInfo_t* firstTxn,
                                      L3_txnInfo_t* secondTxn);
static uint8_t L3_getTraderPairRetryCnt(uint8_t pendingTxn_id,
                                        uint8_t matchingTxn_id);
static uint8_t L3_getNextSeqNum(void);
static void L3_storeTxn(L3_txnInfo_t* txnInfo);
static void L3_sendRecPrice(uint8_t traderId, uint16_t avg_price);
static void L3_sendRecLoc(uint8_t traderId);
static void L3_sendMch(uint8_t traderId, uint8_t accept);
static void L3_sendWaitPair(uint8_t traderId);
static void L3_resetAll();

void L3_initFSM(void);
void L3_FSMrun(void);
