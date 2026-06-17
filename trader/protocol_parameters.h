#define DBGMSG_L2 0  // L2 디버그 출력 제어 (1: 활성화)
#define DBGMSG_L3 1  // L3 디버그 출력 제어 (1: 활성화)

#define L3_MAXDATASIZE 1024  // L3 데이터 크기 (바이트)

#define L2_ARQ_MAXRETRANSMISSION 10  // ARQ 최대 재전송 횟수
#define L2_ARQ_MAXWAITTIME 5         // ARQ 최대 대기 시간(초)
#define L2_ARQ_MINWAITTIME 2         // ARQ 최소 대기 시간(초)
#define L1_FREQCHANNEL 1             // 주파수 채널(x 1MHz)

#define L3_PAIR_TIMEOUT 40  // 페어링 응답 대기 타임아웃(초)
#define L3_MCH_TIMEOUT 40   // 매칭 결과 대기 타임아웃(초)

#define L3_COORDINATOR_ID 0  // 코디네이터 노드 ID (고정값)
#define L3_MIN_RSSI -60      // 임의 설정

// 중간 위치 임의 설정(숙명여대 눈꽃광장홀)
#define AVG_LOC 4312  // 04312, 우편번호는 636***까지 표현 가능

#define L3_MAX_SEQNUM 256