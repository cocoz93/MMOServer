# 01. AcceptThread 분리와 세션 할당·ABA 방어

출처: 네트워크 라이브러리 설계&구현 (9장 중 1장 + 2장 앞부분)
노션: https://app.notion.com/34116a0b9f59802eaf47c3ff8e15e082 · https://app.notion.com/34116a0b9f59805ca586d7c8c3597545
추출: 2026-08-12

> **요약(1): accept를 IOCP 워커에서 분리, 전용 스레드 1개(AcceptThread)를 둔다.**
> (AcceptEx는 일관된 설계방향이지만 병목이 아니고 실익이 미미)
>
> **요약(2): 접속한 세션은 배열에서 자리를 꺼내 쓴다. 배열 자리를 다시 쓸 때 생기는 혼동(ABA)은 접속마다 오르는 세대 번호로 가려내고, IOCount는 쓰는 동안 세션이 사라지지 않게 붙잡는다.**

> [그림: deck_01_cover]

## AcceptEx를 쓰지 않은 이유

> [그림: deck_02_claim]

- AcceptEx가 모든 I/O를 IOCP로 통합하는 점에서 아키텍처적으로 더 일관된 설계.
- AcceptEx 도입 시 소켓 풀링 등 부수 작업 대비 실익이 없어서 현재 구조를 유지. 필요 시 전환 가능.

> [그림: deck_03_structure]

> [그림: deck_04_decision]

- accept은 주된 병목지점이 아님 — 접속은 세션당 한 번뿐이라 호출 빈도가 낮다. 접속이 가장 몰리는 부하 테스트 램프업에서도 초당 수백 건이다(부하 클라 스레드마다 10ms에 하나씩, `RampUpIntervalMs=10`)
- AcceptEx는 accept를 IOCP 워커 풀로 통합하고 여러 개를 동시에 대기시켜 순간 대량 접속(burst)을 병렬 흡수하는 데 유리하지만, 개별 accept 지연 자체가 빨라지는 것은 아님

### 상세 표 — Accept 스레드 vs AcceptEx

| 항목 | Accept 스레드 | AcceptEx |
| --- | --- | --- |
| **스레드 개수** | 별도 1개 필요 | 워커 스레드 공유 |
| **멀티코어 활용** | ❌ 단일 스레드 (설계 선택) | ✅ 워커 풀 분산 |
| **병렬 accept** | ❌ 순차 처리 | ✅ 동시 처리 (N개 pending) |
| **첫 데이터 수신** | ❌ 별도 Recv 필요 | ✅ Accept와 동시 수신 가능 |
| **코드 복잡도** | 단순 | 복잡 |
| **메모리 사용** | 낮음 (온디맨드 할당) | 중간 (미리 소켓 생성) |
| **사전 준비** | 불필요 (온디맨드) | 사전 준비 필요 (소켓 풀 + OVERLAPPED) |
| **디버깅 난이도** | 쉬움 | 어려움 |
| **리소스 정리** | 간단 (리슨 소켓을 닫아 accept를 깨우고 join) | 복잡 (pending 취소) |
| **DoS 방어** | 데이터를 안 보내고 붙어만 있는 소켓이 쌓일 위험 없음 (접속만 하고 노는 세션은 60초 무활동 타임아웃으로 정리) | 첫 데이터까지 같이 받게 설정하면(dwReceiveDataLength > 0) 데이터를 안 보내는 소켓이 무한 대기로 남는다 (SO_CONNECT_TIME으로 접속 경과시간을 재서 끊어야 함) |

(참고: 노션 내부 문서 2건이 연결되어 있다 — 이 묶음 밖의 페이지)

---

## Session 객체 관리 방식

AcceptThread가 연결을 받으면 세션 자리를 꺼내 쓴다. 그 자리를 어떻게 관리하는지가 아래 내용이다.

> [그림: deck_02_structure]

Session은 배열 컨테이너로 관리.

- 최초 서버 기동시 동접자만큼 확보
- 클라이언트가 접속하면 할당 (SOCKET을 Session이 포함)

이때 재할당(배열) 방식이기 때문에 ABA문제가 존재 → SessionID를 부여/증가시킨다. 이 관리방식으로 컨텐츠쪽에서 요청오는 세션이 유효한지도 체크가능 (컨텐츠는 SessionID만 알고 Session객체와는 분리됨)

- **16-bit index = 최대 65,535 동시 세션.** 일반적인 게임서버 동접 규모에서 충분하며, 필요 시 index를 24-bit로 늘려 약 1,600만 세션까지 받을 수 있다. 다만 SessionID는 64비트 고정이라 그만큼 아래쪽 세대 번호가 48→40비트로 줄고, 아래 891년 계산도 함께 짧아진다.
- **Index**: 세션 배열에서 O(1)로 검색하기위한 인덱스 번호
- **uniqueId(세대 번호)**: 접속할 때마다 1씩 오르는 값으로, SessionID의 아래 48비트를 차지한다. 약 281조 — 초당 1만 접속이 쉼없이 이어져도 고갈까지 약 891년 (사실상 재사용 걱정 없음)

---

## 세션 ABA 방어

> [그림: deck_03_problem]

**문제** — 세션 배열은 index로 재사용된다. 해제된 index에 새 세션이 할당되면, **예전 sessionId를 들고 있던 스레드가 새 세션을 조작**할 수 있다.

**해결 — SessionID 구조**: `[16bit index][48bit uniqueId]` — index로 배열 접근, uniqueId로 세대 구분.

**외부 진입점 3단계 검증** (`RequestSendMsg`, `RequestDisconnectSession(sessionId)`):

```
1) FindSession     — index로 세션 조회 + sessionId 일치 확인 (잠정적)
2) AcquireSession  — IOCount(그 세션에 걸려 있는 미완료 I/O 개수)를 CAS로 +1. 이미 0이면 실패 = 정리가 끝난 세션
3) sessionId 재확인 — 1~2 사이 재할당 검출
```

**내부 경로** (WorkerThread → ProcessRecv/ProcessSend):
완료 통지를 꺼낼 때(GetQueuedCompletionStatus) 통지에 함께 실려 오는 값이 곧 세션 주소라 바로 쓴다. 해당 IO의 IOCount ref가 아직 살아있으므로 세션이 해제될 수 없다.

> [그림: deck_04_defense]

---

## 소켓 ABA 방어

> [그림: deck_05_socket]

**문제** — OS가 `SOCKET` 핸들 값을 재사용할 수 있다. 이전 소켓과 동일한 핸들이 새 연결에 할당되면, **stale handle로 잘못된 소켓에 IO**를 걸 수 있다.

**해결** — `closesocket`은 `ReleaseSession`(IOCount==0)에서**만** 호출된다.

```
소켓을 사용하는 모든 코드 (PostRecv, PostSend)
  → AcquireSession으로 IOCount pin
  → IOCount >= 1인 동안 closesocket 불가
  → 소켓 핸들이 사용 중에 닫힐 수 없음
```

pending IO가 모두 완료되어 IOCount가 0이 된 후에만 소켓이 닫히므로, **새 연결이 같은 핸들을 받아도 겹치지 않는다**.

---

*이어지는 내용: IOCount에 flag가 추가로 필요한 이유와 CAS (2a장), RST 즉시 종료 (2b장).*
