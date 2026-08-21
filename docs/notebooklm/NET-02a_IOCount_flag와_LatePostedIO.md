# 02a. IOCount에 flag가 필요한 이유(_disconnecting)와 Late-posted IO Race

출처: 네트워크 라이브러리 설계&구현 (9장 중 2장 — 두 편으로 나눈 것 중 1편)
노션: https://app.notion.com/34116a0b9f59805ca586d7c8c3597545
추출: 2026-08-12

> 앞 장(1장) 요약: 세션은 배열로 관리하고, SessionID `[16bit index][48bit uniqueId]`로 세대를 구분한다. IOCount는 세션을 쓰는 동안 해제되지 않게 붙잡는 참조 수다.

## 문제: CAS 없이 InterlockedIncrement를 쓰면?

> [그림: deck_06_cas]

`_disconnecting` 확인과 `InterlockedIncrement` 사이에 틈이 있다.

**가장 단순한 이중 해제 시나리오 (배열 자리 재사용이 끼지 않은 경우)**

```
Thread A (AcquireSession)         Thread B (Disconnect)
───────────────────────       ─────────────────────────
_disconnecting check → FALSE ✓
                                  _disconnecting = TRUE
                                  IOCount 1→0 → ReleaseSession → Close(), Push(index)
InterlockedIncrement → IOCount 0→1  ← 좀비 참조!
sessionId 확인 → 불일치 or 사용 후
IOCountDecrement → IOCount 1→0
→ ** ReleaseSession 재실행 = 이중 해제!
```

Thread A가 **이미 해제된 세션**의 IOCount를 0→1로 올린다. 되돌릴 때 1→0이 되면서 **ReleaseSession이 두 번 실행**된다.

**재할당 끼어들면 더 심각**

```
Thread A (AcquireSession)         내부
───────────────────────       ─────────────────────────
_disconnecting check → FALSE ✓
                                  IOCount 1→0 → ReleaseSession, Push(index)
                                  AcceptThread: Pop(index), Initialize
                                  → 새 세션, IOCount=1, _disconnecting=FALSE
InterlockedIncrement → IOCount 1→2 (새 세션에 눌러)
sessionId 불일치 → IOCountDecrement → IOCount 2→1
→ 새 세션의 IOCount가 1이라 정상 작동하지만, 운이 좋았을 뿐
```

이 경우 3단계 sessionId 검증이 잡아주지만, **재할당 타이밍에 의존**하는 것이라 안전하지 않다.

## 해결: CAS

```
InterlockedCompareExchange(&_ioCount, current + 1, current)
→ current == 0이면 CAS 실패 → 0→1 원천 차단
```

CAS는 "현재값이 0이면 아예 건드리지 않는다"를 **원자적으로** 보장한다. 확인과 증가 사이에 틈이 없으므로 좀비 참조 자체가 불가능하다.

## CAS와 _disconnecting flag의 역할 구분

두 메커니즘은 **서로 다른 문제**를 해결한다. 둘 다 필요하다.

| 메커니즘 | 방어 대상 | 시나리오 |
| --- | --- | --- |
| **CAS (0→1 차단)** | 이미 해제된 세션의 좀비 참조 | IOCount==0인 세션에 새 스레드가 접근 |
| **_disconnecting flag** | 종료 중인 세션에 새 IO 게시 | IOCount>0이지만 disconnect 진행 중, 다른 스레드가 AcquireSession 시도 |

---

## Late-posted IO Race (Recv/Send post-check)

> [그림: deck_07_late_io]

### 문제

IO를 걸기 전 "종료 중인가"를 확인하고 실제로 거는 사이에, 다른 스레드가 종료를 시작해 CancelIoEx까지 끝내버릴 수 있다. 그러면 취소가 지나간 뒤에 IO가 걸리므로, 이 IO만 취소 대상에서 빠진다.
완료통지가 온 이후에는 당연히 _disconnecting 을 체크하여 더이상의 I/O를 걸지 않으므로 문제가 없지만, 문제는 그 Recv에 완료 통지가 영영 안 오는 경우다. 세션이 종료 단계로 넘어가지 못한 채 그대로 남는다.

```
Thread A (PostRecv)               Thread B (Disconnect)
─────────────────                 ──────────────────────
_disconnecting check → FALSE
                                  _disconnecting = TRUE
                                  CancelIoEx() ← 아직 안 걸린 IO는 취소 대상 없음
WSARecv() posted
← 이 IO는 CancelIoEx 이후라 취소 안 됨
```

클라가 패킷을 안 보내면 (크래시, 네트워크 단절, 모바일 백그라운드 등) 이 Recv는 **영구 pending → IOCount가 0에 도달 불가 → 세션 영구 누수**.

### 해결

IO 제출 **직후** `_disconnecting`를 다시 확인하고, true면 **방금 건 IO만** 취소한다.

```
WSARecv(..., &ex->overlapped, ...);

if (session->_disconnecting == TRUE)
    CancelIoEx((HANDLE)socket, &ex->overlapped);  // 방금 건 IO만 취소
```

- **pre-check**: 이미 닫히는 세션의 IO 게시 차단
- **post-check**: pre-check 이후 끼어든 disconnect race 회수
- 이미 완료된 IO에 CancelIoEx 호출해도 부작용 없음 (에러 반환만)
- 정상 경로 비용: `volatile LONG` read 1회 (앞선 WSARecv가 커널로 들어가는 함수 호출이라 컴파일러도 CPU도 그 뒤의 읽기를 앞으로 당기지 못한다 — 안전한 이유는 이것이다. 순서를 코드로 직접 보장하려면 이 자리는 저장→적재 순서라 acquire가 아니라 전체 장벽이 필요하다.)

---

## 개인적인 소감 — atomic은 싼가

→ atomic 연산 단건 비용은 syscall·네트워크 대기에 비하면 작다. 처음엔 빈도가 높아 성능을 갉을 거라 봤지만, IOCP syscall 비용에 묻히는 수준이었다.

**다만 최적화가 진행될수록 이 전제가 뒤집혔다.** 비용을 정하는 건 단건 값이 아니라 **빈도 × 경합**이고, 큰 항(syscall·복사)을 걷어내고 나면 atomic이 다음 병목으로 올라온다. 이 프로젝트에서 실제로 두 번 걸렸다.

- **팬아웃의 AddRef** — 멤버십 브로드캐스트에서 수신자 1명마다 `InterlockedIncrement64`를 돌리던 것을 **배치 AddRef 1회**(`InterlockedExchangeAdd64`)로 압축했다. 섹터 밀도가 오를수록 이 호출이 곱으로 늘어난다.
- **SendQ 락프리 전환** — 뮤텍스 링버퍼를 락프리 큐로 바꿨더니 오히려 **10% 저하**. CAS 재시도 경합이 뮤텍스보다 비쌌다. 락프리 큐·스택에 지수 백오프(`CASBackoff`)를 둔 것도 같은 이유다.

즉 "atomic은 싸다"가 아니라 **"경합 없는 atomic이 싸다"**가 정확하다. 아래 표는 **무경합 기준의 통상 인용치**이며, 코어 간 경합 시 수백 사이클까지 벌어진다. syscall 값도 CPU 세대·Spectre/Meltdown 완화 적용 여부에 따라 크게 갈린다.

| 작업 유형 | 소요 시간 (Cycles) | 비고 |
| --- | --- | --- |
| **Atomic 연산** | ~20 - 100 | 멀티코어 간 동기화 필요 |
| **커널 모드 전환 (syscall)** | ~1,000 - 2,000 | 유저 모드와 커널 모드 간 Context Switch 발생 |
| **네트워크 I/O 대기** | ~10,000 - 100,000+ | 네트워크 카드 및 물리적 거리 영향 |

### 시도해본 방식 (폐기) — Overlapped I/O풀 + SessionID 비교

Per-Input/Output (매 요청 마다) Overlapped I/O 풀을 할당받아서 사용하는 방식.
Session과 Overlapped 구조체가 같이 묶여있다면 재할당되어도 구분할 수 있는 방법이 없음.
따라서 Overlapped은 풀을 활용해 IO할때마다 새로 할당받고, 검증시에는 현재 SessionID와 Overlapped에 (IO요청시 저장해두었던 SessionID)로 해결한다.

**하지만 이 방식은 완료통지가 왔을 때 재할당은 구분할 수 있지만 결국 IO직전에 재할당 되어버리면 구분할 방법이 없다. 따라서 폐기**

**ABA문제 해결원리**

- IO시마다 현재 할당된 SessionID를 Overlapped에 저장해둔다.
- 실제 IO요청 전 / 완료통지 이후에 저장해둔 ID와 현재 ID를 비교 검증한다.
- IO 제출 직전 — WSABUF 준비 ~ WSASend 사이에 세션이 재사용되어, 구 데이터가 신 소켓으로 전송되는 것
- IO 완료 시점 — 구 세션의 완료 통지가 신 세션의 로직에서 처리되는 것

**성능**

- 풀 할당/반환 오버헤드 (실측이 아니라 통상 인용치 — 락프리 풀이면 ~10-30 cycles)
- IO 수만큼 풀 사이즈 사전 확보 필요
- 캐시: 풀 메모리가 분산되어 cache miss 가능성 있음

**이 방식을 시도할 때 쓰려던 것 (지금 코드에는 없음)**

- 락프리 프리리스트 활용해서 Alloc / Free (CInternalFreeList\<OverlappedEx\>)

---

*이어지는 내용: RST로 즉시 종료 (2b장).*
