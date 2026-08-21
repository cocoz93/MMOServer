# 05a. Timing Wheel — 왜 쓰는가, 소유권 분리, ABA 방지

출처: 네트워크 라이브러리 설계&구현 (9장 중 5장 — 두 편으로 나눈 것 중 1편)
노션: https://app.notion.com/35816a0b9f59804db9bff9d6679696bc
추출: 2026-08-12

> **요약: IOCP 게임서버에서 수만 세션의 무활동 타임아웃을 O(1)로 처리하는 타이밍 휠.**
> 외부 스레드는 Lock-Free 큐로 요청만 push하고, 타이머 스레드가 단독으로 휠을 소유한다 — 락 없이 동작하는 단일 Writer 구조.

## 왜 타이밍 휠인가

> [그림: 01_scan_vs_wheel]

세션 타임아웃의 가장 단순한 구현은 **전체 세션 선형 스캔**이다. 매 tick마다 `now - lastRecvTime > timeout`을 비교하면 된다. 동접 1만이면 1만 번 비교 — 수십 μs로, 구현이 매우 간단하다.
단, 이 방식은 **세션 수에 비례하여 비용이 증가**한다. 타이밍 휠은 이 문제를 해결한다.

| 방식 | Tick 복잡도 | Insert/Cancel | 추가 자료구조 |
| --- | --- | --- | --- |
| 선형 스캔 | O(N) — 전체 세션 | O(1) | 없음 |
| **타이밍 휠** (현재 구현) | **O(K)** — K=해당 슬롯 만료 수, 전체 N 대비 amortized O(1) | O(1) | 슬롯 배열 + 이중 연결 리스트 |

타이밍 휠은 **고정 duration 타이머가 대량으로 존재하는 상황**에 최적화된 자료구조다.
세션 타임아웃, Rate Limit, 도배 방지 등 "N초 후 만료"가 일괄 적용되는 모든 곳에 동일하게 적용 가능하다.

> **이득은 속도가 아니라 확장성.** 지금 부하 테스트 규모(동접 수천)에선 선형 스캔도 수십 μs라 "몇 ms 빨라졌다" 실측은 무의미(둘 다 바닥). 휠의 가치는 **tick당 O(N) 스캔을 Recv당 O(1)로 분산**한 것 — 세션이 10만·100만으로 커져도 비용이 세션 수에 비례해 늘지 않는다.

---

## 아키텍처

핵심은 **소유권 분리**다.

- **외부 스레드**는 휠을 직접 조작하지 않는다. Lock-Free 큐에 요청(Register=접속할 때 / Refresh=패킷을 받을 때 / Unregister=세션이 끝날 때)을 push할 뿐이다.
- **타이머 스레드**가 큐를 drain한 뒤 휠을 조작하고, cursor를 전진시켜 만료 슬롯을 처리한다.

휠 자료구조에 접근하는 스레드가 하나뿐이므로, mutex·spinlock 없이도 thread-safe하다.

---

## 동기화 상세

| 대상 | 접근 주체 | 동기화 |
| --- | --- | --- |
| 휠 (슬롯, 노드, 커서) | 타이머 스레드 단독 | 불필요 (단일 Writer) |
| 요청 큐 | 워커/Accept → 타이머 | Lock-Free Queue (MPSC) |
| _running 플래그 | Start/Stop ↔ 타이머 | std::atomic (acquire/release) |
| 타임아웃 콜백 | Start에서 설정 → 타이머에서 호출 | 스레드 생성 전 설정 (happens-before) |

---

## ABA 방지 — sessionId 매칭

세션 인덱스는 풀에서 재사용된다. 인덱스만으로 식별하면 이전 세션의 stale 요청이 새 세션에 적용될 수 있다. 이를 방지하기 위해 모든 요청에 **sessionId**(단조 증가 ID)를 함께 전달하고, 휠 조작 시 현재 노드의 sessionId와 비교한다.

> [그림: 04_aba_guard]

### 구체적 시나리오 검증

**Case 1 — 타임아웃 후 같은 인덱스에 새 세션이 들어온 경우**

1. Session A (index=5, id=100) 타임아웃 → 노드 제거, slotIndex=-1
2. Session B (index=5, id=101) → Register(5, 101) 큐에 push
3. 이전의 stale Refresh(5, 100)이 아직 큐에 남아있음
4. Drain 시: Refresh → `node.slotIndex == -1` → **무시** ✓
5. Register → sessionId=101로 갱신, 삽입 ✓

**Case 2 — Register가 먼저 처리된 경우**

1. Register(5, 101) 처리 → sessionId=101, 슬롯 삽입
2. stale Refresh(5, 100) 처리 → `node.sessionId=101 ≠ 100` → **무시** ✓

**Case 3 — 큐 순서 역전 (Refresh → Unregister → Register)**

1. Refresh(5, 100): `slotIndex == -1` → 무시 ✓
2. Unregister(5, 100): `slotIndex == -1` → 무시 ✓
3. Register(5, 101): sessionId=101 설정, 삽입 ✓

어떤 순서로 drain되든 새 세션의 수명이 오염되지 않는다.

---

## 활용 범위

현재는 세션 타임아웃 전용이지만, **"고정 duration + 대량 + 개별 콜백 불필요"** 조건을 만족하는 곳에 동일 구조로 적용 가능하다.

- **패킷 송신 제한 (Rate Limiter)** — N초 내 요청 M회 초과 시 차단
- **접속 시도 제한** — 같은 IP의 반복 접속 쿨다운
- **채팅 도배 방지** — 일정 시간 내 메시지 횟수 제한

가변 duration이 필요한 타이머(스킬 쿨다운, 버프 만료, 리스폰 등)는 별도의 **Hierarchical Timing Wheel** 구조가 필요하며, 이 구현과는 설계 목적이 다르다.

---

*이어지는 내용: 휠의 물리적 구조, Tick 동작, 슬롯 수 +1 보정, 해상도 선택 (2편).*
