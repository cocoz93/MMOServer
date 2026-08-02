# 파이프라인 밖 도구 (유물)

저장소에 남아 있지만 현재 빌드·테스트 파이프라인에 들어 있지 않은 도구를 기록한다.
지우지 않은 이유와, 되살리려면 무엇이 필요한지를 함께 적는다.

---

## StressTest/AcceptStressTest

**무엇을 재는가**

서버의 accept 처리 능력만 따로 재는 도구다. N개 워커 스레드가
`socket → connect → (0~N ms 대기) → closesocket`을 반복하면서
초당 접속 수(CPS)와 `connect()` 소요시간(avg/max/min, μs)을 1초 주기로 출력한다.
게임 패킷은 한 바이트도 주고받지 않는다.

**현재 상태 (2026-07-28 확인)**

- 실질 변경은 2026-05-05(`98e881d`)가 마지막. 이후는 디렉터리 정리 커밋뿐
- `Run/.IOCP_build.bat` 빌드 목록에 없음
  (IOCP_Server / GameClient / EchoStressClient / MMOStressClient 4개만 빌드)
- `Run/bin/`에 산출물 없음. 빌드물은 프로젝트 로컬 `x64/{Debug,Release}/`에만
- 어떤 `.bat` / `.ps1` / 문서도 이 이름을 참조하지 않음
- 설정이 INI가 아니라 실행 시 콘솔 대화형 입력(IP / Port / ThreadCount / DelayMin / DelayMax)이라
  **측정 조건을 재현할 기록이 남지 않는다**

**왜 지우지 않았나**

측정 축이 다른 도구와 겹치지 않는다.
`3. MMO_stress`는 접속을 유지한 채 게임플레이 패킷 부하를 재고,
이쪽은 접속 수립·해제 회전율과 `connect()` 지연만 잰다.
서버의 accept 경로와 세션 슬롯 재사용(`IOCPServer.cpp`의 `_availableIndices`)을
직접 때리는 유일한 도구다.

서버 헤더를 하나도 include하지 않는 자립형 WinSock 프로그램(툴셋 v143)이라
프로토콜이 바뀌어도 깨지지 않고, 지금 열어도 그대로 빌드된다.

**되살리려면**

1. 노션 `서버 검증 및 클라 구현` DB에 항목 신설
2. `Run/.IOCP_build.bat` 빌드 목록에 편입
3. 대화형 입력을 INI로 전환 — 측정 조건이 파일로 남아야 수치를 근거로 쓸 수 있다

셋 다 하기 전에 뽑은 수치는 재현할 수 없으므로 근거로 쓰지 않는다.
