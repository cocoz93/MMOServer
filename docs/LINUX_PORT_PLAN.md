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

## 1. LockFree 포팅 — ✅ 완료 (2026-08-03)

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

- [x] **1-B** `LockFreeCompat.h` 신설 — **완료 (2026-08-03)**. `LockFree_Test/LockFree/LockFreeCompat.h`
  - **판정 결과**: `-mcx16` 있음 → `-Wall -Wextra` 경고0 컴파일 통과 / `-mcx16` 없음 → `#error`로 차단(exit 1).
    추가로 시맨틱 단위테스트 **15/15 PASS**(CAS128 성공·실패 양쪽의 반환값과 comparand 갱신, 포인터 CAS의 "교환 전 값" 반환, 증감의 "연산 후 값" 반환, `Decrement16` 16비트 래핑),
    생성 코드에 **인라인 `lock cmpxchg16b` 2개 / libatomic 호출 0** 확인
  - 구성: 어댑터 5종(`CompareExchange128` 7곳 / `Increment64` 5 / `Decrement64` 3 / `CompareExchangePointer` 2 / `Decrement16` 1),
    타입 별칭(`INT64`·`LONG64`·`SHORT`·`PVOID`·`UINT_PTR`, `NULL`·`FALSE`·`TRUE`), 매크로(`__forceinline`·`__declspec`·`__fastfail`·`YieldProcessor`)
  - **1-A에서 놓친 타입 발견**: `PVOID` 6회(포인터 CAS 인자). 별칭에 포함시켰다

  > **실측으로 뒤집힌 전제 — 16바이트만 `__sync`를 써야 한다** (g++ 13.3, 2026-08-03)
  >
  > 원래 계획은 "`__atomic_compare_exchange_n`이 expected를 in/out으로 받아 시맨틱이 맞으니 그걸 쓰고, `-mcx16`으로 뮤텍스 폴백만 막자"였다. 실제 코드 생성을 확인해보니 **틀렸다**.
  >
  > | 빌트인 | `-mcx16` 있음 | 없음 |
  > |---|---|---|
  > | `__atomic_compare_exchange_n` (16B) | `call __atomic_compare_exchange_16@PLT` | 〃 |
  > | `__sync_val_compare_and_swap` (16B) | **`lock cmpxchg16b` 인라인** | `call __sync_val_compare_and_swap_16@PLT` |
  >
  > - `__atomic_*`는 **`-mcx16`을 줘도 libatomic 함수 호출**이다. 락프리이긴 하지만(libatomic이 내부에서 cmpxchg16b를 쓴다) 핫패스마다 PLT 경유 호출이 붙는다
  > - 방어막도 `__atomic_always_lock_free(16, 0)`으로는 못 세운다 — GCC가 16바이트를 인라인 처리하지 않아 **`-mcx16` 유무와 무관하게 항상 false**다. 처음 이걸로 짰다가 `-mcx16`을 줬는데도 assert가 걸려서 발각됐다
  > - 올바른 판별자는 **`__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16`** — `-mcx16`일 때만 정의된다
  > - 8바이트·16비트·포인터는 `-mcx16` 없이도 `__atomic_*`가 인라인(`lock xaddq`/`lock xaddw`/`lock cmpxchgq`)이라 그대로 뒀다. **16바이트만 예외**다

- [x] **1-C** Windows 힙 API 대체 — **완료 (2026-08-03). (a) 표준 할당자 치환으로 진행**
  - **판정 결과**: 어댑터 동작 테스트 **10/10 PASS**(전용 힙 생성·할당·해제, 정렬 할당의 실제 정렬값), `-Wall -Wextra` **경고 0**. Windows 경로는 `#ifdef _WIN32` 안이 `<windows.h>` include뿐이라 무변경(최종 확인은 1-F)
  - 추가한 어댑터: `HeapCreate` / `HeapSetInformation` / `HeapAlloc` / `HeapFree` / `HeapDestroy` / `_aligned_malloc` / `_aligned_free`
  - **1-A에서 또 놓친 것**: `_aligned_malloc` 4곳·`_aligned_free` 8곳 (`InternalFreeList` 1/2, `LockFreeQueue` 2/5, `LockFreeStack` 1/1). Windows CRT 함수라 `Interlocked*` 패턴 검색에 안 걸렸다

  > **선택지 3안 중 (a)로 간 근거 — 성능 우려가 조사로 해소됐다**
  >
  > 착수 전엔 "LFH 설정에 성능 의도가 실려 있어 `malloc` 치환이 6단계 비교의 변인이 될 수 있다"고 봤는데, 실제 호출 경로를 보니 **힙 API가 전부 cold path**였다.
  > - `HeapAlloc`은 `AllocNewNode()` 안에서만 불리고, 이 함수엔 **`__declspec(noinline)`**이 붙어 있다 — 프리리스트가 비었을 때만 도는 경로다. 운영 중에는 `Init(사전적재)`로 채워두므로 거의 안 불린다 (MMO 본체도 `Init(_maxClients * 2)`로 미리 채운다)
  > - `HeapFree`는 소멸자의 정리 루프에서만, `HeapDestroy`는 소멸자 끝에 1회
  >
  > 핫패스에 닿지 않으므로 (b) mmap 아레나는 과잉이고, (c) 힙 계층 제거는 Windows 회귀 위험만 크다.
  >
  > **다만 Windows와 달라지는 점 하나**: `HeapDestroy`는 힙을 통째로 반환해 "아직 Free되지 않은(사용 중) 노드"의 메모리까지 회수하지만, `malloc` 경로엔 그런 일괄 회수가 없다. 소멸자는 프로세스 생애 1회라 실질 영향은 없으나(OS가 회수) **ASan/valgrind에는 누수로 잡힌다** — 2-D에서 그렇게 보이면 이것이 원인이다. 원본도 같은 자리에서 "T 소멸자 미호출"을 이미 인정하고 주석에 남겨 두었다.

- [x] **1-D** `windows.h` 제거 + 어댑터 치환 — **완료 (2026-08-03)**
  - **판정 결과**: 헤더 4개의 **모든 멤버 함수를 명시적 인스턴스화**(템플릿 파라미터 조합 포함)해 컴파일 + **링크 + 실행까지 성공**, `-Wall -Wextra` 경고 0.
    산출 바이너리에 **인라인 `cmpxchg16b` 15개 / 외부 CAS 호출 0** 확인
  - 고친 곳은 `InternalFreeList.h`의 include 한 줄뿐이다 — `<windows.h>`·`<intrin.h>` → `"LockFreeCompat.h"`. 나머지 3개 헤더는 이걸 타고 받으므로 **자료구조 코드는 한 줄도 안 건드렸다**
  - 처음엔 객체 생성만으로 판정하려 했는데, 그러면 실제 호출된 멤버만 인스턴스화돼 검증이 헐거워진다. 명시적 인스턴스화로 바꾸면서 `CLockFreeQueue`가 `PlacementNew=true`를 static_assert로 막고 있다는 것도 확인했다(설계 의도, 노드 Tag 보존)

  > **1-A 조사가 세 번째로 보완됐다 — 심볼 grep은 정본이 아니다**
  >
  > 컴파일러를 돌리자 패턴 검색이 놓친 것이 한꺼번에 나왔다.
  > - `SwitchToThread()` 3곳 (`InternalFreeList` 81 / `Queue` 95 / `Stack` 53) → `sched_yield()`
  > - **TLS API 5종** (`ExternalTlsFreeList`) — `TlsAlloc`/`TlsFree`/`TlsGetValue`/`TlsSetValue`/`TLS_OUT_OF_INDEXES` → pthread key 매핑. 호출부가 인덱스를 `int TlsIndex`로 들고 있어 무효값을 `-1`로 맞췄다
  > - `offsetof` — `<cstddef>`가 필요했다(Windows에선 `windows.h`가 끌어오던 것)
  > - `<intrin.h>` — MSVC 전용 헤더
  >
  > 앞선 두 번(1-B의 `PVOID`, 1-C의 `_aligned_malloc` 계열)까지 합치면 패턴 검색은 **네 번 연속 불완전**했다. 원인은 단순하다 — `Interlocked*`·`__forceinline` 같은 "아는 이름"만 찾았고, Windows CRT·TLS·스레드 API처럼 이름이 다른 계열은 걸리지 않았다.
  > **남은 단계에서는 조사 결과를 완전한 목록으로 신뢰하지 말고, 컴파일러가 뱉는 에러를 정본으로 삼을 것.** 특히 4-A(골격 Windows 심볼 분류)가 같은 함정을 안고 있다.

- [x] **1-E** CMake에 LockFree 경로·`-mcx16` 반영 — **완료 (2026-08-03)**
  - **판정 결과**: `cmake --build build-linux` 전체 성공, **경고 0**. 산출물 `lockfree_headers_check`에 **인라인 `cmpxchg16b` 19개 / 외부 CAS 호출 0**, 실행 exit 0
  - `add_compile_options(-mcx16)`을 UNIX 분기 **전역**에 걸었다. 타겟마다 붙이면 앞으로 추가될 타겟(epoll 백엔드)이 빠뜨릴 수 있는데, 빠뜨려도 `LockFreeCompat.h`의 `#error`가 잡긴 하지만 그건 사고를 늦게 아는 방식이다
  - **`lockfree_headers_check` 타겟 신설** (`poc/lockfree_headers_check.cpp` + `LockFreeConfig.cpp`) — 4단계 전까지 서버 본체가 리눅스에서 안 열리므로, 그동안 이 타겟이 LockFree 이식 상태를 지킨다. 1-D에서 쓰던 임시 검증을 빌드에 상주시킨 것이다
  - libatomic 링크는 실제로 불필요했다 — `-latomic` 없이 링크 성공

  > **부수 수정 — `CrashDump.h` 리눅스 경로 경고 6건 제거**
  > 새 타겟이 `LockFreeConfig.cpp`를 통해 `CrashDump.h`를 끌어오면서, 시그널 핸들러의 `::write()` 반환값 무시가 `warn_unused_result` 경고로 드러났다. 3단계 대상 파일이지만 지금 처리했다 — 경고가 쌓이면 정작 봐야 할 새 경고가 묻힌다.
  > 핸들러 안에서는 write 실패를 복구할 길이 없으므로 `WriteRaw()` 헬퍼로 감싸 "의도적으로 버린다"를 코드로 남겼다. Windows 경로(`#ifdef _WIN32`)는 건드리지 않았다.

- [x] **1-F** Windows 회귀 확인 `[회귀빌드]` — **완료 (2026-08-03). 1단계 졸업**
  - **판정 결과**: Release 전체 재빌드 **IOCP 팔 경고0 오류0 / RIO 팔 경고0 오류0**. `BuildConfig.h` 원복 확인
  - 판정 범위 밖이지만 **실제 기동까지 확인**했다 — LockFree는 서버가 뜰 때 메모리 풀을 잡는 자리(`CSerialBuffer::_TlsMsgFreeList->Init(maxClients*2)` = 12,000노드 사전 할당)라, 빌드만으로는 이식이 멀쩡한지 알 수 없다. 초기화 실패 로그 없이 `Server started`까지 도달
  - Windows 쪽에서 실제로 바뀐 것은 include 경로 한 줄(`InternalFreeList.h`)뿐이고, 빌드가 통과했다는 것 자체가 `LockFreeCompat.h`가 제대로 끼어들어 `windows.h`/`intrin.h`를 공급했다는 증거다(안 끼었으면 `HANDLE`·`Interlocked*`를 못 찾아 깨진다)

---

### 1단계 결산 — LockFree 포팅 완료 (2026-08-03)

- 자료구조 코드는 **한 줄도 고치지 않았다**. 바뀐 것은 `InternalFreeList.h`의 include 한 줄과, 새로 만든 `LockFreeCompat.h` 하나뿐
- 어댑터가 덮은 범위: 원자연산 5종 / 힙 API 5종 / 정렬 할당 2종 / TLS 5종 / 스레드 양보 1종 / 타입 별칭 9개 / 컴파일러 지시자 4개
- **가장 큰 소득은 `__atomic` → `__sync` 전환**이다. 그냥 갔으면 16바이트 CAS가 libatomic 호출로 내려가 핫패스마다 PLT를 타면서도 아무도 몰랐을 것이다. 6단계 비교에서 리눅스만 불리해진 채 "epoll이 느리다"는 잘못된 결론이 나올 뻔했다
- **조사 방식의 교훈**: 심볼 grep은 4회 연속 누락을 냈고 컴파일러가 한 번에 다 잡았다. 4-A에서 반복하지 말 것

---

## 2. 안전성 검사 — ✅ 완료 (2026-08-03)

컴파일이 아니라 **동작**을 본다. 락프리 자료구조는 컴파일만으로는 아무것도 보장하지 않는다.

- [x] **2-A** `LockFree_Test` 리눅스 빌드 — **완료 (2026-08-03)**
  - **판정 결과**: 컴파일 exit 0, **경고 0 / 에러 0**, 산출물 283,808 bytes. 바이너리에 **인라인 `cmpxchg16b` 54개**. Windows(`LockFree_Test.sln` Release Rebuild)도 정상 — 2-B의 대조 기준선이 살아 있다
  - 빌드 명령 (2-B에서 재사용):
    ```
    cd LockFree_Test
    g++-13 -std=c++17 -mcx16 -O2 -DNDEBUG -Wall -Wextra -I. -ILockFree TestCode.cpp -o /tmp/lockfree_test -pthread
    ```
  - **예상과 달리 코드 수정이 거의 없었다.** `TestCode.cpp`에는 이미 `#ifdef _WIN32` 분기가 12곳 있었다(`system("cls")`/`system("clear")`) — 원저자가 이식성을 어느 정도 고려해 둔 상태였고, 그래서 **에러는 처음부터 0**이었다
  - 대신 리눅스에서만 나는 **경고 24건**(glibc `system()`의 `warn_unused_result`)을 `ClearScreen()` 헬퍼로 정리했다. 12개 `#ifdef` 블록이 헬퍼 한 곳으로 모여 중복도 함께 줄었다. 화면 지우기라 테스트 로직과 무관하므로 2-B 대조에 영향 없다
  - 계획서가 말한 "Windows 의존 1곳"은 `USE_RACE_HOOK` 빌드의 `windows.h`다 — 일반 빌드에는 안 들어오므로 **2-C에서 처리**한다

- [x] **2-B** 테스트 전량 실행 + Windows 대조 — **완료 (2026-08-03)**
  - **판정 결과**: headless 모드 **6종 전부 양 OS 종료코드 일치(모두 rc=0)**

  | 테스트 | 인자 | Windows | Linux |
  |---|---|---|---|
  | `repro` | 8 64 10 | rc=0 위반 미검출 | rc=0 위반 미검출 |
  | `cons` | — | rc=0 PASS(완주) | rc=0 PASS(완주) |
  | `fl` | 8 10 | rc=0 위반 미검출 | rc=0 위반 미검출 |
  | `tls` | 4 4 10 | rc=0 위반 미검출 | rc=0 위반 미검출 |
  | `tlsleak` | 20 4 5 | rc=0 누수 확인(알려진 한계) | rc=0 누수 확인(알려진 한계) |
  | `qbuf` | 8 10 | rc=0 무결(버퍼 보존) | rc=0 무결(버퍼 보존) |

  - 위 표는 시간을 줄여 돌린 것이라, **설계된 기본 시간으로 리눅스에서 한 번 더** 확인했다 — `repro`(60초)·`fl`(30초)·`tls`(30초) 전부 rc=0
  - `tlsleak`이 양쪽 모두 "누수 확인"인 것이 중요하다 — **리눅스에서 새로 생긴 문제가 아니라 원래 알려진 한계**다(테스트 자체가 관찰용이라 항상 0을 반환한다)
  - `qbuf`가 rc=1("테스트 무효 — false 0회")이 아니라 rc=0이므로, 검증 조건이 실제로 발생한 유효한 실행이었다
  - **함정**: WSL의 `/tmp`는 세션 사이에 비워진다. 빌드 산출물을 거기 두면 다음 실행에서 `rc=127`이 뜬다 — 스크립트에 재빌드 조건을 넣어 둘 것

- [x] **2-C** `LF_RACE_HOOK` 증폭 빌드로 경합 재현 — **완료 (2026-08-03)**
  - **판정 결과**: 증폭 빌드 컴파일 경고0, 증폭 상태에서 **6종 전부 rc=0**(`repro` 30초 / `cons` / `fl` 20초 / `tls` 20초 / `qbuf` 15초 / `tlsleak`). Windows 빌드·동작도 유지(`cons` rc=0)
  - 2-A에서 이월한 `USE_RACE_HOOK`의 `windows.h`를 없앴다 — `GetCurrentThreadId()`가 유일한 이유였고, 표준 `std::hash<std::thread::id>`로 바꾸니 **양 OS가 같은 코드로 돈다**. 시드에 필요한 성질(스레드별로 다른 값)은 그대로다

  > **증폭이 실제로 걸렸는지부터 확인했다**
  >
  > 증폭 빌드가 통과했다는 것만으로는 부족하다. 매크로가 안 걸렸으면 그냥 일반 빌드를 돌린 것이고, 그래도 초록불이 뜬다 — 2-B의 `qbuf` 무효 케이스와 같은 함정이다. 두 가지로 확인했다.
  > - **테스트가 스스로 찍는 라벨**: 일반 `RACE_HOOK OFF - 대조군` / 증폭 `RACE_HOOK ON` ← 이게 정본이다
  > - **처리량 차이**: 같은 조건(`fl 8 5`)에서 alloc 누적 **25,116,933 → 21,055,238 (−16%)**. 스톨이 실제로 시간을 먹고 있다

  - **함정(2-B에 이어 재발)**: WSL의 `/tmp`는 **wsl.exe 호출마다** 비워진다. 앞 호출에서 만든 바이너리가 다음 호출에서 사라져 `No such file` 이 뜬다 → 빌드 산출물은 `/mnt/c/...` 아래(스크래치패드 등)에 두어야 세션을 넘긴다

- [x] **2-D** TSan / ASan — **완료 (2026-08-03). 선택 페이즈였지만 실행했다**
  - 건너뛰지 않은 이유: 이 자료구조는 `volatile INT64` 기반인데 **MSVC는 x64에서 `volatile`에 acquire/release 시맨틱을 주고 GCC는 주지 않는다**. 이식에서 실제 위험이 있는 지점이라 확인이 필요했다
  - **판정 결과**: TSan은 **모든 테스트에서 race를 보고하지만 리눅스 이식이 만든 것이 아니다**(근거 아래). 테스트 자체 판정은 전부 통과

  > **race가 `__sync` 탓인지 알고리즘 구조 탓인지 — 실험으로 갈랐다**
  >
  > 최초 가설은 "TSan이 `__sync_val_compare_and_swap`(1-B의 CAS128 어댑터)을 원자연산으로 인식하지 못한다"였다. 실제로 `InterlockedCompareExchange128`이 최상위 프레임으로 8건 등장했다.
  > **CAS128만 `__atomic` 판으로 바꾼 사본을 만들어 같은 조건으로 돌렸다**(원본은 그대로 두고 `-I`로 덮어썼다):
  >
  > | 테스트 | `__sync` 판 | `__atomic` 판 |
  > |---|---|---|
  > | `fl 4 5` | race 5 | race 5 |
  > | `cons` | race 29 | race 30 |
  >
  > **수가 그대로다.** TSan이 이해하는 `__atomic`으로 바꿔도 안 줄었으니 `__sync`는 원인이 아니다. 보고의 최다 패턴이 **`Read of size 8` 52건**인 것과 맞물린다 — 락프리가 CAS 직전에 하는 **평문/`volatile` 스냅샷 읽기**가 다른 스레드의 원자 쓰기와 겹치는, 이 알고리즘의 구조 자체다.
  >
  > **정직하게 적자면 "오탐"이라기보다 C++ 표준상으로는 실제 UB다.** `volatile` 읽기는 원자성도 순서도 보장하지 않는다. x86-64에서 자연 정렬 8바이트 로드가 하드웨어적으로 원자적이고 `volatile`이 컴파일러 재배치를 막아 실무적으로 도는 것이다. 근본 해결은 `volatile` → `std::atomic`(relaxed) 전환인데 그건 자료구조 재설계라 **포팅 범위 밖**이다. 리눅스에서 새로 생긴 위험이 아니라는 점이 여기서 중요하다.

  - **ASan 결과**: `fl`은 **누수 0**. TLS 경로에서만 검출 — `tlsleak` 2,567,680 bytes/40건, `tls` 128,384 bytes/2건. 이는 테스트가 원래 "누수 확인(알려진 한계)"로 보고하던 항목이고 **Windows에서도 같은 보고가 난다**(2-B 대조)
  - **1-C의 예고는 빗나갔다**: "`HeapDestroy` 일괄 회수가 사라져 누수로 잡힐 것"이라 적었지만 `fl`에서는 나타나지 않았다. 그 테스트가 모든 노드를 프리리스트로 되돌리고 끝나기 때문이다. 사용 중 노드를 남긴 채 소멸하는 경로에서만 의미가 있다
  - **함정**: TSan은 WSL2에서 `FATAL: unexpected memory mapping`으로 **시작조차 못 한다**(ASLR 충돌). 이때도 rc=66이라 "실행됐는데 깨끗하다"로 오해하기 쉽다 → **`setarch -R`로 ASLR을 끄고 돌릴 것**. 처음 3개 테스트를 이 상태로 "통과"로 셀 뻔했다

---

### 2단계 결산 — 안전성 검사 완료 (2026-08-03)

- 리눅스에서 테스트 6종이 Windows와 **같은 결과**를 낸다(2-B), 경합을 증폭해도 불변식이 유지된다(2-C)
- TSan/ASan이 보고하는 것들은 **양 OS 공통의 알려진 성질**이지 이식이 만든 결함이 아니다(2-D, 대조 실험으로 입증)
- 남은 관찰 항목: `volatile` 기반 스냅샷 읽기(표준상 UB, 실무적으로 동작) / TLS 청크 미회수(원래 알려진 한계)

---

## 3. CrashDump 검증 — ✅ 완료 (2026-08-03)

구현은 이미 끝났다(커밋 `2db4f3d`). 실제로 찍히는지만 본다.

- [x] **3-A** 링크에 `-rdynamic` 추가 — **완료 (2026-08-03)**
  - **판정 결과**: `build.ninja`의 링크 명령 2곳에 반영, 두 타겟 모두 빌드 성공
  - **효과를 대조로 확인했다** — 옵션이 명령줄에 들어간 것만으로는 판정이 약해서, 같은 소스를 `-rdynamic` 없이 링크해 비교했다

    | 링크 | 동적 심볼 수 |
    |---|---|
    | `-rdynamic` 없음 | **1개** |
    | `-rdynamic` 있음 | **100개** |

  - `CrashDump`·`LockFree_OnAllocFail` 관련 심볼 4개가 실제로 export된 것도 확인했다 — 3-B에서 스택에 이름으로 찍힐 대상이다
  - `-mcx16`과 같은 이유로 `add_link_options`에 **전역**으로 걸었다. 앞으로 추가될 타겟(epoll 서버)도 크래시 덤프를 쓰므로 타겟마다 붙이면 빠뜨린다

- [x] **3-B** 크래시 유발 후 로그 확인 — **완료 (2026-08-03). 3단계 졸업**
  - **판정 결과**: `SIGABRT`(CRASH 매크로)·`SIGSEGV`(널 역참조) **양쪽 모두** 핸들러가 잡아 stderr + `crashdump.txt`에 스택을 남긴다. 사유 문자열(`Reason:`)도 기록된다. 헤더 주석의 "리눅스 빌드 환경 전이라 미검증"은 이제 검증됐다

  **이름이 나오는 범위 — 두 갈래다**

  | 함수 종류 | 로그에 찍히는 것 | 예 |
  |---|---|---|
  | extern linkage (클래스 멤버·전역 함수) | **이름**(mangled) | `_Z17ExternProbe_Outerv`, `main`, `_ZN10CCrashDump13SignalHandlerE...` |
  | static / 익명 네임스페이스 | **주소만** | `./crash_probe2_O0(+0x12e8)` |

  - `-rdynamic`은 **동적 심볼 테이블에 올릴 수 있는 것만** 내보낸다. static 함수는 internal linkage라 애초에 대상이 아니다 — 3-A로는 해결되지 않는 종류의 한계다
  - 서버 코드는 대부분 클래스 멤버 함수라 실무에서는 이름이 나오는 쪽이 많지만, 익명 네임스페이스 헬퍼가 스택에 끼면 주소만 남는다

  **주소를 이름으로 되돌리는 절차 (실측 확인)**
  ```
  addr2line -f -C -e <실행파일> 0x12e8      # 함수명 + 소스:줄번호
  grep -oP '_Z[A-Za-z0-9_]+' crashdump.txt | c++filt   # mangled 이름 풀기
  ```
  - 실제로 `0x12e8 → StaticProbe_Inner() (crash_probe2.cpp:18)`까지 복원됐다. **줄 번호가 나오는 건 Windows 미니덤프보다 나은 점**이다(`-g` 필요)
  - 핸들러 안에서 demangle하지 않는 것은 옳다 — `abi::__cxa_demangle`은 malloc을 쓰므로 async-signal-safe가 아니다. 사후 변환이 정석이다

  > **검증 중 걸린 함정**: `-O2`에서는 `volatile int* p = nullptr; *p = 1;`이 **UB 최적화로 통째로 사라져** 크래시가 안 났다. 그대로였으면 "SIGSEGV 경로가 동작한다"고 잘못 기록할 뻔했다(실제로는 그 다음 줄의 SIGABRT 경로가 찍힌 것이었다). SIGSEGV 확인은 `-O0`으로 해야 한다

---

### 3단계 결산 — CrashDump 검증 완료 (2026-08-03)

- 구현은 브랜치에 이미 있었고(커밋 `2db4f3d`) 이번에 **실제로 도는 것을 확인**했다: 시그널 포착 → 스택 기록 → 파일 저장 → 기본 동작으로 재발생
- 링크에 `-rdynamic`이 필요하다는 전제도 대조로 입증했다(동적 심볼 1개 → 100개)
- 남은 한계는 static 함수 이름인데, `addr2line` 사후 변환으로 **줄 번호까지** 얻을 수 있어 실무상 문제가 되지 않는다

---

## 4. epoll 백엔드 ← 최대 작업

**전제가 바뀌었다.** 옛 계획은 `NetIoModel.h`가 `EpollServer.h`를 include하는 구조였지만, main이 전송계층을
`Transport_Iocp.cpp` / `Transport_Rio.cpp`로 이미 갈라놨다. epoll은 **세 번째 팔(`Transport_Epoll.cpp`)** 로 얹는다.
확장점이 이미 뚫려 있어 오히려 유리하다 — 경계 함수 14개(`IOCPServer.h:377-390`)를 구현하면 된다.

- [x] **4-A** 경계 조사 — **완료 (2026-08-03)**. 아래가 4-B·4-C의 작업 목록이다

  **① 전송 경계 14개 — Windows 타입이 박힌 것은 3개뿐이다**

  | 함수 | 문제 |
  |---|---|
  | `DWORD TransportListenFlags() const` | 반환형 `DWORD` |
  | `TransportAttachSession(CSession*, SOCKET)` | 인자 `SOCKET` |
  | `TransportStartFirstRecv(CSession*, SOCKET, int64_t)` | 인자 `SOCKET` |

  나머지 11개는 이미 `bool`/`void`/`const char*`/`int64_t`로 **중립**이다. 경계 설계가 잘 돼 있어 4-B가 가벼워진다.

  **② 골격의 Windows 심볼 — 주석·문자열 제외 실측**

  | 분류 | 건수 | 심볼 |
  |---|---|---|
  | **③ 타입 별칭으로 해결** | **약 95** | `TRUE/FALSE` 38 · `Interlocked*` 22 · `SOCKET` 16 · `INVALID_SOCKET` 10 · `DWORD` 6 · `ZeroMemory` 4 · `SOCKET_ERROR` 2 |
  | **① Platform으로 밀 것** | 약 14 | `closesocket` 5 · `WSAGetLastError` 4 · `WSACleanup` 3 · `WSAStartup` 1 · `WSADATA` 1 |
  | **② Transport 뒤로 밀 것** | 약 9 | `OVERLAPPED` 5 · `HANDLE` 3 · `ULONG_PTR` 2 · `WSASocket` 1 |
  | 표준 그대로 (POSIX 동일) | 6 | `bind`/`listen`/`accept` 3 · `setsockopt` 2 · `htons` 1 |

  합계: `IOCPServer.cpp` 100건 + `IOCPServer.h` 29건

  - **압도적 다수가 ③(타입 별칭)이다.** 실제 구조를 옮겨야 하는 ②는 9건뿐이고, 그것도 `OVERLAPPED`·`ULONG_PTR`은 IOCP 완료 통지 전용이라 `CSession`의 IOCP 분기에 몰려 있다
  - `bind`/`listen`/`accept`/`setsockopt`/`htons`는 Winsock과 POSIX가 **같은 이름**이라 손댈 필요가 없다

  > **계획서에 적어둔 "`WSASend`가 골격에 2곳 남아 있다"는 틀렸다**
  >
  > 실제로 보니 **둘 다 주석**이었다(`IOCPServer.cpp:305` `#if USE_ZERO_SNDBUF` 안의 설명, `:1095` 집계 설명). 코드상 `WSASend`·`WSARecv`는 골격에 **0건**이고 전부 Transport로 이사한 상태다.
  > 1-A와 **똑같은 실수**를 반복했다 — grep이 주석을 셌다. 이번엔 계획서 경고대로 주석·문자열을 걷어내고 다시 셌고, 위 표는 그 결과다.
  > 그래도 이 표를 완전한 목록으로 믿지 말 것 — **4-C에서 실제로 컴파일해보면 여기 없는 것이 더 나온다**(1단계가 네 번 그랬다).

- [x] **4-B** `[회귀빌드]` 경계 타입 중립화 — **완료 (2026-08-03)**
  - **판정 결과**: Windows Release 전체 재빌드 **IOCP 팔 경고0 오류0 / RIO 팔 경고0 오류0**, `BuildConfig.h` 원복 확인. 리눅스에서는 `static_assert`로 별칭 성립 검증(`NetSocket`=`int` 4바이트, `kInvalidSocket`=-1)
  - 바꾼 것은 4-A가 짚은 **경계 3개**뿐이다 — `DWORD TransportListenFlags()` → `uint32_t`, `TransportAttachSession/StartFirstRecv`의 `SOCKET` → `Platform::NetSocket`. 선언 1곳 + 구현 2팔 × 3 = 7곳
  - `Platform.h`에 `NetSocket`/`kInvalidSocket` 별칭 신설, `IOCPServer.h`에 include 추가

  > **Windows 쪽을 `SOCKET`이 아니라 `UINT_PTR`로 정의한 이유**
  >
  > `SOCKET`은 `<WinSock2.h>`에 있는데 그 헤더는 `<Windows.h>`보다 먼저 와야 한다는 순서 제약이 있다. `Platform.h`가 그 제약을 자기를 include하는 모든 파일에 퍼뜨리지 않도록, 같은 타입인 `UINT_PTR`(`<Windows.h>` 제공)로 정의했다. winsock2.h 원문이 `typedef UINT_PTR SOCKET`이라 타입은 정확히 일치하고, **빌드 통과 자체가 그 증거다** — 골격이 `SOCKET` 변수를 그대로 넘기는데 타입이 달랐으면 깨진다.

  - **다음 단계에서 주의할 점**: 두 OS에서 `NetSocket`의 **크기와 부호가 다르다**(Windows `UINT_PTR` 8바이트 부호없음 / 리눅스 `int` 4바이트 부호있음). 실패 판정도 다르다 — Windows는 `INVALID_SOCKET`(최대값), 리눅스는 `-1`. `sock < 0` 같은 비교를 쓰면 Windows에서 영원히 거짓이 되므로, 판정은 반드시 `kInvalidSocket`과의 등가 비교로 할 것

- [x] **4-C** `[회귀빌드]` 골격에서 Windows 전용 코드 밀어내기 — **완료 (2026-08-03)**
  - **판정 결과**: 리눅스에서 `IOCPServer.cpp` **컴파일 에러 0**, Windows Release 재빌드 **IOCP 팔·RIO 팔 경고0 오류0**, `BuildConfig.h` 원복 확인
  - 에러가 줄어든 경과: **148 → 88 → 61 → 46 → 44 → 3 → 0**

  **Platform에 들인 것** (4-A 분류의 ①에 해당)
  - 소켓 헤더 분기 — `<WinSock2.h>`/`<WS2tcpip.h>`/`<Windows.h>` ↔ `<sys/socket.h>` 계열. **WinSock2가 Windows.h보다 먼저여야 하는 순서 제약을 이 한 곳에 가뒀다**
  - 소켓 API 4종 — `SocketStartup`/`SocketCleanup`/`CloseSocket`/`LastSocketError`, 그리고 논블로킹 판정용 `WouldBlock`
  - 스칼라 타입·매크로 — `LONG`/`LONGLONG`/`BOOL`/`ULONG_PTR`/`SOCKADDR(_IN)`/`LINGER`/`SOCKET`, `TRUE`/`FALSE`/`ZeroMemory`/`Sleep`/`SOMAXCONN_HINT`/`INVALID_SOCKET`/`SOCKET_ERROR`
  - **32비트 원자연산 4종** — `InterlockedIncrement`/`Decrement`/`Exchange`/`CompareExchange`. LockFreeCompat이 64·16비트만 덮어서(락프리 자료구조가 그 폭만 쓴다) 골격의 `volatile LONG` 상태 플래그용으로 새로 필요했다

  **플랫폼 분기로 남긴 것** (②에 해당 — 개념 자체가 없는 것들)
  - `OverlappedEx`의 `OVERLAPPED` 멤버 — epoll은 "제출하고 완료를 돌려받는" 모델이 아니라 "fd가 준비됐다"는 통지라 대응물이 없다. `operation`/`slot`은 양쪽 공통이라 남겼다
  - `HandleCompletion` 선언, `WSASocket`(플래그 개념이 리눅스에 없음 → `::socket`), `accept`의 `addrLen`(POSIX는 `socklen_t*`)

  > **버그 하나를 덤으로 잡았다 — `LockFreeCompat.h`의 `ULONG`/`DWORD` 폭**
  >
  > `unsigned long`으로 정의돼 있었는데 **리눅스 LP64에서는 64비트**다(Windows는 32비트). 1단계에서는 그 타입이 힙 어댑터 인자로만 쓰여 드러나지 않았지만, 골격이 같은 이름을 쓰면서 `conflicting declaration`으로 터졌다. 양쪽을 `uint32_t`로 통일했다 — Windows 폭과 일치한다.
  > `LONGLONG`도 처음 `int64_t`로 뒀다가 `LONG64`(=`long long`)와 충돌했다. LP64에서 `int64_t`는 `long`이라 `long long`과 다른 타입이다.

  > **전처리기 분기를 넣다 기존 `#if`를 깨뜨렸다**
  > `HandleCompletion` 앞에 `#ifdef _WIN32`를 넣었더니, 그 뒤의 `#endif`가 바깥 `#if !USE_RIO_TRANSPORT`를 닫던 것이라 `unterminated #if`가 났다. 조건부 컴파일 안에 또 조건부를 넣을 때는 **닫는 짝을 눈으로 세어볼 것**.

- [x] **4-D** `Transport_Epoll.cpp` 뼈대 — **완료 (2026-08-03). 리눅스에서 서버가 뜬다**
  - **판정 결과**:
    ```
    [main.cpp:72] Network I/O model: epoll
    [Transport_Epoll.cpp:106] [Network] Server started — epoll workers=4 (affinity cores=12, Mode: GameServer)
    [GameServer.cpp:347] [GameServer] Started - Mode: GameServer
    [MonitorServer.h:92] [MonitorServer] Listening on port 9090
    ```
    포트 6000 `LISTEN` 확인, 클라 1개 **TCP 연결 성공** 후 해제까지 정상
  - **서버 전체 12개 TU가 리눅스에서 컴파일·링크된다.** 4-C에서 골격만 열었는데, 나머지 11개는 대부분 그대로 통과했고 아래만 손봤다
    - `SerialBuffer.cpp` — `memcpy_s` 4곳·`wcslen`. `memcpy_s`는 MS 확장이라 **대상 크기 검사가 리눅스에서 사라진다**(호출부가 이미 범위를 계산해 넘기는 자리들이지만, 방어 한 겹이 빠진다는 사실은 Platform 주석에 남겼다)
    - `CoreAffinity.cpp` — `SetThreadAffinityMask` → `Platform::SetCurrentThreadAffinity`(pthread) 신설
    - `main.cpp` — `LoggerGuard`가 `USE_SPDLOG_LOGGER` 안에 있어 정의 필요
    - `MonitorManager.h` — `LONG64` 별칭 누락
    - `NetIoModel.h` — **계획서가 지적한 낡은 전제를 정리했다**(아래)

  > **`NetIoModel.h`를 현재 구조에 맞게 다시 썼다**
  >
  > 옛 설계는 OS마다 서버 클래스를 따로 두고(`CEpollServer` 등) 이 헤더에서 고르는 것이었다. 그 사이 전송 계층이 `Transport_*.cpp`로 분리되면서 **클래스를 나눌 이유가 사라졌다** — 갈리는 것은 "제출·수거 방식"뿐이고 세션 관리·패킷 분해는 공통이다. 그래서 `using NetIoModel = CIOCPServer` 하나로 두고, 이름표(`kNetIoModelName`)만 IOCP/RIO/epoll로 고른다.

  - **DB 워커를 빌드 옵션으로 뺐다** — WSL 안에 MySQL이 없어 서버가 `DB init failed`로 죽었다. 네트워크 뼈대 검증에 DB는 불필요하므로 `option(MMO_USE_DB OFF)`로 기본 비활성. 5단계 실동작에서 `-DMMO_USE_DB=ON`으로 켠다. `BuildConfig.h`의 `USE_DB_WORKER`에 `#ifndef` 가드를 둬 빌드 시스템이 이길 수 있게 했다

  **이번 페이즈에서 채운 것 / 남긴 것**
  - 채움: epoll 인스턴스 생성·해제, 워커 스레드(`epoll_wait` 루프), accept 소켓 등록(`EPOLLIN|EPOLLRDHUP` + 논블로킹 전환), 끊김·오류 처리, 종료 유도(`EPOLL_CTL_DEL` + `shutdown`)
  - 남김(스텁): `PostRecv`(4-F), `TransportSubmitSegment`·`TransportSendImmediate`·`TransportFlushDirty`(4-G)
  - **IOCP와 다른 점 하나**: 종료 시 IOCP는 `PostQueuedCompletionStatus`로 워커를 깨웠지만 epoll엔 그런 "가짜 완료"가 없다. `epoll_wait`에 100ms 타임아웃을 줘 주기적으로 `_running`을 확인하게 했다

- [x] **4-E** 세션 관리 이식 — **완료 (2026-08-03)**
  - **판정 결과**: 슬롯 누수 없음

    | 시나리오 | created | destroyed | count | accept_failed |
    |---|---|---|---|---|
    | 접속·해제 **100회 반복** | 100 | 100 | **0** | 0 |
    | **동시 20개** 접속 중 | 20 | 0 | 20 | 0 |
    | 동시 20개 해제 후 | 20 | 20 | **0** | 0 |

  - 세션 관리 코드(인덱스 스택·uniqueId·refcount)는 골격에 있어 **한 줄도 옮기지 않았다**. 대신 수명의 마지막 고리 하나가 빠져 있었고, 그걸 찾아 채운 것이 이번 페이즈다

  > **결함: 끊어도 세션이 반환되지 않았다 (created 1 / destroyed 0 / count 1)**
  >
  > 4-D 뼈대는 클라가 붙는 것까지만 봤는데, 지표를 보니 **끊어도 슬롯이 그대로 남았다**. 100명이 들락날락하면 슬롯이 말라 접속을 못 받게 되는 결함이다.
  >
  > 원인은 두 모델의 ref 반환 주체가 다르다는 데 있었다.
  > - **IOCP**: `CancelIoEx`가 걸려 있던 I/O를 실패로 완료시키고, 그 **완료 통지를 받은 워커**가 `IOCountDecrement`를 부른다 → IOCount가 0으로 수렴 → `ReleaseSession`
  > - **epoll**: 걸린 I/O가 없으니 그런 통지가 **영영 오지 않는다**. `Initialize`가 세운 `IOCount=1`을 아무도 놓지 않아 세션이 영구히 사용 중으로 남는다
  >
  > epoll에서 `IOCount=1`의 의미는 "pending I/O 하나"가 아니라 **"epoll에 등록되어 있다"**다. 그래서 등록을 빼는 `TransportRequestDisconnect`에서 직접 `IOCountDecrement`를 부르도록 했다. `InterlockedExchange(_disconnecting)` 게이트가 이 경로를 한 번만 통과시킨다(IOCP 팔과 같은 방식).
  >
  > **이 차이는 4-F·4-G에도 그대로 적용된다** — epoll 경로에서는 "완료가 ref를 놓는다"는 IOCP의 전제를 쓸 수 없다. 수신·송신을 채울 때 ref 증감의 짝을 각 자리에서 명시적으로 맞춰야 한다.

- [x] **4-F** 수신 경로 — **완료 (2026-08-03)**
  - **판정 결과**: 헤더 4바이트 패킷 20개 전송 →

    | 지표 | 값 |
    |---|---|
    | `mmo_recv_packets_total` | **20** (보낸 수와 일치) |
    | `mmo_recv_bytes_total` | **80** (4바이트 × 20, 정확) |
    | `mmo_wsa_recv_calls_total` | 20 |
    | `mmo_packet_errors_total` | **0** |
    | 파싱 에러 로그 | **0줄** |

  - 링버퍼 이동과 패킷 분해는 골격의 `ProcessRecv`가 이미 다 한다. epoll이 할 일은 **"몇 바이트 읽었는지"를 넘기는 것뿐**이라 새 코드가 짧다
  - 구현: `EPOLLIN` 통지 → `recv()` 루프 → `ProcessRecv(session, n)`. 루프를 도는 이유는 한 번에 링의 직선 구간만큼만 읽히기 때문이고, `n < writable`이면 커널 버퍼를 다 비운 것이라 빠져나온다
  - **ref 짝은 이 함수가 직접 맞춘다** — 4-E에서 확인한 대로 epoll엔 ref를 놓아 주는 완료 통지가 없다. 진입에 `AcquireSession`, 종료에 `IOCountDecrement`
  - 종료 조건 넷을 구분했다: `n>0`(정상) / `n==0`(상대 FIN → 종료 유도) / `EAGAIN`(더 읽을 것 없음 → 정상 탈출) / `EINTR`(재시도)

  > **첫 시도는 실패했고, 그게 오히려 경로를 증명했다**
  > 프로토콜을 모른 채 임의 바이트를 보냈더니 `Invalid packet size: 0`으로 거부되고 `packet_errors 2`가 찍혔다. **거부됐다는 것 자체가 read → 링버퍼 → 파서까지 도달했다는 증거**였다. 이후 실제 헤더 형식(`MsgHeader{uint16 size, uint16 type}`, size는 헤더 포함 전체 크기)으로 맞추니 에러 0으로 통과했다.

- [x] **4-G** 송신 경로 — **완료 (2026-08-03). (c)+(a) 결합**
  - **판정 결과**: 클라 12개 동시 접속 + 하트비트 30라운드

    | 항목 | 값 |
    |---|---|
    | 서버 `mmo_send_bytes_total` | 1064 |
    | 클라 12개가 받은 총 바이트 | **1064 (완전 일치 → 유실 0)** |
    | `mmo_send_packets_total` / `mmo_wsa_send_calls_total` | **44 / 22 → 패킷 2개당 syscall 1회 (코얼레싱 작동)** |
    | `partial_send` / `send_queue_overflow` / `send_contention` | 0 / 0 / 0 |

  - 순서는 TCP가 보장하고, 링버퍼 구간을 두 스레드가 겹쳐 보내지 않도록 `_sendSubmitBusy` 잠금이 "제출 구간 계산 ~ writev"를 덮는다(게임 스레드와 epoll 워커가 같은 세션을 만질 수 있다)
  - 구현: `GetSubmitInfo()`로 미전송 구간을 얻어 `writev`(링이 감기면 iovec 2개) → 보낸 만큼 `MarkSubmitted` + `ConsumeSubmitted`. **제출과 완료가 한 호출에서 끝난다**
  - `EPOLLOUT`은 부분 전송·`EAGAIN`일 때만 등록하고 다 보내면 해제한다. 같은 상태면 `epoll_ctl`을 부르지 않도록 `_epollWantWrite` 표식을 뒀다

  > **남은 것: 송신 워커 분리 (정확성 아닌 성능)**
  > 확정 방침은 "게임루프 → dirty 배치 → **송신워커**"였는데, 지금은 `TransportFlushDirty`에서 **게임 스레드가 직접 `writev`를 부른다**. 코얼레싱(틱 끝 배치 → 세션당 1회 전송)은 이미 살아 있지만, **게임 스레드가 syscall에 붙잡히는 문제**는 그대로다.
  > Windows에서 이걸 분리했을 때 tick p99가 39.18 → 17.38로 떨어졌다(SendThread 분리 실험). 같은 이득이 리눅스에서도 나올 것으로 보지만 **정확성이 아니라 성능 사안**이라 4-J로 넘긴다 — 지금 섞으면 이번 판정("유실·순서")이 흐려진다.

  > **골격이 팔에 요구하는 것은 `TransportSubmitSegment` 하나뿐이다 (착수 전 조사)**
  >
  > 골격 `PostSend`(`IOCPServer.cpp:757`)가 공통 부분을 다 갖고 있다: 세션 pin, 제출 잠금, 깊이 게이트, 링버퍼 미제출 구간 계산, 슬롯 할당.
  > 문제는 **그 계약이 "제출과 완료가 분리된다"는 IOCP 전제 위에 서 있다**는 것이다.
  > - IOCP: `WSASend` 제출 → 나중에 완료 통지 → 그때 슬롯 반환·ref 감소·링 소비
  > - epoll: `write()`가 **그 자리에서** 보낸 바이트를 돌려준다. 기다릴 완료가 없다

  **확정 방침 — "기존 구조를 지킨다"가 아니라 "epoll에 최적인 것만 남긴다"**

  | 요소 | 처리 | 근거 |
  |---|---|---|
  | 게임루프 → dirty 배치 → 송신워커 | **유지** | 코얼레싱·게임스레드 보호는 **모델과 무관한 최적화**다. 리액터에서도 게임 루프가 send syscall에 붙잡히면 tick p99가 무너진다 |
  | 워커가 `writev`로 전송 | **즉시 완료** | epoll 본성. 보낸 바이트를 그 자리에서 알 수 있으므로 완료를 기다릴 이유가 없다 |
  | 슬롯 링·`_sendInFlight`·깊이 게이트 | **우회** | 이것이 진짜 IOCP 유산이다. epoll에서는 "이미 끝난 일을 적는 장부"라 짐만 된다 |
  | `EPOLLOUT` | **부분 전송 때만 등록** | 평시 등록해 두면 보낼 것이 없어도 통지가 계속 온다 |

  - `_sendDepth`(다중 pending)는 **리눅스에서 1로 고정**된다. RIO에서 얻은 깊이 실험은 Windows 자산으로 남기고, 6단계 비교는 **양쪽 다 코얼레싱을 켠 상태**로 맞춰 공정성을 확보한다
  - 성능 튜닝(엣지 트리거 등)은 여기서 하지 않는다 — 정확성 판정이 흐려진다. **4-J로 분리**했다

- [x] **4-H** TimingWheel·타임아웃 연동 — **완료 (2026-08-03). 코드 수정 없이 통과**
  - **판정 결과**: 무응답 클라(A)와 하트비트 클라(B)를 함께 붙여 대조

    | 시점 | `session_count` | `session_timed_out` | `session_destroyed` |
    |---|---|---|---|
    | +5초 (둘 다 접속) | 2 | 0 | 0 |
    | +45초 | 2 | 0 | 0 |
    | **+70초** | **1** | **1** | 1 |
    | 최종 (둘 다 종료) | 0 | 1 | 2 |

  - **무응답 A만 끊기고 하트비트 B는 살아남았다.** 이 대조가 핵심이다 — `RequestRefresh`(수신 시 수명 갱신)가 동작하지 않았다면 **둘 다** 타임아웃됐을 것이다. 즉 타임아웃과 갱신 두 경로가 함께 검증됐다
  - 코드를 고치지 않았다. `CTimingWheel`은 `std::thread`·`steady_clock`·`sleep_for`만 쓰는 플랫폼 독립 구현이고(브랜치가 `Sleep`을 `sleep_for`로 이미 바꿔 뒀다), 등록·갱신 호출도 골격 쪽(`RequestRegister`는 accept 경로, `RequestRefresh`는 `ProcessRecv`)에 있어 **epoll 팔이 손댈 것이 없었다**
  - 설정: `SESSION_TIMEOUT_SEC = 60`, `TIMER_TICK_INTERVAL_MS = 1000` (`IOCPServer.h:522-523`, 하드코딩)

- [x] **4-I** `[회귀빌드]` 왕복 검증 — **완료 (2026-08-03). 4단계 정확성 졸업**
  - **판정 결과**: 리눅스 서버에서 접속·이동·채팅이 모두 왕복한다

    | 단계 | 확인 |
    |---|---|
    | 접속 | `S2C_ZONE_INFO(1010)` + `S2C_CREATE_MY_PLAYER(1004)` 수신 |
    | 이동 | 이동 40회 → `recv_packets 40`(전부 파싱) / `broadcast_calls 31`(게임 로직 수행) / `move_budget_rejects 0` / 클라가 **31개 되받음** |
    | 채팅 | `S2C_CHAT(1008)` 26바이트 수신, 서버 에러 0 |

  - Windows 회귀빌드: **IOCP 팔 경고0 오류0 / RIO 팔 경고0 오류0**, 리눅스 빌드도 정상, `BuildConfig.h` 원복
  - 클라 2개를 붙였을 때 서로에게 브로드캐스트가 가지 않은 것은 **결함이 아니라 AOI가 동작하는 증거**였다(시야 밖 스폰). 이동으로 확인한 것은 "자기 이동이 자기에게 돌아오는" 왕복이다

  > **회귀빌드가 실수를 세 개 잡았다** — 리눅스만 보고 넘어갔으면 Windows가 깨진 채로 갔다
  > 1. `_epollWantWrite`(리눅스 전용 멤버)를 `ResetTransportState()`에서 조건 없이 참조 → Windows에서 미선언 식별자
  > 2. `Platform.h`가 `<WinSock2.h>`를 들이자 **`sockaddr` 재정의**가 터졌다. 이 헤더는 여러 곳에서 쓰이는데, 그중 하나라도 `<Windows.h>`를 먼저 들인 TU가 있으면 구버전 `winsock.h`가 딸려온다. `WIN32_LEAN_AND_MEAN`으로도 못 막았다(먼저 들인 TU에는 그 매크로가 없다) → **소켓 헤더 순서 책임을 `IOCPServer.h`로 되돌렸다.** Platform.h는 리눅스 소켓 헤더만 제공한다
  > 3. 그 과정에서 `WIN32_LEAN_AND_MEAN`이 `mmsystem.h`를 빼 `timeBeginPeriod`가 사라졌다 → `<timeapi.h>` 직접 include
  >
  > 교훈: **"한곳에 가둔다"가 항상 옳지는 않다.** Windows의 소켓 헤더 순서는 이미 검증된 배치가 있었고, 그걸 공용 헤더로 끌어올리자 순서를 통제할 수 없게 됐다.

---

### 4단계 정확성 결산 (4-A ~ 4-I, 2026-08-03)

리눅스에서 **접속 → 이동 → 채팅**이 도는 MMO 서버가 섰다.

- 골격은 그대로 두고 `Transport_Epoll.cpp` 하나를 새로 썼다. 세션 관리·패킷 분해·타임아웃은 손대지 않았다
- 두 모델의 차이가 실제로 문제가 된 곳은 **두 군데**였다
  - **ref 반환 주체**(4-E) — IOCP는 완료 통지가 놓지만 epoll엔 그 통지가 없다. 등록 해제 자리에서 직접 놓도록 고쳤다
  - **제출/완료 분리**(4-G) — `writev`가 그 자리에서 끝내므로 슬롯 링·깊이 게이트를 우회했다
- 나머지(수신·타임아웃)는 골격이 이미 플랫폼 중립이라 얇게 잇는 것으로 끝났다

- [대기] **4-J** epoll 튜닝 (성능) — **측정 환경(5단계)이 전제라 뒤로 미룬다**
  - 판정이 "항목별 A/B 수치"인데 부하 클라가 붙기 전에는 잴 것이 없다. **5단계를 먼저 끝내고 돌아온다**
  - 정확성이 확보된 뒤에 성능을 붙인다. 섞으면 "느려진 것"과 "틀어진 것"을 구분할 수 없다
  - **최우선 후보 — 송신 워커 분리** (4-G에서 이월): 지금은 게임 스레드가 직접 `writev`를 부른다.
    Windows에서 같은 분리로 tick p99가 **39.18 → 17.38**로 떨어졌으므로 리눅스에서도 재현되는지 본다.
    기존 SendWorker 풀(K3, uniqueId%K 분배) 구조를 그대로 쓰되 `WSASend` 자리를 `writev`로 바꾸면 된다
  - 후보 (각각 켜고 끄며 측정):
    - **엣지 트리거(`EPOLLET`)** — 통지 수가 준다. 대신 "`EAGAIN`이 날 때까지 읽는다"는 규칙을 어기면 조용히 멈춘다
    - **`EPOLLEXCLUSIVE`** — 다중 워커가 같은 리슨 소켓을 기다릴 때 thundering herd 방지
    - **`SO_REUSEPORT`** — 워커별 리슨 소켓을 따로 두어 accept 경합 자체를 없앤다
    - **`TCP_NODELAY`** — 이미 켜져 있는지부터 확인(골격에 주석 처리된 자리가 있다)
    - **`MSG_MORE` / `TCP_CORK`** — 코얼레싱 보조. 애플리케이션 묶음과 중복이면 이득이 없을 수 있다
  - **판정** → 항목별로 A/B 수치와 채택/기각 근거를 남긴다 (기각도 자산이다)

---

## 5. 실동작 검증

- [x] **5-0** 프로토콜 문자 폭 고정 — **완료 (2026-08-04)**

  > **`wchar_t`는 Windows 2바이트 / 리눅스 4바이트다.** 5단계는 "Windows 부하 클라 → 리눅스 서버"인데, 채팅 프로토콜이 `sizeof(wchar_t)`에 의존해 **와이어 포맷이 OS마다 달라진다**. 지금 고치지 않으면 5-B에서 채팅이 깨진다.
  >
  > 발견 경위: 4-I에서 채팅 패킷을 조립하다가 `MSG_C2S_CHAT`의 `wchar_t message[512]`가 리눅스에서 2048바이트가 되는 것을 확인했다(주석은 "1024바이트"라고 적혀 있다 — Windows 전제).

  - 영향 범위 **9곳** (채팅 경로에 국한)
    - `Protocol.h` 3 — `CHAT_MSG_MAX_LEN` 주석, `MSG_C2S_CHAT::message`, `MSG_S2C_CHAT::message`
    - `GameServer.cpp` 5 — 195·198(가변 길이 계산) / 850(최소 크기) / 1043·1044(경계 정렬·글자 수)
    - `SerialBuffer.cpp` 1 — `operator<<(const wchar_t*)`의 `wcslen`
  - **`char16_t`로 가는 이유**: Windows에서 `wchar_t`가 이미 2바이트라 **와이어 포맷이 바뀌지 않는다** — 기존 클라·더미와 그대로 호환된다. UTF-8 전환은 클라까지 손대야 해서 범위가 커진다
  - **판정 결과**: 양 OS 크기 완전 일치

    | 타입 | Windows | Linux |
    |---|---|---|
    | `ChatChar` | 2 | 2 |
    | `MSG_C2S_CHAT` | **1028** | **1028** |
    | `MSG_S2C_CHAT` | **1034** | **1034** |

    Windows Release 재빌드 **경고0 오류0**, 리눅스에서 **UTF-16 형식 채팅 왕복 확인**(`S2C_CHAT` 18바이트 수신, 서버 에러 0)
  - `using ChatChar = char16_t`를 `Protocol.h`에 두고 9곳을 그 별칭으로 바꿨다. `MakeChat`의 인자 타입도 함께 교체(컴파일러가 잡아 줬다)
  - 고친 뒤 클라 쪽 전송 형식은 `msg.encode('utf-16-le')` — **Windows 클라가 보내던 바이트 배치 그대로**다. 와이어 포맷이 안 바뀌었다는 뜻이고, 이것이 `char16_t`를 고른 이유다

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
