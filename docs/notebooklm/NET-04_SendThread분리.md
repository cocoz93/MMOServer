# 04. SendThread 분리 유무와 이슈정리

출처: 네트워크 라이브러리 설계&구현 (9장 중 4장)
노션: https://app.notion.com/34116a0b9f5980cca5f2e65b5122a6fe
추출: 2026-08-12

> 아래 본문은 최초 설계 시의 결정(워커 직접 Send)이다.
> 이후 **Send가 병목으로 실측되어 SendThread(송신 전용 워커 여러 개(현재 3개) · 세션 고유번호를 워커 수로 나눈 나머지로 담당 워커를 하나 고정)를 분리**한다.
> (참고: 노션 내부 실험 문서 1건이 연결되어 있다 — 이 묶음 밖의 페이지)

## 초기 결정 — Send를 WorkerThread에서 직접 (이후 번복)

**당시 판단 — 워커 직접 Send 채택.** Send 전용 스레드는 구조 확장성·브로드캐스트 제어에 유리하지만, 당시 규모에서는 레이턴시·처리량 이점이 더 큰 워커 직접 호출이 낫다고 보았다.

당시 구조는 **[다수의 I/O WorkerThread - 단일 컨텐츠 Thread 구조]**로, 별도 SendThread를 두지 않음.

| 항목 | 워커 직접 호출 | Send 전용 스레드 |
| --- | --- | --- |
| **동시성 복잡도** | 높음 (다수 워커의 동시 Send 경합) | 낮음 (단일 스레드가 직렬 송신) |
| **송신 지연** | 낮음 (즉시 송신) | 중간 (큐 대기 발생 가능) |
| **처리량 확장성** | 높음 (다수 워커가 병렬 송신) | 낮음 (단일 스레드 병목 가능) |
| **구조 확장성** | 낮음 (송신 정책 변경 시 워커 전체 수정) | 높음 (SendThread만 수정하면 됨) |

※ 이 표는 **단일 SendThread를 가정한 판단**이다. 실제 채택안은 송신 워커를 여러 개(현재 3개) 두는 풀이라, 표의 "처리량 확장성 낮음"과 "동시성 복잡도 = 단일 스레드가 직렬 송신" 두 칸은 지금 구조에 해당하지 않는다. 지금은 세션마다 담당 워커가 하나로 고정돼 있어, 워커를 늘려도 한 세션이 보내는 순서는 그대로 지켜진다.

(분리 쪽 논거) 따로 SendThread를 분리하면 WorkerThread는 보낼 거리만 넘기면 끝이라, 워커끼리 겹치는 문제를 고민할 일이 없다.
WorkerThread에서 Send까지하게된다면 RequestSend → PostSend와, Send완료통지 이후에 PostSend를 타는 구문쪽에 문제가 생길 확률이 높다.

**SendThread는 송신흐름 중앙화, Batching용이, 자체 Nagle, 브로드캐스트 제어 등의 여러 장점을 가지고있지만, 당시 규모에서는 이점보다 queue/thread 추가에 따른 context switch·레이턴시 오버헤드가 더 크다고 보고 분리하지 않았다.**

(→ 이 판단은 이후 실측에서 뒤집혔다. 송신이 틱의 74%를 차지했고, 분리 후 tick p99가 −56% 감소했다.)

---

## Send쪽 동기화 이슈

### 1. SendQ 자체에 문제있는 경우

LockFreeQ에 데이터가 남아있는데도 Dequeue결과가 0으로 나온 경우
SendQ에 lock을 잘못걸었거나, 걸었다고해도 빈틈이 있는 경우
(GetPtr을 가져온다음, DirectSize를 가져오는 등의 행위는 단일 동작은 안전하지만 그사이에 큐의 상황이 변경되었을 수 있음)

### 2. 서버쪽 SendQ에 잔여데이터가 남는 상황

| 순서 | Thread A | Thread B | 비고 |
| --- | --- | --- | --- |
| 1 | send 완료통지가 온 상황 | | |
| 2 | | Send요청으로 SendQ에 Enqueue | |
| 3 | | flag를 true로 바꾸고 send하려했으나, 이미 이전값이 true이므로 send하지않음 | |
| 4 | 송신처리 후 flag 는 true → false | | |
| **5** | **SendQ에 남은데이터는 보내지지않음** | **SendQ에 남은데이터는 보내지지않음** | |

**해결방법: 완료통지 이후에 SendQ의 사이즈를 체크하고 PostSend(실제로 송신을 거는 함수)로 진입한다.**

단, 단순히 큐 체크 후 진입하면 두 스레드가 동시에 PostSend에 진입하는 double-entry race가 발생할 수 있다.

올바른 패턴:

```
[Send 완료 통지]
  1. 큐에서 보낸 만큼 제거
  2. 큐에 남은 데이터가 있으면 → flag 유지(true), 바로 WSASend
  3. 큐가 비었으면 → flag = false
  4. flag를 false로 내린 직후, 큐 재확인
  5. 큐에 데이터가 생겼으면 → CAS(false→true) 성공 시 WSASend
  5-1. CAS 실패 시 → 다른 스레드가 이미 send 소유권을 획득한 상태이므로, 아무것도 안 함 (안전)
```

핵심은 flag가 false인 순간이 생기면 반드시 큐를 한번 더 확인하고, 재진입은 반드시 CAS(값이 예상대로일 때만 바꾸는 원자 연산)로 하여, 한 소켓에 송신이 언제나 하나만 걸려 있도록 보장하는 것.

### 3. WSASend의 크기를 0으로 하는 상황

**[GetUseSize()]를 체크하고 [SendFlag = True]를 하면 문제가 된다.**

SendFlag를 먼저 잡으면(인터락 함수) 정작 보낼 데이터가 없을 때 그 비용이 헛돈다고 보아, 처음에는 사이즈 체크를 앞에 두었다. 하지만 이 경우 send 0을 하는 경우가 나온다.

| 순서 | Thread A | Thread B |
| --- | --- | --- |
| **1** | Send후, 완료통지 온 상황. sendingflag = false; | |
| **2** | SendQ의 사이즈가 0이아니므로 PostSend진입, sendingflag = true 직전 (예정) | |
| **3** | | Send요청을 받고 PostSend까지 진입 |
| **4** | | 이후 완료통지까지 받고 sendingflag 를 false로 바꾼다. |
| **5** | sendingflag = true로 바꾸고 Send진행 | |
| **6** | WSASend요청으로 크기가 0 | |

**해결: 사이즈 체크보다 플래그 변경처리가 우선되어야 함**

동일 소켓에 WSASend가 2개 이상 동시에 걸리면 송신 순서가 보장되지 않으며, 크기 0의 WSASend는 정상 완료되더라도 불필요한 커널 진입과 완료통지를 유발한다. 반드시 CAS로 flag를 먼저 획득한 뒤 사이즈를 체크해야 한다.

### 4. SendFlag가 한 스레드만 진입하는 구조를 놓친 경우

이때 SendFlag를 인터락함수를 사용하지않고 일반대입(=)하면, **x86에서 유일하게 허용되는 store-load reordering**으로 인해 로직순서가 뒤바뀌게 된다.

```
InterlockedExchange(&_sending, FALSE);   // STORE (+ full barrier)
if (_sendQ.GetDataSize() > 0)            // LOAD
```

이 코드에서 InterlockedExchange 대신 `_sending = FALSE` (일반 대입)을 쓰면:

| 순서 | Thread A (완료 통지) | Thread B (새 Send 요청) | 비고 |
| --- | --- | --- | --- |
| 1 | _sending = FALSE (store buffer에 대기, 아직 다른 코어에 안 보임) | | |
| 2 | GetDataSize() 먼저 실행 → 0 (비어있음) | | store-load reorder |
| 3 | | SendQ에 Enqueue | |
| 4 | | _sending 읽기 → TRUE (A의 store가 아직 안 보임!) | |
| 5 | | "이미 전송중" 판단, PostSend 안 함 | |
| 6 | _sending = FALSE가 이제야 flush | | |
| **7** | **큐에 데이터 있는데 아무도 안 보냄!** | **큐에 데이터 있는데 아무도 안 보냄!** | |

InterlockedExchange는 full memory barrier로서 store buffer를 즉시 flush하여, STORE가 다른 코어에 보인 후에만 LOAD가 실행되도록 보장한다.
(보통 atomic변수는 규칙에 맞게 store, load, exchange등의 함수를 사용해야하지만, 성능을 조금이나마 줄여보기 위해 시도했었음)

참고: `volatile`을 사용하더라도 memory barrier가 아니므로 동일한 store-load reordering 문제가 발생한다. volatile은 컴파일러 최적화(레지스터 캐싱)만 방지할 뿐, CPU의 store buffer flush를 보장하지 않는다.
