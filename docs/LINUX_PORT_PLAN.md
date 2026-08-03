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

## 2. 안전성 검사

컴파일이 아니라 **동작**을 본다. 락프리 자료구조는 컴파일만으로는 아무것도 보장하지 않는다.

- [ ] **2-A** `LockFree_Test`를 리눅스에서 빌드 (`TestCode.cpp`의 Windows 의존 1곳 정리)
  - **판정** → 테스트 실행 파일이 만들어짐

- [ ] **2-B** 기존 테스트 전량 실행 + Windows 결과와 대조
  - **판정** → Windows에서 통과하던 항목이 리눅스에서 **동일하게** 통과 (항목 수까지 대조)

- [ ] **2-C** `LF_RACE_HOOK` 증폭 빌드로 경합 재현
  - **판정** → 증폭 상태에서도 자료구조 불변식이 깨지지 않음

- [ ] **2-D** `-fsanitize=thread` 빌드 (선택 — 2-C가 깨끗하면 건너뛸 수 있음)
  - **예상되는 누수 보고 1건**: 소멸 시점에 사용 중이던 노드는 해제되지 않는다. Windows의 `HeapDestroy` 일괄 회수를 표준 할당자로 옮기면서 사라진 동작이다(1-C 참조) — 결함이 아니라 알려진 차이로 기록할 것
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
