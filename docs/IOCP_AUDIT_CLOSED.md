# IOCP_Server 결함 감사 — 종료 선언

**닫은 날짜**: 2026-07-26
**대상 커밋**: `e24c245` 이후 (이 문서와 같은 커밋에서 8건 수정)

이 문서는 "IOCP 서버에 결함이 더 없나"라는 질문을 **닫기 위한** 기록이다.
아래 범위 × 아래 렌즈로 전수 감사했고, 남긴 것은 각각 **안 하는 이유**를 적었다.
이후의 감사 요청은 이 문서의 "재개 조건" 또는 "새 diff 한정"으로만 받는다.

---

## 1. 닫은 범위

`IOCP_Server/IOCP_Server/` + `Shared/` 의 자체 소스 전량 (32파일, 10,443줄).

BuildConfig.h, Common.h, ServerConfig.h, main.cpp, IOCPServer.h/.cpp, GameServer.h/.cpp,
Player.h/.cpp, MapManager.h/.cpp, SectorManager.h/.cpp, Zone.h/.cpp, SerialBuffer.h/.cpp,
TimingWheel.h, MonitorManager.h, MonitorServer.h, DB/DBWorker.h/.cpp, CoreAffinity.h/.cpp,
Crash/CrashDump.h, RioApi.h, Shared/RingBuffer.h, Shared/Protocol/Protocol.h,
Shared/Common/ErrorLog.h, Shared/Common/Logger.h/.cpp

**범위 밖 (이 문서가 닫지 않음)**
- `ThirdParty/` (spdlog·httplib·fmt) — 외부 코드
- `LockFree/` 4파일의 **내부 구현** — 별도 트랙에서 작업 중. 사용하는 쪽 코드는 감사함
- `GameClient/`, `StressTest/`, `RIO/`, `WebClient/`, `Monitoring/`

## 2. 사용한 렌즈 6개

감사가 매번 새 지적을 낳는 이유는 **볼 때마다 각도가 달라서**다. 그래서 각도를 미리
전부 열거하고 한 번에 소진했다. 이 6개 밖의 새 각도가 아니라면 이미 본 것이다.

1. **동시성·수명** — 세션 수명/pin·ABA, 송신 1-pending 전이, 락 규율, 종료 순서
2. **프로토콜·파싱** — 조작 클라 가정. 길이 신뢰, 경계 검사, 정수 승격, 인덱스 유입
3. **자원 정합** — 획득/반환 짝, refcount 수지, 소켓·타이머·게이지 짝
4. **과한 코드** — 도달불가 방어, 죽은 분기, 불필요 동기화, 미사용 심볼
5. **성능** — 핫패스 낭비, false sharing, 캐시 지역성, 틱 내 복잡도
6. **실패 경로** — WSA 즉시 실패 롤백, 부분 초기화, INI 이상값, DB 장애, 셧다운

## 3. 판정 기준 (사전 고정)

| 등급 | 조건 | 처리 |
|---|---|---|
| **확정** | 코드 경로만으로 실패 시나리오가 완결 | 고침 |
| **관측대기** | 논리는 성립하나 실측·재현이 없음 | **안 고침**, 이 문서에 기록 |
| **기각** | 검증 가능한 근거 없음 | 기록도 안 함 |

실측 근거 없는 성능 지적은 전부 관측대기다. "가능성"을 할 일처럼 쌓지 않는다.

---

## 4. 고친 것 (확정 8건)

### 4-1. 같은 존 재입장 시 좌표 재추첨 — 실제 버그

`GameServer.cpp` `RecvZoneChange` 자동배정 분기

채널 지정 분기(`targetChannelIndex >= 0`)는 `newZone == oldZone`을 **reason 2로 거부**하는데,
자동배정 분기(`-1`)에는 같은 검사가 없었다. 현재 맵을 `targetMapId`로 지정하면
`FindOrCreateChannel`이 여유 있는 기본 채널(= 지금 있는 존)을 그대로 돌려주고,
그대로 `LeaveZone → EnterZone`이 돌면서 `CalcSpawnPos`가 좌표를 **맵 전역 난수로 재추첨**한다.

- 결과: 요청 한 번에 맵 어디로든 순간이동. `MOVE_BUDGET`(이동량 예산)을 통째로 우회한다
- 부수: 요청마다 주변 9섹터에 DELETE + CREATE×2 팬아웃
- 현 INI(`MaxPlayersPerChannel=10000` > `MaxClients=6000`)에서는 채널이 만원이 될 수 없어
  같은 맵 요청은 **항상** 이 경우에 걸린다
- 조작 클라뿐 아니라 정상 클라의 `/map 0`(GameClient `SendZoneChange(id)`)로도 발동

→ 자동배정 분기에도 같은 존 검사를 추가.

### 4-2 ~ 4-8. 도달불가 방어 7곳 → assert 강등

커밋 `e24c245`가 4곳을 강등하면서 **같은 함수의 형제 검사들을 남겼다**. 그 잔여를 정리했다.
릴리즈에서 죽은 분기가 사라지고, 불변식이 깨지면 디버그에서 먼저 터진다.

| 위치 | 왜 도달불가인가 |
|---|---|
| `IOCPServer.cpp` `WorkerThread` 널 검사 | overlapped 널을 위에서 이미 걸렀고, completionKey는 `_sessions[i].get()`(비널)로만 등록. 널 키를 만드는 유일한 곳(셧다운 PQCS)은 overlapped=nullptr이라 위 분기에 걸림 |
| `ProcessRecv` `bytesTransferred == 0` | 호출자 2곳(WorkerThread·RioDrainCompletions)의 `canProcess`가 둘 다 `bytes != 0` 검사. 값 파라미터라 이후 변동 없음 |
| `ProcessRecv` `MoveWritePtr` 실패 | PostRecv 게시 총합 = 게시 시점 freeSize와 정확히 일치. recv 1-pending + 소비도 같은 완료 체인 → freeSize 불변 |
| `PostRecv` `bufCount == 0` | 파싱 후 잔여는 불완전 패킷 조각뿐(< 1458), 링 용량 65535. disconnect로 끝났으면 AcquireSession이 먼저 실패 |
| `ParsePackets` `Peek` 실패 | 1번 검사가 `dataSize >= headerSize`(2 또는 4) 보장, 요청은 2바이트. 무락 단일 접근이라 중간 감소 없음 |
| `ParsePackets` `Dequeue` 실패 | 5번 검사가 `dataSize >= totalPacketSize` 확인, 이후 소비 주체 없음. 목적지는 Alloc 직후 버퍼 |
| `GameServer.cpp` `OnReceived` `pMsg` 널 | RECEIVED 생산자는 ParsePackets 한 곳, 그 버퍼는 직전에 역참조된 Alloc 결과 |

**주의**: Peek·Dequeue·MoveWritePtr은 **부작용이 있는 호출**이다. 호출을 `assert()` 안에
넣으면 릴리즈에서 식째로 사라져 동작이 깨진다. 반드시 호출은 밖, 결과만 assert.

#### 강등의 부작용 (자가검증에서 확인한 것)

**`mmo_recv_buffer_overflow_total`은 현 빌드에서 구조적으로 0이 된다.**
이 카운터를 올리던 곳은 세 군데였는데 두 곳(`ProcessRecv`의 `MoveWritePtr` 실패,
`PostRecv`의 `bufCount == 0`)이 이번에 assert로 바뀌었다. 남은 하나는 `RioPostRecv`이고
그건 `#if USE_RIO_TRANSPORT`(현재 0) 안이라 컴파일되지 않는다.
→ **IOCP 빌드에서 증가 지점이 0개다.** 원래도 도달불가라 값은 0이었지만, 이제는 구조적으로
0이므로 대시보드에서 "감시 중"으로 읽으면 안 된다. 링 용량이나 `MAX_PACKET_SIZE`를 바꿔
불변식을 흔들면 이 메트릭이 아니라 **디버그 빌드의 assert**가 알려준다.

**RIO 팔은 일부러 손대지 않았다.**
`RioPostRecv`의 `directWriteSize == 0` 검사는 IOCP 팔의 `bufCount == 0`과 같은 부류(같은
논증으로 도달불가)지만, `USE_RIO_TRANSPORT=0`이라 **컴파일도 빌드 검증도 되지 않는다.**
검증할 수 없는 코드를 함께 고치는 것이 더 위험하다고 판단해 남겼다.
RIO를 채택하게 되면 그때 같은 패턴으로 정리할 것.

**검증**: Release x64 + Debug x64 양쪽 빌드 성공(`_DEBUG`/`NDEBUG` 분리 확인 — Debug에서
assert 활성, Release에서 소멸). assert 실패 시 `CrashDump.h`의 전역 인스턴스
(`inline CCrashDump CrashDump;`, `IOCPServer.cpp`가 include)가 SIGABRT를 받아 덤프를 남긴다.
**런타임 스모크는 미실시** — 이번 변경의 유일한 검증 공백이다(아래 9장).

---

## 5. 남긴 것 (관측대기 — 의도적으로 안 고침)

### 5-1. 채팅 유량 제한 부재 → 송신 링 초과 시 이웃까지 절단
서버에 **수신 빈도 제한이 없다**. 한 클라가 한 틱에 최대 길이 채팅을 몰아넣으면 틱 끝
digest 연접이 세션 송신 링(65535B)을 넘고, all-or-nothing Enqueue가 실패해 그 섹터
주민 **전원**이 `RequestDisconnectSession`으로 끊긴다(가해자가 아닌 제3자가 끊김).

- **안 고치는 이유**: 고치려면 레이트 리밋이라는 새 기능이 필요하다. 감사가 아니라 기능 추가
- 단일 클라 계산상 63건 ≈ 65,142B로 빠듯해, 실제 발동엔 추가 트래픽이나 다중 가담이 필요
- **재개 조건**: `mmo_send_queue_overflow_total`이 정상 부하에서 0이 아니면

### 5-2. DB 저장 실패 시 dirty 복원 통로 없음
`TickPeriodicSave`는 **큐 수용 시점**에 `_dbDirty=false`로 만든다. 커밋 `5614a60`이
백프레셔 드롭은 복원하게 했지만, 큐에 들어간 뒤 `ExecSave`가 실패하는 경로는 복원이 없다.
특히 로그아웃 경로는 enqueue 직후 `delete player`라 재시도 주체가 사라진다.

- **안 고치는 이유**: `DBWorker.cpp`가 이 레인을 **"손실 허용·무트랜잭션 전용"**으로 명시.
  발동에 DB 장애가 전제되고, 유실 폭은 저장 주기(10초) 이내로 유계
- 단, `DBWorker.h`의 `Enqueue` 계약 주석("false면 dirty 되돌려 재시도해야")과 로그아웃 경로의
  실제 동작은 **모순**이다. 재화·거래를 넣게 되면 이 레인이 아니라 별도 레인이어야 한다
- **재개 조건**: `mmo_db_failed_total`이 0이 아닌 채로 운영에 들어갈 때

### 5-3. 동적 채널 인덱스 소진
`_nextChannelIndex`는 단조 증가만 하고 `CleanupEmptyChannels`가 회수하지 않는다.
누적 999회에 도달하면 `CreateChannel`이 영구 nullptr → 신규 접속 거부.

- **안 고치는 이유**: 현 INI에서 **도달 불가**. `MaxPlayersPerChannel=10000`이
  `MaxClients=6000`보다 커서 동적 채널 생성 자체가 일어나지 않는다
- **재개 조건**: `MaxPlayersPerChannel`을 `MaxClients` 아래로 내리는 순간 유효해진다

### 5-4 ~ 5-6. 성능 3건 (실측 없음 → 전부 관측대기)
| 항목 | 내용 |
|---|---|
| `CPlayer` 캐시라인 | `_sessionId`(offset 88)가 직렬화 필드(offset 0~)와 다른 라인. 송신 타깃 루프가 타깃당 2라인을 만짐. 개별 `new`라 5,000개가 힙에 산탄 |
| `CSession` 라인 동거 | `_sendDirty`/`_queuedForSend`(게임·송신 스레드)와 `_recvQ._readPos/_writePos`(수신 워커)가 같은 64B 라인 |
| `dirtyMovers` 정렬 | 틱마다 `std::sort`. 섹터 평면 그리드로 버킷팅하면 O(D log D)→O(D) |

셋 다 **프로파일 근거가 없다.** 현 실측 병목은 게임루프 단일코어(멤버십)이고, 위 셋은 그와
다른 축이다. 손대려면 먼저 재고 나서 손대는 게 순서다.

### 5-7. MOVE_BUDGET_SLACK 0.25 오탐 여부
이전 감사에서 이월된 항목. 루프백 실측은 RTT≈0이라 변별력이 없다.
- **재개 조건**: 실네트워크에서 `mmo_move_budget_rejects_total`이 0이 아니면 되돌릴 것

---

## 6. 판정: 유지 (지적은 사실이나 고치지 않기로 결정)

### 6-1. `_sessionId == -1` 검사 9곳
`CPlayer::_sessionId` 쓰기는 저장소 전체에 2곳(생성자 `-1`, `OnConnected`의 실ID)뿐이고
-1로 되돌리는 코드가 없다. -1인 구간은 `OnConnected` 안 몇 줄뿐인데 그 사이 송신 루프가 없다.
→ **검사는 항상 거짓이 맞다.** 브로드캐스트의 선카운트 패스가 이 검사 때문에만 존재한다.

- **유지 이유**: 제거는 assert 강등이 아니라 **동작 로직 변경**(패스 접기)이다. 그리고 무세션
  엔티티(NPC 등)를 섹터에 넣는 순간 되살려야 하는 검사다. 성능 이득도 실측 전이다
- 손대려면 별도 과제로, A/B 측정과 함께

### 6-2. `!session` 널 검사 묶음 (7곳)
호출처 63곳 전수 확인 결과 전부 비널 원천. 다만 건당 비용이 사실상 0이라 **이득이 없다**.
(단 `ProcessSend`의 `_disconnecting` 재검사는 실재 레이스 방어 — 유지 필수)

### 6-3. 미사용 심볼
`MIN_PACKET_SIZE`, `SESSION_INDEX_BITS/MASK`, `GetServerMode`, `ThreadSafeQueue::IsEmpty`,
`SerialBuffer`의 `operator>>` 10종·문자열 `operator<<` 2종·`MoveReadPos`·`operator=`,
`RingBuffer::IsValid`, `TimingWheel::_timeoutSec`·`Slot::IsEmpty` 등.

- **유지 이유**: 런타임 비용 0. `SerialBuffer`/`RingBuffer`는 공용 자료구조라 API를 지우면
  다른 소비자(StressTest 등)나 향후 사용에 걸린다. 감사 부담은 이 문서가 대신 진다

---

## 7. 발견 0건

**동시성·수명 렌즈는 신규 발견이 없다.** 세션 pin/ABA 3-step, 송신 1-pending의
clear-then-recheck, `_queuedForSend` 핸드오프, 이벤트 순서(DISCONNECTED→CONNECTED),
셧다운 순서(IOCP 팔과 RIO 팔의 상반된 순서 포함), TimingWheel ABA 가드,
zero-copy 링 참조 수명 — 전부 시나리오 단위로 추적해 안전을 확인했다.

과거 지적된 **`sessionId % K` 음수 문제는 해소 상태**다. 송신 분배·RIO 소유워커는
`ExtractUniqueId`(48비트 마스크) 결과라 음수가 불가능하고, `DBWorker::SlotIndex`는
uint64 캐스팅이다. uint 캐스팅이 아니라 마스크로 원천 차단된 형태.

---

## 8. 앞으로의 감사 규칙

이 문서 이후, 아래가 아니면 **다시 훑지 않는다.**

1. **새 diff 한정** — "이번 변경에서 X가 있나" (범위가 닫힌 질문)
2. **재개 조건 발동** — 5장의 각 항목에 적힌 지표가 실제로 움직였을 때
3. **새 렌즈** — 2장의 6개에 없는 각도일 때만

열린 질문("과한 코드 없나", "최적화 할 거 더 없나")은 답이 무한하다. 층위를 계속 내려가며
"실측 없는 가능성"을 만들어낼 뿐이므로, 그 형태로는 요청하지 않는다.

**판정 기준은 3장 그대로 유지**: 재현 하네스나 실측 수치가 없으면 결함이 아니라 기록이다.

---

## 9. 남은 검증 공백 (이 감사가 못 채운 것)

**런타임 스모크 미실시.** 4-1(존 이동)은 동작을 바꾸는 수정이라 빌드 성공만으로는 부족하다.
확인해야 할 것은 두 줄이다.

1. 같은 맵을 지정한 `C2S_ZONE_CHANGE` → `S2C_ZONE_CHANGE_FAIL(reason=2)`가 오고 **좌표가 그대로**인가
2. 다른 맵 / 만원이라 새로 만들어진 채널로의 이동은 **여전히 성공**하는가

부하 클라(`MMOStressConfig.ini`)는 `TargetMapId=-1`(현재 맵 제외 랜덤)이고 서버는 `MapCount=1`이라
존 이동이 `reason 0`에서 이미 걸린다 → **이번 수정은 부하 테스트 경로에 닿지 않는다**(회귀 없음).
따라서 2번을 보려면 `MapCount`를 2 이상으로 올린 임시 INI가 필요하다.

기존 하네스(`C:\Users\USER\Desktop\MMO_audit_harness\`)에는 존 이동 시나리오가 없다.
`cheat_test.cpp`가 실제 TCP 공격 클라라 여기에 "같은 맵 ZONE_CHANGE 반복 → 좌표 변화량 측정"을
붙이는 것이 가장 짧은 경로다(수정 전에는 요청마다 맵 전역으로 튄다).

**(2026-07-27 갱신) 무인 런타임 스모크 실시 — 공백 해소, 6/6 통과.**
프로토콜을 직접 파싱하는 임시 스크립트 클라(PowerShell)로, 수정 커밋(`d76dae1`)을 포함한
Release 바이너리(2026-07-27 03:43 빌드)에 대해 확인했다.

1. **같은 맵 지정** — `C2S_ZONE_CHANGE(map=0)` → `S2C_ZONE_CHANGE_FAIL(reason=2)` 수신,
   이후 ZONE_INFO/ZONE_CHANGE_OK 미수신(리스폰 발생 안 함). 거부 후 좌표는 스폰
   (63.37, 59.46) 대비 이동 프로브 자체 이동분 4.05타일(UP축, x 불변)뿐 — **재추첨 없음**
2. **다른 맵 이동** — `MapCount=2` 임시 INI에서 `map=1` 요청 → `ZONE_INFO(map=1)` +
   `ZONE_CHANGE_OK(map=1, 새 좌표)` 정상 수신. 직후 같은 맵 재요청은 `reason=2` 거부(대칭 확인)

부수 확인: `MapCount=1`에서 `targetMapId=-1` → `reason=0` (부하 테스트 경로 회귀 없음 재확인).
INI는 바이트 동일 복원(SHA256 일치). 하네스 편입(회귀 자산화)은 락프리 병합 때 함께 이관.

---

## 10. 렌즈 추가 감사 (2026-07-27) — 계측 오버헤드 · 리팩토링

8장 규칙 3(새 렌즈)에 따른 1회 감사. 범위(1장)·판정 기준(3장)은 그대로 적용했다.

- **계측 오버헤드** — 핫패스에서 지표 수집 자체가 먹는 비용. 호출당 원자연산, `now()` 빈도,
  지역 누적으로 접을 수 있는데 안 접힌 곳
- **리팩토링** — 버그를 부르거나 수정을 어렵게 만드는 중복·책임 혼재만 (미관 개선 제외)

기지 4건(틱 지역 누적 기적용 패턴 / `_broadcastCalls`·`_broadcastTargets` 3곳 /
`ProcessNetworkEvents`의 패킷당 `now()` / MonitorManager 핫카운터 false sharing)은 제외하고 봤다.

**결과: 확정 0건. 코드 무변경(빌드·스모크 불필요). 관측대기 2건 + 각주 1건 + 리팩토링 기록 4건.**

### 10-1. 계측 오버헤드

**전제 확인 — 대부분은 이미 접혀 있다.** `ParsePackets`(지역 `parsedPackets` → 종료 1회),
게임루프 틱 끝 일괄 반영(`_tick*` 7종 + `FlushHandleLatency`), `FlushSectorSends` 메트릭(틱당 2회),
SendWorker 드레인당 측정, DBWorker(원자 1회가 MySQL 왕복 옆이라 무의미) — 전부 **문제 없음**.
현 토글에서 호출당 원자연산이 살아 있는 활성 지점은 아래 (a)와 기지 항목(`RegisterSectorItem`:2023)뿐이다.
(`BroadcastAroundSector`·`BroadcastSectorPacket`·`FlushSectorUpdates`의 계측은 컴파일되지만
현 토글 조합에서 호출자가 없는 A/B 팔이다.)

#### (a) 멤버십 경로의 송신 메트릭 — mover당 원자연산 ~8회, 지역 누적으로 접힘 [관측대기]

`_sendPackets`/`_sendEnqueuedBytes` 갱신이 게임 스레드의 두 곳에서 호출당 원자로 남아 있다.

- `SendPacket` — 호출당 2회 (GameServer.cpp:1111-1112). 정상 부하의 상시 호출자는
  멤버십 인바운드 배치(`SendCreate/DeletePlayerBatch`)의 청크당 1회
- `FanoutToSectors` — 호출당 2회 (GameServer.cpp:1868-1869). 타겟 N을 1회로 접은 배치이긴 하나
  호출 자체가 mover당 2회(이탈+진입)

섹터 이동 1건당 합산 ~8회(fanout 2호출×2 + 인바운드 배치 2청크×2). `FixSameTickMoverPairs`
주석에 기록된 실측 M≈310(5,000명, 틱당 섹터이동)을 대입하면 **~2,500회/틱 ≈ 62k회/초**가
게임 스레드에서, 현 병목인 멤버십 구간 안에서 발생한다.

비용을 키우는 정황: `_sendPackets`가 속한 캐시라인(MonitorManager.h:40-45)에는 송신 워커가
WSASend마다 갱신하는 `_wsaSendCalls`, IOCP 워커가 완료마다 갱신하는 `_sendBytes`·
`_wsaSendCompletions`가 동거한다. 게임 스레드의 증가 1회가 남의 스레드가 쓰는 라인을 당겨온다.
(라인 동거 자체는 기지 별도 과제 소관 — 여기서 새로 적는 것은 **게임 스레드 기여분의 접기**다.)

- **수정안**: `_tickSendPkts`/`_tickSendBytes` 지역 누적 → 틱 끝 1회 반영. 같은 함수 안의
  `++_tickMembershipSends`가 이미 이 패턴이라 스타일 신설이 아니다. 비활성 팔
  (BroadcastAroundSector:1162, BroadcastSectorPacket:1987)도 같은 헬퍼로 접어 A/B 동등성 유지.
  EchoTestSend(워커 스레드)는 접지 않는다.
- **안 고친 이유**: 횟수는 경로 계수로 도출한 값이고 시간 비용 실측이 없다 (3장 기준).
- **재개 조건**: membership_cost 재실측 때 이 접기를 A/B 한 팔로 같이 태울 것

#### (b) 타이밍휠 REFRESH 요청이 recv 완료마다 1건 [관측대기 — 2026-07-17 나노최적화 조사 기지 항목의 잔존 확인]

`ProcessRecv` → `RequestRefresh`(IOCPServer.cpp:788)가 완료마다 lock-free 큐에 요청을 넣고,
타이머 스레드가 1초마다 전량 드레인한다. 휠 해상도가 1초(`TIMER_TICK_INTERVAL_MS=1000`)라
**유효 정보는 세션당 초당 1건**인데 요청량은 recv 완료량 그대로다 — 그 비율만큼 중복 enqueue다.

- **수정안**: 세션에 "마지막 갱신 초" 평문 스탬프를 두고 초가 바뀔 때만 enqueue.
  recv 1-pending이 세션당 처리를 직렬화하므로 동시 작성자가 없고, 설령 어긋나도 결과는
  refresh 1건 더/덜 — 무해.
- **안 고친 이유**: 워커 스레드 비용이라 현 병목(게임 스레드)과 다른 축 + 실측 없음.

#### (c) 각주 — `PushNetworkEvent`의 패킷당 `now()` (IOCPServer.cpp:1936)

기지 항목(ProcessNetworkEvents:701, 소비측 `now()`)의 **생산측 쌍**. handle-latency를
샘플링/스로틀하게 되면 양끝을 함께 손봐야 한다는 기록이다. 워커 스레드 비용이라 신규 지적이 아니다.

### 10-2. 리팩토링 — 기록 4건 (전부 안 고침, 사유 명기)

#### R1. FlushSectorUpdates ↔ FlushSectorSends 1단계 — 정렬·그룹핑·청크 루프 축자 복제

GameServer.cpp:1904-1944와 2040-2076이 문자 그대로 같다(2037 주석이 "빌드 로직 그대로"라고
자인). 차이는 최내곽 1줄(`BroadcastSectorPacket` vs `RegisterSectorItem`)뿐. 두 팔은
`USE_BROADCAST_BUNDLE` A/B의 양쪽이라 **그룹핑에 수정이 생기면 손으로 두 번 반영해야 하고,
한쪽만 반영되면 "빌드 로직 동일"이라는 실험 통제 전제가 조용히 깨진다.**

- **수정안**: 정렬+그룹 경계+청크 분할을 콜백 받는 공용 헬퍼로 (동작 불변, 빌드로 검증 가능)
- **안 고친 이유**: 지금은 두 팔이 동일함을 이 감사가 확인했다. 수정은 별도 diff로
- **재개 조건**: 이 두 함수 중 한쪽이라도 고치게 되는 첫 diff에서 통합을 먼저 할 것

#### R2. 존 입장 의식 3중 복제 — fallback 사본만 `_dbDirty` 누락 (복제가 이미 낳은 불일치)

{입장 → lastSync 초기화 → dbDirty → 존 정보/자기소개 → BroadcastEnterZone} 의식이 3곳에
복제돼 있다: OnConnected(GameServer.cpp:733-762), 존이동 성공(1375-1391), 존이동 실패 후
원맵 복귀(1352-1366). **복귀 사본만 `player->_dbDirty = true`가 없다** — `EnterZone`의
`CalcSpawnPos`가 좌표를 재추첨하는데 저장 대상 마킹이 빠져, IDLE로 머물면 다음 주기 저장에서
제외된다(DB에 옛 좌표 잔존).

- **단, 현재 도달불가**: `CZone::EnterZone`(Zone.cpp:32-55)은 `player==nullptr` 외에 실패를
  반환하지 않는다 → `!EnterZone` 분기 자체(733, 1348)가 죽은 방어다. 누락도 잠복 상태
- **안 고친 이유**: 도달불가 경로 수정은 검증할 수 없다(4-8의 RIO 팔과 같은 논리)
- **재개 조건**: `EnterZone`에 실패 사유(용량·조건 스폰 등)가 생기는 diff에서, 이 3중 의식을
  헬퍼 1개로 접는 것까지 함께 할 것. 그 전까지 이 기록이 낡은 사본의 존재를 대신 기억한다

#### R3. 2패스 배치 AddRef 팬아웃 — 소유권 산수가 3중 복제

선카운트 → `AddRef(N)` → 타겟별 `RequestSendMsg`(ref 1씩 소비) → 배치 메트릭 → `SubRef` 패턴이
3곳에 복제: `BroadcastAroundSector`(1140-1166) / `FanoutToSectors`(1832-1874) /
`BroadcastSectorPacket`(1966-1991). "확보 수 = 소비 수" 계약이 세 곳에서 각자 성립해야 하고,
`RequestSendMsg`의 ref 계약이 바뀌면 세 곳을 lockstep으로 고쳐야 한다(어긋나면 누수/이중해제 —
과거 감사가 사냥하던 바로 그 부류).

- **안 고친 이유**: 3곳 중 2곳은 현 토글에서 비활성 A/B 팔 — 통합이 오히려 실측이 끝난 활성
  경로를 흔든다. 각 사이트 주석이 서로를 교차 참조하고 있어 당장의 이탈 위험은 낮다
- **재개 조건**: RIO 채택 등 송신 경로 대수술 때 함께 통합

#### R4. 섹터 이동 부기(Remove→좌표 갱신→Add→변경 기록) 3곳 — 순서 계약을 주석이 방어 중

Zone::Tick(Zone.cpp:120-125) / RecvMoveStart(GameServer.cpp:947-955) /
RecvMoveStop(1016-1026)이 같은 6줄을 복제한다. "이전 좌표로 Remove를 먼저"라는 순서 계약을
SectorManager.cpp:72-76 주석이 **호출처 4곳을 열거하는 방식으로** 방어하고 있다 — 새 호출처가
생길 때마다 그 열거와 검증을 사람이 갱신해야 한다.

- **수정안**: `MoveToSector(zone, player, newX, newY)` 헬퍼 1개
- **안 고친 이유**: 3곳은 안정 상태이고, 순서 위반은 디버그 assert(SectorManager.cpp:77)가
  즉시 잡는다. 4번째 호출처가 생기는 diff에서 헬퍼 도입을 먼저 할 것

### 10-3. 이 렌즈의 종료 선언

계측 오버헤드·리팩토링 각도는 이 문서로 닫는다. 이후 재훑기는 8장 규칙(새 diff / 재개 조건 발동 /
또 다른 새 렌즈)으로만 받는다.
