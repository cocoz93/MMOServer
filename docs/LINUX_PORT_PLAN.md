# 리눅스 포팅 계획 (feat/cmake-netiomodel)

Windows IOCP 서버를 리눅스로 옮기는 작업의 남은 순서.
단계마다 **페이즈**로 쪼갰고, 각 페이즈에 **판정** 기준을 붙였다. 판정이 통과해야 다음으로 넘어간다.

## 이 문서를 쓰는 법 (루프 작업 규칙)

**이 문서가 진행 상태의 정본이다.** 작업을 시작할 때 여기부터 읽고, 체크가 안 된 첫 페이즈 **하나만** 한다.

### 페이즈 하나를 끝내는 절차

1. **작업** — 페이즈 본문에 적힌 것을 한다
2. **판정** — 페이즈에 붙은 판정 기준을 실제로 돌려본다. 판정을 "돌린 셈 치고" 넘어가지 않는다
3. **자가검증** — 이번 페이즈에서 **내가 근거로 든 것**(파일·줄번호·개수·수치)을 실제 코드와 다시 대조한다.
   틀린 게 나오면 문서를 고치고, 그게 판정을 뒤집는 수준이면 페이즈를 다시 연다.
   *범위는 이번 페이즈에서 새로 주장한 것까지다 — 문서 전체를 매번 재감사하지 않는다*
4. **기록** — `- [ ]` → `- [x]`로 바꾸고 판정 결과를 한 줄로 적는다(확인 가능한 형태로: 수치·명령 결과·경고 개수 등)
5. **커밋** — 판정을 통과했으면 커밋한다. 메시지는 **개조식**(명사형 종결), `Co-Authored-By` 같은 트레일러는 넣지 않는다.
   **push는 하지 않는다** — 사용자가 직접 한다 (지금 브랜치는 리베이스로 히스토리가 갈려 force가 필요한 상태)

### 막혔을 때

- 판정을 못 넘겼으면 `- [!]`로 두고 **막힌 지점과 시도한 것**을 적은 뒤, **다음 페이즈로 계속 간다**
- 단, 막힌 페이즈가 전제인 페이즈는 시도하지 말고 `- [대기]`로 표시한다. 대신 **의존이 없는 경로로 우회**한다:
  - **1·2단계(LockFree)가 막혀도 `4-A`~`4-C`는 갈 수 있다** — 그쪽은 Windows 코드 리팩터링이라 리눅스 빌드와 무관하다
  - `3단계(CrashDump)`는 1단계와 독립이다
  - `4-D` 이후는 1~3이 전제다(리눅스에서 서버가 서야 한다)
- 사람이 직접 해야 하는 페이즈는 **[사용자]** 로 표시했다. 여기 걸리면 멈추고 무엇이 필요한지 알린다
- 판단이 갈리는 지점(설계 선택)은 혼자 정하지 말고 선택지를 남긴다

### 상시 유의

- 소스 `.h/.cpp` = UTF-8 BOM + LF, `CMakeLists.txt` = BOM + CRLF. 줄바꿈 검증은 CR 생카운트 말고 `git diff`로
- **Windows 회귀 빌드(Release 전체 재빌드)는 `[회귀빌드]` 표시가 붙은 페이즈에서만** 돌린다.
  기준선은 경고0 오류0이고, IOCP 팔·RIO 팔 양쪽을 본다(RIO는 `BuildConfig.h`의 `USE_RIO_TRANSPORT`를 임시로 1로 두고 확인 후 원복).
  리눅스만 건드린 페이즈에서는 생략한다 — 전체 재빌드는 수 분 걸린다
- 리눅스 명령은 `wsl -d Ubuntu-24.04 -u root -- bash -lc "..."` (root면 sudo 비번 불필요).
  PowerShell에서는 `$env:WSL_UTF8=1`을 먼저 줘야 출력이 안 깨진다
- 코드를 건드리면 그 심볼을 참조하는 다른 파일도 검색해 영향 범위를 확인한다

---

## 이미 끝난 것

**브랜치 커밋 11개** (2026-08-03에 main 위로 리베이스 완료)

- Windows CMake Release 빌드 (경고0 오류0)
- Windows 스칼라 타입 → cstdint 고정폭 (75곳). `DWORD=unsigned long`이 리눅스 LP64에서 8바이트라 필수
- Platform 격리 계층 (타이머·경로·affinity·종료 시그널·스레드 CPU 측정)
- INI 로더를 플랫폼 독립 파서로 교체 (`GetPrivateProfile*` 제거)
- 크래시 덤프 분기 (Windows 원본 `#ifdef` 보존 + Linux sigaction/backtrace)
- 통계 카운터 `Interlocked` → `std::atomic` (`Counter` 캡슐화)
- 로거 시각 `localtime_r` 분기, DB winsock 분기
- 독립 epoll 에코 PoC (`poc/EpollEchoServer.h`) — 서버 본체와는 미연결

**리베이스에서 지킨 것** (브랜치를 그대로 채택했으면 사라졌을 기능)

- 창 닫기·로그오프 시 정리 대기 → `Platform::InstallShutdownHandler(cb, completeFlag, waitMaxMs)`로 이식
- `mmo_thread_kernel_ratio` 메트릭 → `GetThreadCpuTimeNs` 3인자 오버로드 추가
- `WorkerCounter.completionCount/dequeueCalls`는 **평문 유지** — 원자 증가는 완료수거 A/B를 편향시킨다

## 방향 결정 (재론 금지)

커스텀 컴포넌트(SerialBuffer·LockFree)는 **유지하고 포팅**한다. 라이브러리로 교체하지 않는다.

- 큐·스택을 라이브러리로 바꿔도 `CExternalTlsFreeList`(SerialBuffer 풀)가 `CInternalFreeList`를 청크 풀로 쓰므로 128비트 CAS 문제가 그대로 남는다 (`ExternalTlsFreeList.h:6,91` → `InternalFreeList.h:284`)
- 진행 중인 "자체 큐 vs 외부 8종" 벤치의 비교 대상이 사라진다
- custom vs 라이브러리 비교는 **백엔드 고정 A/B**로 따로 한다. OS 포팅과 묶으면 두 변수가 섞인다

---

## 0. 리눅스 환경 — ✅ 완료 (2026-08-03)

- [x] **0-A** WSL2 + Ubuntu 24.04 설치 **[사용자]** → 커널 6.18.33.2, WSL2 확인
- [x] **0-B** 툴체인 설치 → g++ 13.3.0 / cmake 3.28.3 / ninja 1.11.1 / libmysqlclient 8.0.46
- [x] **0-C** 경로 확인 → `/mnt/c/Users/USER/Desktop/MyGit` 아래 LockFree·MMO 형제 배치 확인 (`LockFreeConfig.h:39` 전제 유지)
- [x] **판정** → configure 통과 + `epoll_echo_poc` 실제 빌드·링크 성공 (경고0)

---

## 1. LockFree 포팅

브랜치 코드가 리눅스에서 **컴파일되게** 만드는 단계. 동작 검증은 2단계에서 한다.

- [x] **1-A** 남은 Windows 심볼 전수 조사 — **완료 (2026-08-03)**. 아래가 조사 결과이자 1-B~1-D의 작업 목록이다

  대상: `MyGit/LockFree/LockFree_Test/LockFree/` 헤더 4개 (`InternalFreeList.h` / `ExternalTlsFreeList.h` / `LockFreeQueue.h` / `LockFreeStack.h`)

  **① `windows.h` 직접 include — 1곳**: `InternalFreeList.h:8`. 나머지 3개 헤더는 이걸 타고 들어온다

  **② Interlocked 호출 — 18곳** (텍스트 등장은 20곳이지만 2곳이 코드가 아니다: `ExternalTlsFreeList.h:75` 주석, `LockFreeQueue.h:166-167` assert 메시지 문자열)

  | 종류 | 호출 | 위치 |
  |---|---|---|
  | `CompareExchange128` | **7** | `Queue` 318·343·352·429·452 / `Stack` 219 / `InternalFreeList` 284 |
  | `Increment64` | 5 | `ExternalTls` 57 / `InternalFreeList` 230·252 / `Queue` 368 / `Stack` 185 |
  | `Decrement64` | 3 | `InternalFreeList` 310 / `Queue` 476 / `Stack` 242 |
  | `CompareExchangePointer` | 2 | `InternalFreeList` 210 / `Stack` 167 |
  | `Decrement16` | **1** | `ExternalTls` 255 |

  - **정정 2건**: 이전 기록의 "Interlocked 20곳"과 "CAS128 8곳, Queue 6"은 둘 다 텍스트 등장을 센 것이다. `LockFreeQueue.h:166-167`은 호출이 아니라 **정렬 검사 assert의 메시지 문자열**이고, `ExternalTlsFreeList.h:75`는 주석이다. 실제 호출은 **총 18곳, CAS128 7곳(Queue 5)**
  - `Decrement16` 대상 필드는 `ExternalTlsFreeList.h:71`의 **`alignas(64) volatile SHORT FreeCount`** — 16비트 부호있는 정수다

  **③ Windows 힙 API — 6곳, `InternalFreeList.h`에 집중** ← *어댑터로 못 덮는다. 1-C 참조*
  - `HeapCreate` 120 / `HeapSetInformation`(저단편화 힙 켜기) 130 / `HeapAlloc` 242 / `HeapFree` 166 / `HeapDestroy` 170 / `HANDLE hHeap` 331

  **④ 기타 Windows 전용 매크로·함수**
  - `__fastfail(FAST_FAIL_INVALID_ARG)` 3곳 — `ExternalTls` 247·250 / `InternalFreeList` 188 → `__builtin_trap()`
  - `YieldProcessor()` 2곳 — `InternalFreeList` 77 / `Queue` 90 → `__builtin_ia32_pause()`
  - `__declspec(noinline)` 1곳 — `InternalFreeList` 236 → `__attribute__((noinline))`
  - `UINT_PTR` 1곳 — `Queue` 166 → `uintptr_t`

  **⑤ 스칼라 타입** — `INT64` 43회(`Queue` 25 / `InternalFreeList` 9 / `Stack` 7 / `ExternalTls` 2), `LONG64` 9회(`ExternalTls` 6 / `InternalFreeList` 3), `SHORT` 2회, `__forceinline` 5회

- [ ] **1-B** `LockFreeCompat.h` 신설 — 원자연산 어댑터 5종 + 타입 별칭 + 매크로
  - 어댑터로 감싸는 이유는 호출부를 안 고치기 위해서다. CAS128은 MSVC판이 128비트를 `high`/`low` 64비트 둘로 쪼개 받고 GCC판은 128비트 값 하나로 받는데, `__atomic_compare_exchange_n`이 expected를 in/out으로 받아 시맨틱은 그대로 맞는다
  - 어댑터 5종: `CompareExchange128`(7곳) / `Increment64`(5) / `Decrement64`(3) / `CompareExchangePointer`(2) / **`Decrement16`(1, `volatile SHORT` 대상)**
  - 함께 필요한 것: `INT64`·`LONG64`·`SHORT`·`UINT_PTR` 별칭, `__forceinline`·`__declspec(noinline)` 매크로, `__fastfail`·`YieldProcessor` 대체
  - 16바이트 정렬 요건은 이미 코드에 있다 (`LockFreeQueue.h:69` `alignas(16)`), 검사 assert도 이미 있다 (`:166`)
  - **함정**: `-mcx16`이 없으면 GCC가 `cmpxchg16b` 대신 libatomic **뮤텍스 폴백**으로 조용히 내려간다. 락프리인 줄 알고 락을 쓰게 된다 → `static_assert(__atomic_always_lock_free(16, 0))`로 컴파일 단계에서 막을 것
  - **판정** → 헤더 단독 컴파일 통과 + `-mcx16` 뺀 빌드에서 static_assert가 실제로 걸리는지 확인

- [ ] **1-C** Windows 힙 API 대체 ← **설계 선택이 필요하다. 혼자 정하지 말 것**
  - `CInternalFreeList`는 **전용 힙**(`HeapCreate`)을 만들어 노드를 거기서만 할당한다. 리눅스엔 "프로세스 안에 격리된 힙"이라는 개념이 없어 1:1 치환이 안 된다
  - 게다가 `HeapSetInformation(..., HeapCompatibilityInformation, 2)`는 **저단편화 힙(LFH)을 켜는 코드**라 성능 의도가 실려 있다. 단순 `malloc` 치환은 할당 성능이 달라질 수 있고, 그러면 6단계 IOCP vs epoll 비교에 변인이 하나 섞인다
  - 선택지 (실제 착수 시 사용자에게 확인):
    - **(a) `malloc`/`free` 단순 치환** — 가장 짧다. glibc malloc도 스레드별 아레나가 있어 실용상 충분할 수 있으나, 성능 동등성은 측정 전엔 모른다
    - **(b) 리눅스에서도 전용 아레나 유지** — `mmap` 기반 청크 할당기를 직접 둔다. Windows 동작에 가깝지만 새 코드가 늘고, 그 코드 자체가 검증 대상이 된다
    - **(c) 힙 계층을 아예 걷어내고 상위 풀에 맡김** — 이 자료구조가 이미 프리리스트 풀이라 중복일 수 있다. 다만 Windows 동작도 함께 바뀌므로 회귀 위험이 가장 크다
  - **판정** → 선택한 방식으로 리눅스 컴파일 통과 + Windows 경로 **무변경**(`#ifdef`로 기존 힙 코드 보존)

- [ ] **1-D** `InternalFreeList.h:8`의 `windows.h` 제거 + 4개 헤더의 Windows 심볼을 어댑터로 치환
  - `SerialBuffer.h`가 `LockFreeConfig.h`(`:39~42`)를 타고 이 헤더들을 끌어오므로, 여기가 막히면 서버 본체도 리눅스에서 안 열린다
  - **판정** → 리눅스에서 LockFree 헤더 4개 컴파일 통과

- [ ] **1-E** CMake에 LockFree 경로·`-mcx16` 반영 (UNIX 분기)
  - **판정** → `cmake --build` 로 LockFree를 쓰는 TU가 컴파일됨

- [ ] **1-F** Windows 회귀 확인 `[회귀빌드]`
  - **판정** → `IOCP_Server.sln` Release 재빌드 **경고0 오류0 불변** (IOCP 팔·RIO 팔 양쪽)

---

## 2. 안전성 검사

컴파일이 아니라 **동작**을 본다. 락프리 자료구조는 컴파일만으로는 아무것도 보장하지 않는다.

- [ ] **2-A** `LockFree_Test`를 리눅스에서 빌드 (`TestCode.cpp`의 Windows 의존 1곳 정리)
  - **판정** → 테스트 실행 파일이 만들어짐

- [ ] **2-B** 기존 테스트 전량 실행 + Windows 결과와 대조
  - **판정** → Windows에서 통과하던 항목이 리눅스에서 **동일하게** 통과 (항목 수까지 대조)

- [ ] **2-C** `LF_RACE_HOOK` 증폭 빌드로 경합 재현
  - **판정** → 증폭 상태에서도 자료구조 불변식이 깨지지 않음

- [ ] **2-D** `-fsanitize=thread` 빌드 (선택 — 2-C가 깨끗하면 건너뛸 수 있음)
  - **판정** → TSan 경고 0, 또는 "이건 오탐"이라는 근거와 함께 기록

---

## 3. CrashDump 검증

구현은 이미 끝났다(커밋 `2db4f3d`). 실제로 찍히는지만 본다.

- [ ] **3-A** 링크에 `-rdynamic` 추가 — 없으면 backtrace에 함수명이 안 나온다
  - **판정** → 링크 옵션에 반영됨

- [ ] **3-B** 의도적 널 역참조 / abort 유발 후 로그 확인
  - **판정** → 로그에 **함수명 포함** 스택이 남음

---

## 4. epoll 백엔드 ← 최대 작업

**전제가 바뀌었다.** 옛 계획은 `NetIoModel.h`가 `EpollServer.h`를 include하는 구조였지만, main이 전송계층을
`Transport_Iocp.cpp` / `Transport_Rio.cpp`로 이미 갈라놨다. epoll은 **세 번째 팔(`Transport_Epoll.cpp`)** 로 얹는다.
확장점이 이미 뚫려 있어 오히려 유리하다 — 경계 함수 14개(`IOCPServer.h:377-390`)를 구현하면 된다.

- [ ] **4-A** 경계 조사 — 전송 경계 14개 함수의 시그니처에 박힌 Windows 타입, 그리고 골격(`IOCPServer.cpp` 1,167줄)에 남은 Windows 심볼을 분류
  - 실측 사전조사 (2026-08-03): `SOCKET` 20 / `InterlockedExchange` 14 / `closesocket` 7 / `WSAGetLastError` 4 / `InterlockedDecrement` 4 / `ZeroMemory`·`WSACleanup`·`OVERLAPPED`·`InterlockedIncrement`·`CancelIoEx` 각 3 / `WSAStartup`·`WSASocket`·`WSASend`·`DWORD` 각 2
  - **`WSASend`가 골격에 2곳 남아 있다** — 전송계층으로 다 갔을 줄 알았던 자리라, 어느 `#if` 분기에 걸려 있는지부터 확인할 것
  - 분류 축: ① Platform으로 밀 것 ② Transport 경계 뒤로 밀 것 ③ 타입 별칭으로 해결될 것
  - **판정** → 분류 결과가 이 문서에 기록됨 (조사만, 코드 수정 없음)

- [ ] **4-B** `[회귀빌드]` 경계 타입 중립화 — 소켓 핸들 등 경계 시그니처를 플랫폼 중립 별칭으로
  - **판정** → Windows Release 재빌드 경고0 오류0 **불변** (동작 불변 리팩터링)

- [ ] **4-C** `[회귀빌드]` 골격에서 Windows 전용 코드 밀어내기 — `IOCPServer.cpp`가 리눅스에서도 컴파일되는 상태로
  - **판정** → 리눅스에서 골격 TU 컴파일 통과(링크는 아직 실패해도 됨) + Windows 재빌드 불변

- [ ] **4-D** `Transport_Epoll.cpp` 뼈대 — 리스너 + 워커 스레드 + `epoll_wait` 루프. 연결 수락/해제까지만
  - **판정** → 리눅스에서 서버가 뜨고 클라 1개가 붙었다 끊김 (데이터 교환 없음)

- [ ] **4-E** 세션 관리 이식 — 인덱스 스택·uniqueId·refcount. 대부분 그대로 옮겨진다
  - **판정** → 붙었다 끊기를 반복해도 세션 슬롯이 새지 않음(생성/소멸 카운트 일치)

- [ ] **4-F** 수신 경로 — 링버퍼 + 패킷 분해
  - **판정** → 클라가 보낸 패킷이 게임 로직까지 도달(에코 모드로 확인)

- [ ] **4-G** 송신 경로 ← **IOCP와 가장 다른 곳**
  - IOCP는 "보내달라고 걸어두면 완료를 통지"하는데, epoll은 "보낼 수 있게 되면 알려줌"이다
  - Send Coalescing과 SendWorker 풀을 `EPOLLOUT` 등록/해제 방식으로 재설계해야 한다
  - 기존 자산: Send Coalescing(syscall −94%), SendThread 분리, SendWorker Pool(K3, uniqueId%K 분배)
  - **판정** → 대량 송신에서 데이터 유실·순서 뒤바뀜 없음

- [ ] **4-H** TimingWheel·타임아웃 연동
  - **판정** → 무응답 클라가 타임아웃으로 정리됨

- [ ] **4-I** `[회귀빌드]` 왕복 검증
  - **판정** → 클라 1개로 **접속 → 이동 → 채팅** 왕복

---

## 5. 실동작 검증

- [ ] **5-A** WSL2 포트포워딩 (기본 NAT라 외부에서 안 붙는다)
  - **판정** → Windows 쪽에서 리눅스 서버 포트로 접속됨

- [ ] **5-B** Windows 부하 클라 → 리눅스 서버, 100명
  - **판정** → `loop_p99 < 40ms`, `buffer_full = 0`

- [ ] **5-C** 1,000명
  - **판정** → 같은 기준 유지

---

## 6. IOCP vs epoll 비교

- [ ] **6-A** 실기 환경 확보 **[사용자]** — 듀얼부팅 또는 클라우드 인스턴스
  - **주의**: WSL2에서 낸 수치는 성능 비교로 **쓸 수 없다**. 가상화 + NAT 오버헤드 때문에 IOCP가 이기는 게 당연해진다. 0~5는 WSL로 충분하지만 6번만은 아니다
- [ ] **6-B** 같은 시나리오 A/B 수집
- [ ] **6-C** 결과 정리 (포트폴리오 소재)
