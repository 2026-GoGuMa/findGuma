# 🍠 찾았구마 (FindGuma)

> FSM 기반 P2P 거래 매칭 프로토콜 — Trader / Coordinator 이중 노드 구조

---

## 📌 프로젝트 개요

**찾았구마**는 거래자(Trader)와 중개자(Coordinator)가 분리된 이중 FSM(Finite State Machine) 구조를 기반으로 동작하는 P2P 거래 매칭 프로토콜입니다.  
각 노드는 독립적인 상태 머신을 가지며, PDU(Protocol Data Unit)를 통해 통신합니다.

---

## 🗂️ 프로젝트 구조

```
BASECODE_CAPSTONE/
├── coordinator/   # 중개자 노드
└── trader/        # 거래자 노드
```

---

## 🔁 FSM 설계

### Coordinator Node FSM

| 상태 | 설명 |
|------|------|
| `IDLE` | 주문 모드 / LOC PDU를 대기하는 상태 |
| `WAIT_PAIR` | 거래 매칭을 감지하여 페어링을 기다리는 상태 |
| `WAIT_PRICE_CNF` | 상품의 거래 가격 수락을 기다리는 상태 |
| `WAIT_LOC_CNF` | 결정된 위치의 수락을 기다리는 상태 |

**이벤트**

| 이벤트 | 설명 |
|--------|------|
| A | TXN PDU 수신 |
| B | CNF PDU 수신 |
| C | PAIR timeout |
| D | CNF 송신 timeout |

**주요 액션**

- Data Store: 수신 정보(id, Seller\_A, Goods\_A) 저장
- Data Update: 재시도 리스트에 거래자 A·B 정보 저장
- Send REC: avg\_price 혹은 avg\_loc 정보 송신
- Send MCH: match\_success/fail 정보 송신
- Send WAIT\_PAIR: 매칭 대기 Trader 노드에 알림 송신
- Reset: 재시도 리스트를 재사용 임시데이터로 비우기

**조건**

| 조건 | 내용 |
|------|------|
| c1 | signal >= min\_sig |
| c2 | 양쪽 상대방 판단 (isSeller\_A != isSeller\_B == 1, Goods\_A == Goods\_B == 1) |
| c3 | 요청 수락 (CNF\_A & CNF\_B == 1) |
| c4 | 재시도 횟수 판단 (rtylist[id\_A\*id\_B] < max\_rty(3)) |
| c5 | 초과 처리 (rtylist[id\_A\*id\_B] == 0) |

---

### Trader Node FSM

| 상태 | 설명 |
|------|------|
| `BROADCASTING` | 대기 상태, 거래 미시 발생 시 TXN PDU 브로드캐스팅 |
| `WAIT_PRICE_REC` | Coordinator로부터 매칭 여부인 REC PDU(avg\_price)를 기다리는 상태 |
| `WAIT_LOC_REC` | Coordinator로부터 REC PDU(avg\_location)를 기다리는 상태 |
| `WAIT_LOC_MCH` | CNF PDU 송신 후 Coordinator로부터 REC PDU(avg\_location) 혹은 MCH PDU(match\_fail)를 기다리는 상태 |

**이벤트**

| 이벤트 | 설명 |
|--------|------|
| A | Coordinator로부터 WAIT\_PAIR 수신 |
| B | REC PDU 수신 (Coordinator로부터 추천 페어 도착) |
| C | MCH PDU 수신 (match\_success / match\_fail 페어 결과 도착) |
| D | Timeout (각 단계에서 응답이 오지 않을 때 타이머 만료) |

**주요 액션**

1. Send TXN: 희망 물품 및 거래 데이터 송신
2. Send CNF: 제안에 대한 수락 응답 송신
3. Timer 시작
4. Timer 정지
5. Reset / Notify: 메모리 정리 및 사용자에게 결과 리포트

---

## 📦 PDU 설계

### PDU 구조

| TYPE | SEQ NUM | SRC ID | DST ID | Data (Payload) |
|------|---------|--------|--------|----------------|

### Coordinator PDU

| 타입 | 설명 |
|------|------|
| `MCH` | 매치 성사 여부 (1bit): `match_success(1)` \| `match_fail(0)` |
| `REC` | 추천 정보 — 가격 및 위치 (16byte): `avg_price` \| `avg_loc` |
| `WAIT_PAIR` | 페어를 기다리고 있는 상태 알림. Payload: None |

### Trader PDU

| 타입 | 설명 |
|------|------|
| `TXN` | 거래 정보: `id`, `isSeller`, `goods`, `price` |
| `CNF` | 제안 수락 여부: `price_cnf` \| `loc_cnf` |

> `REC_P`, `REC_L`은 모두 `REC` PDU에 종속된 하위 PDU로 정의합니다.

---

## 📝 변수 설명

| 변수 | 설명 |
|------|------|
| `rtylist` | 거래자 ID의 고유합을 인덱스로 하는 재시도 횟수 저장 리스트 |
| `isSeller` | 판매자 여부 (boolean) |
| `goods` | 상품 정보 |

---

## ⚙️ 기타 사항

- 블랙리스트(재시도 불가) 유지 기간 `T_BLACKLIST`는 설계 복잡도를 줄이기 위해 현재 버전에서 제외되었습니다.

---

## 🌿 브랜치 종류 및 규칙

| 브랜치 | 용도 | 설명 |
|--------|------|------|
| `main` | 배포용 | 항상 안정적인 상태 유지. 배포 시 이 브랜치 기준으로 진행. **직접 작업 금지** |
| `dev` | 개발 통합 | 각 기능 브랜치를 이 브랜치로 merge. 팀원 PR 후 코드리뷰 → merge 권장. 리뷰 지연 시 자기 책임 하에 직접 merge 가능 |
| `feat/{이슈번호}-{설명}` | 기능 개발 | 새로운 기능 개발 시 사용. 예: `feature/#12-login-api` |
| `fix/{이슈번호}-{설명}` | 버그 수정 | 발견된 버그 수정용 브랜치 |
| `hotfix/{이슈번호}-{설명}` | 긴급 수정 | 배포 후 발생한 긴급 이슈 처리 시 사용 |
| `refactor/{이슈번호}-{설명}` | 리팩토링 | 로직 변경 없이 코드 구조 개선 목적 |
| `chore/{이슈번호}-{설명}` | 설정/환경 | 빌드 설정, 패키지 설치 등 부수 작업 시 사용 |

- ✅ 기능 개발이 완료되면 `develop` 브랜치로 Pull Request → Merge
- ✅ merge 완료된 브랜치는 즉시 삭제 권장

---

## 🖥️ 커밋 컨벤션

| 태그 | 설명 |
|------|------|
| `feat` | 새로운 기능 추가 |
| `fix` | 버그 수정 |
| `hotfix` | 급한 버그/이슈 패치 |
| `refactor` | 코드 리팩토링 (기능 변화 없음) |
| `add` | 부가적인 코드/라이브러리/파일 추가 |
| `del` | 불필요한 코드/파일 삭제 |
| `docs` | 문서 작업 (README, Wiki 등) |
| `chore` | 환경 설정, 빌드 작업 등 기타 잡일 |
| `correct` | 오타, 타입 수정 등 |
| `move` | 코드/파일 위치 이동 |
| `rename` | 파일/변수/함수 이름 변경 |
| `improve` | 성능/UX 개선 |
| `test` | 테스트 코드 작성/수정 |

```
형식: 태그: #{이슈번호} 한국어 메시지
예시: feat: #12 거래자 FSM 상태 전이 구현
```
