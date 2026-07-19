# 리눅스 포팅 계획 (feat/cmake-netiomodel)

Windows IOCP 서버를 리눅스로 옮기는 작업의 남은 순서.
각 항목에 **판정** 기준을 붙였다. 판정이 통과해야 다음으로 넘어간다.

## 이미 끝난 것 (브랜치 커밋 11개)

- Windows CMake Release 빌드 (경고0 오류0)
- Windows 스칼라 타입 → cstdint 고정폭 (75곳). `DWORD=unsigned long`이 리눅스 LP64에서 8바이트라 필수
- Platform 격리 계층 (타이머·경로·affinity·종료 시그널)
- INI 로더를 플랫폼 독립 파서로 교체 (`GetPrivateProfile*` 제거)
- 크래시 덤프 분기 (Windows 원본 `#ifdef` 보존 + Linux sigaction/backtrace) — 커밋 `6c8fbc7`
- 통계 카운터 `Interlocked` → `std::atomic`
- 로거 시각 `localtime_r` 분기, DB winsock 분기
- 독립 epoll 에코 PoC (`poc/EpollEchoServer.h`) — 서버 본체와는 미연결

## 방향 결정 (재론 금지)

커스텀 컴포넌트(SerialBuffer·LockFree)는 **유지하고 포팅**한다. 라이브러리로 교체하지 않는다.

- 큐·스택을 라이브러리로 바꿔도 `CExternalTlsFreeList`(SerialBuffer 풀)가 `CInternalFreeList`를 청크 풀로 쓰므로 128비트 CAS 문제가 그대로 남는다 (`ExternalTlsFreeList.h:6,91` → `InternalFreeList.h:284`)
- 진행 중인 "자체 큐 vs 외부 8종" 벤치의 비교 대상이 사라진다
- custom vs 라이브러리 비교는 **백엔드 고정 A/B**로 따로 한다. OS 포팅과 묶으면 두 변수가 섞인다

---

## 0. 리눅스 환경 (선행)

이게 없으면 1~3을 다 짜도 컴파일조차 못 돌린다.

- **0-1** WSL2 + Ubuntu 설치 ← **직접 실행** (관리자 권한·재부팅)
  - 현재 상태: WSL 본체 2.7.11.0 + 커널 6.18.33.2 **이미 설치됨**. Windows 선택 기능만 꺼져 있음
  - BIOS 가상화는 이미 켜져 있음 (`VirtualizationFirmwareEnabled: True`, i9-10900)
  - 관리자 PowerShell: `wsl --install --no-distribution` → 재부팅 → `wsl --install -d Ubuntu-24.04`
- **0-2** `gcc-13 cmake ninja libmysqlclient-dev` 설치
- **0-3** 경로: `/mnt/c/Users/USER/Desktop/MyGit` 직접 사용
  - LockFree를 형제 경로(`../../../LockFree/`)로 참조하는 전제가 유지돼야 한다 (`LockFreeConfig.h:39`)
- **판정** → `cmake -S . -B build-linux` **configure 통과** (컴파일은 아직 실패해도 됨)

## 1. LockFree 포팅

- **1-1** 남은 Windows 심볼 전수 조사 (`Interlocked*`, `__forceinline`, `INT64`, `windows.h`)
  - 사전 조사: LockFree 헤더의 Windows 의존은 `InternalFreeList.h` 2곳뿐. 나머지는 테스트 코드와 CrashDump
  - 128 CAS 호출부는 8곳 — `LockFreeQueue.h` 6 / `LockFreeStack.h:219` 1 / `InternalFreeList.h:284` 1
- **1-2** `LockFreeCompat.h` 신설 — CAS128 어댑터 + 타입 별칭 + 인트린식 분기
  - 어댑터로 감싸는 이유는 호출부 8곳을 안 고치기 위해서다. MSVC판은 128비트를
    `high`/`low` 64비트 둘로 쪼개 받고, GCC판은 128비트 값 하나로 받는다.
    `__atomic_compare_exchange_n`이 expected를 in/out으로 받아 시맨틱은 그대로 맞는다
  - 16바이트 정렬 요건은 이미 코드에 있다 (`LockFreeQueue.h:69` `alignas(16)`)
- **1-3** 헤더 4개에서 `windows.h` 경로 차단 (`SerialBuffer.h`가 이걸 타고 딸려온다)
- **1-4** CMake에 `-mcx16` 추가
  - **함정**: 이 플래그가 없으면 GCC가 `cmpxchg16b` 대신 libatomic **뮤텍스 폴백**으로
    조용히 내려간다. 락프리인 줄 알고 락을 쓰게 된다.
    `static_assert(__atomic_always_lock_free(16, 0))`로 컴파일 단계에서 막을 것
- **판정** → ① Windows 빌드 경고0 오류0 **불변** ② 리눅스에서 헤더 4개 컴파일 통과

## 2. 안전성 검사

- **2-1** `LockFree_Test`를 리눅스에서 빌드 (`TestCode.cpp`의 Windows 의존 1곳 정리)
- **2-2** 기존 테스트 전량 실행
- **2-3** `LF_RACE_HOOK` 증폭 빌드로 경합 재현 (`-fsanitize=thread` 병행 검토)
- **판정** → Windows에서 통과하던 항목이 리눅스에서 **동일하게** 통과

## 3. CrashDump 검증

구현은 이미 끝났다(커밋 `6c8fbc7`). 실제로 찍히는지만 본다.

- **3-1** 링크에 `-rdynamic` 추가 — 없으면 backtrace에 함수명이 안 나온다
- **3-2** 의도적 널 역참조 / abort 유발
- **판정** → 로그에 **함수명 포함** 스택이 남는지

## 4. EpollServer 풀 백엔드 ← 최대 작업

`NetIoModel.h:13`이 `EpollServer.h`를 include 하는데 그 파일이 아직 없다.
지금 있는 건 독립 에코 PoC뿐이라 서버 본체와 연결이 안 돼 있다.

- **4-1** `CIOCPServer`의 공개 표면 목록화 (게임로직이 실제로 부르는 것만)
- **4-2** 뼈대: 리스너 + 워커 스레드 + `epoll_wait` 루프
- **4-3** 세션 관리 이식 (인덱스 스택·uniqueId·refcount — 대부분 그대로 옮겨진다)
- **4-4** 수신 경로 (링버퍼 + 패킷 분해)
- **4-5** 송신 경로 ← **IOCP와 가장 다른 곳**
  - IOCP는 "보내달라고 걸어두면 완료를 통지"하는데, epoll은 "보낼 수 있게 되면 알려줌"이다
  - SendWorker와 코얼레싱을 `EPOLLOUT` 등록/해제 방식으로 재설계해야 한다
  - 기존 자산: Send Coalescing(syscall −94%), SendThread 분리, SendWorker Pool(K3, uniqueId%K 분배)
- **4-6** TimingWheel·타임아웃 연동
- **판정** → 클라 1개로 **접속 → 이동 → 채팅** 왕복

## 5. 실동작 검증

- **5-1** WSL2 포트포워딩 (기본 NAT라 외부에서 안 붙는다)
- **5-2** Windows 부하 클라 → 리눅스 서버, 100명
- **5-3** 1,000명
- **판정** → 기존 기준 그대로 `loop_p99 < 40ms`, `buffer_full = 0`

## 6. IOCP vs epoll 비교

- **6-1** 같은 시나리오 A/B 수집
- **6-2** 결과 정리 (포트폴리오 소재)
- **주의**: WSL2에서 낸 수치는 성능 비교로 쓸 수 없다. 가상화 + NAT 오버헤드 때문에
  IOCP가 이기는 게 당연해진다. 6번만은 실기 리눅스(듀얼부팅) 또는 클라우드 인스턴스가
  필요하다. 0~5는 WSL로 충분하다.

---

## 인코딩 규칙

- 소스 `.h/.cpp` = UTF-8 BOM + LF, `CMakeLists.txt` = BOM + CRLF
- BOM 보존 편집은 PowerShell `[IO.File]::WriteAllText(..., UTF8Encoding($true))`
- 줄바꿈 검증은 CR 생카운트 말고 `git diff`로
