#define DBGMSG_L2 0  // L2 디버그 출력 제어 (1: 활성화)
#define DBGMSG_L3 0  // L3 디버그 출력 제어 (1: 활성화)

#define L3_MAXDATASIZE 1024    // L3 데이터 크기 (바이트)

#define L2_ARQ_MAXRETRANSMISSION 10   // ARQ 최대 재전송 횟수
#define L2_ARQ_MAXWAITTIME 5    // ARQ 최대 대기 시간(초)
#define L2_ARQ_MINWAITTIME 2    // ARQ 최소 대기 시간(초)
#define L1_FREQCHANNEL 1        // 주파수 채널(x 1MHz)

#define L3_PAIR_TIMEOUT    5   // 페어링 응답 대기 타임아웃(초)
#define L3_MCH_TIMEOUT     5   // 매칭 결과 대기 타임아웃(초)

#define L3_COORDINATOR_ID  0   // 코디네이터 노드 ID (고정값)
