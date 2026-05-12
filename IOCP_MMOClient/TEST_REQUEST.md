# 테스트 요청: IOCP_MMOClient

## 프로젝트 개요
- IOCP 기반 MMO 게임의 Windows 콘솔 클라이언트
- C++ / WinSock2 / MSVC (.sln/.vcxproj)
- 서버: IOCP_MMOServer (별도 프로젝트, 같은 리포지토리)
- 프로토콜: TCP 스트림, 커스텀 바이너리 패킷 (Protocol.h 공유)

## 파일 구조

### 클라이언트 (검증 대상)
```
IOCP_MMOClient/
├── mainClient.cpp          — 진입점
├── GameInstance.h/cpp       — 게임 루프, 입력, 이벤트 처리, 채팅 커맨드
├── ClientNetwork.h/cpp      — WinSock 연결, TCP 스트림 조립, 패킷 송수신
├── ClientPlayer.h           — Direction/MoveState enum, ClientPlayer 구조체
├── PlayerManager.h          — 내 캐릭터 + 타 플레이어 관리, 이동 보간
├── NetworkEventQueue.h      — 스레드 간 이벤트 큐 (mutex)
├── ConsoleRenderer.h/cpp    — 콘솔 렌더링 (WriteConsoleOutputW)
```

### 서버 (비교 기준, 수정 대상 아님)
```
IOCP_MMOServer/
├── Player.h                 — Direction/MoveState enum, MOVE_SPEED 정의
├── Zone.cpp                 — Tick() 이동 공식 (방향별 좌표 증감)
├── GameServer.cpp           — RecvMoveStart/Stop/Chat/ZoneChange 처리
├── ZoneManager.h/cpp        — 맵/채널 관리, GetRandomMapId
```

### 공유
```
Shared/Protocol/Protocol.h   — MsgHeader, C2S/S2C 패킷 구조체 전체
```

## 스레딩 모델
- **메인 스레드**: GameLoop (25fps) — 입력, 이벤트 소비, PlayerManager 조작, 렌더링
- **수신 스레드**: RecvThread — recv → TCP 조립 → DispatchPacket → OnXxx 콜백
- **규칙**: OnXxx 콜백은 이벤트 큐에 Push만 수행, PlayerManager 직접 접근 금지
- **검증 포인트**: OnXxx에서 PlayerManager를 직접 건드리는 코드가 있으면 버그

## 주요 구성요소

| 모듈 | 역할 |
|------|------|
| CGameInstance | 게임 루프 (25fps), 입력/이벤트 처리, 전체 흐름 제어 |
| CClientNetwork | WinSock 연결, 수신 스레드, 패킷 송수신 |
| CNetworkEventQueue | 수신 스레드 → 게임 루프 스레드 간 이벤트 전달 (mutex 큐) |
| CPlayerManager | 내 캐릭터 + 주변 플레이어 좌표/상태 관리 |
| CConsoleRenderer | 콘솔 더블버퍼링 렌더 (상태바 + 게임뷰 + 채팅) |

## 현재 상태 / 알려진 이슈
- Room/Tetris 기반 → Zone/Channel 기반으로 전면 리팩터링 완료
- 기존 파일 삭제(Room.h/cpp, Tetris.h/cpp), 새 파일 5개 추가
- MSVC Debug/x64 빌드 확인 완료
- 알려진 경고: `std::string(serverIp.begin(), serverIp.end())` wchar_t→char 변환 C4244 (기존 코드, 미수정)

## 테스트 환경
- OS: Windows 10
- 컴파일러: MSVC v143 (Visual Studio 2022)
- 서버 주소: 127.0.0.1:6000 (로컬 테스트)

---

# 테스트 실행 가이드

## 3회차 분할 구조

전체 체크리스트를 한 번에 요청하면 에이전트가 후반부를 얕게 처리합니다.
아래 3회차로 나눠서 요청하세요. 각 회차는 독립적으로 수행 가능합니다.

```
1회차: 기반이 맞는지 (빌드, 패킷 구조, 에러 처리)
  ↓ 여기서 문제 나오면 2~3회차 의미 없음
2회차: 핵심 로직이 맞는지 (스레드, 이동, 이벤트)
  ↓ 가장 버그 가능성 높은 영역
3회차: 기능이 맞는지 (존 이동, 채팅, 화면)
```

---

## 1회차: 빌드 + 패킷 처리 + 에러 처리

### 읽어야 할 파일
- `Shared/Protocol/Protocol.h`
- `IOCP_MMOClient/ClientNetwork.h`
- `IOCP_MMOClient/ClientNetwork.cpp`
- `IOCP_MMOClient/IOCP_MMOClient.vcxproj`

### 체크리스트

#### 빌드 검증
- [ ] MSVC 솔루션 빌드 성공 여부 (Debug/x64)
- [ ] vcxproj에 등록된 파일과 실제 파일이 일치하는가?
- [ ] 삭제된 파일(Room.h/cpp, Tetris.h/cpp 등)이 vcxproj에 남아있지 않은가?

#### 패킷 처리
- [ ] RecvThread에서 패킷이 2개 이상 한 번에 도착했을 때 전부 처리되는가?
- [ ] 패킷이 절반만 도착했을 때 다음 recv까지 대기하는가?
- [ ] header.size가 비정상(0, 버퍼 초과)일 때 안전하게 처리하는가?
- [ ] DispatchPacket이 Protocol.h의 S2C 타입 10종을 전부 커버하는가?
- [ ] 각 SendXxx 함수의 header.size가 sizeof(해당 구조체)와 정확히 일치하는가?

#### 에러 처리
- [ ] recv 반환값 <= 0 시 연결 종료 처리가 되는가?
- [ ] 수신 버퍼 오버플로우(8192 초과) 시 안전하게 종료하는가?
- [ ] 알 수 없는 MsgType 수신 시 크래시 없이 무시하는가?

### 요청 예시
> "TEST_REQUEST.md를 참고해서 **1회차 (빌드 + 패킷 처리 + 에러 처리)** 체크리스트를 점검해줘.
> 읽어야 할 파일: Protocol.h, ClientNetwork.h/cpp, vcxproj"

---

## 2회차: 스레드 안전성 + 이벤트 완전성 + 이동 동기화

### 읽어야 할 파일
- `Shared/Protocol/Protocol.h`
- `IOCP_MMOServer/Player.h` (비교 기준)
- `IOCP_MMOServer/Zone.cpp` (비교 기준 — Tick 함수의 이동 공식)
- `IOCP_MMOClient/ClientPlayer.h`
- `IOCP_MMOClient/PlayerManager.h`
- `IOCP_MMOClient/NetworkEventQueue.h`
- `IOCP_MMOClient/GameInstance.cpp` (OnXxx 콜백 + ProcessNetworkEvents + ProcessInput)

### 체크리스트

#### 스레드 안전성
- [ ] OnXxx 콜백 10종에서 `_playerManager`를 직접 접근하는 코드가 없는가?
- [ ] OnXxx 콜백에서 `_eventQueue.Push()` 외의 공유 상태 접근이 없는가?
- [ ] `_connected` / `_running`이 atomic으로 선언되어 있는가?
- [ ] `CNetworkEventQueue`의 Push/Pop이 mutex로 보호되는가?

#### 이벤트 처리 완전성
- [ ] S2C 10종 → OnXxx → 이벤트 큐 → ProcessNetworkEvents 매핑에 누락이 없는가?
- [ ] 각 OnXxx에서 패킷 필드가 이벤트 필드로 빠짐없이 복사되는가?
- [ ] MOVE_STOP 이벤트에서 내 캐릭터와 타 캐릭터를 구분 처리하는가?
- [ ] SYNC_POSITION에서 내 캐릭터 좌표를 강제 덮어쓰는가?

#### 이동 동기화 (서버 비교)
- [ ] ClientPlayer.h의 Direction enum 값이 서버 Player.h와 동일한가? (NONE=0, UP=1, DOWN=2, LEFT=3, RIGHT=4)
- [ ] MOVE_SPEED가 서버와 동일한가? (5.0f)
- [ ] ApplyMovement의 방향별 공식이 서버 Zone.cpp Tick과 동일한가? (UP→y-=, DOWN→y+=, LEFT→x-=, RIGHT→x+=)
- [ ] ProcessInput의 키 인덱스 0~3이 UP/DOWN/LEFT/RIGHT 순서로 매핑되는가?
- [ ] 키 누름 시 기존 방향 MoveStop → 새 방향 MoveStart 순서가 맞는가?
- [ ] 키 뗌 시 현재 방향과 일치할 때만 MoveStop을 보내는가?

### 요청 예시
> "TEST_REQUEST.md를 참고해서 **2회차 (스레드 안전성 + 이벤트 완전성 + 이동 동기화)** 체크리스트를 점검해줘.
> 서버 Player.h와 Zone.cpp Tick 함수를 기준으로 클라이언트 이동 공식을 비교 검증해줘."

---

## 3회차: 존 이동 + 채팅 + 렌더링

### 읽어야 할 파일
- `IOCP_MMOClient/GameInstance.h`
- `IOCP_MMOClient/GameInstance.cpp` (ProcessChatInput + HandleChatCommand)
- `IOCP_MMOClient/ConsoleRenderer.h`
- `IOCP_MMOClient/ConsoleRenderer.cpp`
- `IOCP_MMOServer/GameServer.cpp` (비교 기준 — RecvZoneChange)
- `IOCP_MMOServer/ZoneManager.h` (비교 기준 — GetRandomMapId)

### 체크리스트

#### 존 이동
- [ ] ZONE_CHANGE_OK 수신 시 Clear() → SetMyPlayer 순서가 맞는가?
- [ ] `/map random` → SendZoneChange(-1)이 호출되는가?
- [ ] `/map <id>` → SendZoneChange(id)이 호출되는가?
- [ ] 서버 RecvZoneChange에서 targetMapId == -1 처리가 있는가?
- [ ] ZONE_CHANGE_FAIL 수신 시 reason별 메시지가 올바른가?

#### 채팅
- [ ] 채팅 모드 진입 시 이동 중이면 MoveStop을 전송하는가?
- [ ] 채팅 모드 진입 시 `_keyPressed` 배열을 초기화하는가?
- [ ] `/` 접두사 입력 시 SendChat 대신 HandleChatCommand가 호출되는가?
- [ ] 일반 채팅은 SendChat(wchar_t*)로 전송되는가?
- [ ] 참고: `_getch()` 기반이므로 현재 한글 입력 미지원 (ASCII 32~126만 허용)

#### 렌더링
- [ ] 게임 뷰가 WriteConsoleOutputW 단일 호출로 출력되는가? (깜빡임 방지)
- [ ] 카메라가 내 캐릭터 중심으로 계산되는가?
- [ ] 레이아웃: STATUS(0행) + VIEW(1~22행) + CHAT(23~36행) + INPUT(37행)이 맞는가?

### 요청 예시
> "TEST_REQUEST.md를 참고해서 **3회차 (존 이동 + 채팅 + 렌더링)** 체크리스트를 점검해줘.
> 서버 GameServer.cpp RecvZoneChange와 ZoneManager GetRandomMapId를 기준으로 존 이동 로직을 비교해줘."

---

## 출력 형식

각 체크리스트 항목에 대해:
- **OK** — 문제 없음
- **ISSUE** — 문제 발견 (파일:라인 + 근거 + 수정 제안)
- **WARN** — 동작은 하지만 개선 권장 (근거 필수)

최종 요약: **문제 N건 / 경고 M건**

## 주의사항
- 서버 코드는 비교 기준일 뿐, 수정하지 말 것
- 코딩 스타일/네이밍 지적 불필요 (기존 컨벤션 유지 원칙)
- 추측성 성능 지적 금지 — 실측 근거 없는 "이게 느릴 수 있다"는 불필요
- 문제 없으면 억지로 지적하지 말고 "문제 없음" 명시

---

## 사용 팁

1. **회차별로 요청해라** — 한 번에 전부 넣으면 후반부를 얕게 처리한다.
2. **1회차부터 순서대로** — 빌드/패킷이 깨져 있으면 2~3회차 검증이 무의미하다.
3. **현재 상태를 솔직히 적어라** — 변경 이력을 명시해야 에이전트가 맥락을 잡는다.
4. **체크리스트를 활용해라** — 범위만 지정하면 얕게 본다. 구체적 질문이 깊은 검증을 유도한다.
5. **읽어야 할 파일을 명시해라** — 에이전트가 탐색에 시간 낭비하는 것을 방지한다.
