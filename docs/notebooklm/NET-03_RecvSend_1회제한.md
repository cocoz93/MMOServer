# 03. 세션당 Recv/Send는 1회로 제한

출처: 네트워크 라이브러리 설계&구현 (9장 중 3장)
노션: https://app.notion.com/34116a0b9f5980839d4ad67dc23f9996
추출: 2026-08-12

> **요약: 세션당 걸어 두는 Recv·Send를 1개로 제한한다.**
> 완료를 기다리는 IO가 하나면 완료 통지도 하나씩만 와서 순서·경합 고민과 완료 처리가 단순해진다 — 개수를 늘리면 단순함이 깨지고, 성능 이득은 아직 실측하지 않았다.

> [그림: 01_pending_limit]

## 1회로 제한하는 이유

### 1. 로직 단순화

다중 Recv-Send 시 IO버퍼/순서 설계/IO pending 관련 로직이 매우 복잡해지고 디버깅도 어려움. IO를 여러번 해서 얻는 이득보다 리스크(코드복잡성)가 더 크다고 판단 (아래 서술)

### 2. 동일 소켓·동일 I/O 타입 기준, 전송 순서는 보장되지만 완료 통지 순서는 보장되지 않음

> [그림: 02_completion_order]

TCP 소켓에서 동일 타입 I/O(Send끼리, Recv끼리)를 여러 번 걸면, 커널은 요청 순서대로 처리하므로 데이터가 나가는 순서는 제출 순서대로다.

**즉, 여러 Overlapped 구조체로 Send를 여러번 호출하면 전송 순서는 보장되지만(TCP, 동일 소켓, 동일 I/O 타입 한정) 완료 통지 순서는 보장되지 않는다.**

완료 워커가 여럿이라 나중에 건 I/O의 완료 통지가 먼저 꺼내질 수 있다. 이 서버의 다중 pending 구현도 이 전제 위에 짰다 — 구체적으로 뭐를 막는지는 아래 「이 서버는 1-Pending을 어떻게 구현했나」에 적었다.

(단, 여러 Overlapped를 여러 스레드가 나눠 처리하면 문제 생길 여지 있음. 이건 Overlapped 개수를 1개로 제한하는 것과는 별개 문제 — 해법은 Overlapped 개수를 줄이는 게 아니라 순서를 지키는 장치를 따로 두는 것이다)

#### MSDN 참고

※ MSDN의 "no guarantees about the order" 문구는 완료 통지 순서에 그대로 적용된다. TCP·동일 소켓·동일 I/O 타입이라도 완료 통지가 제출 순서대로 오진 않는다.

단, 위 "나가는 순서 보장"은 한 스레드가 순서대로 걸었을 때 이야기다. 여러 스레드가 같은 소켓에 제각기 Send를 걸면 어느 쪽이 먼저 커널에 닿을지 모르므로 나가는 순서부터 보장되지 않는다.

> [그림: MSDN 인용 캡처]

> "Please note that while the packets are queued in FIFO order they may be dequeued in a different order."
> (MSDN: "There are no guarantees about the order in which overlapped operations complete.")

**결론적으로 완료 통지 순서가 뒤섞여도 동작 자체는 깨지지 않는다. 대신 순서를 맞추는 코드를 따로 짊어져야 한다 — 그게 1절에서 말한 복잡도다.**

### 3. Page Lock — 이유라기보다 상한선

> [그림: 03_page_lock]

Overlapped Send 중 커널은 **원본 유저 버퍼**를 잠그고(pin) SO_SNDBUF로 복사한다. 정상 시엔 복사가 즉시 끝나 **pinning은 순간적 — 실질 부담 없음**.
오직 **backpressure(수신자가 느려 SO_SNDBUF가 가득 참)**일 때만 pending이 길어져 버퍼가 계속 잠기고, `outstanding × 세션 수`만큼 쌓이면 non-paged pool을 압박(최악 `WSAENOBUFS`).

즉 Page Lock은 overlapped을 무한정 늘리지 말라는 과부하 상한선일 뿐, 평상시 1-pending의 결정적 이유는 아니다. 실질 이유는 맨 위 요약에 적은 완료 처리 단순화이고, Page Lock 최소화는 따라오는 부수 효과다.

> Windows non-paged pool 한도는 시스템 RAM에 비례한다. `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management`의 `NonPagedPoolSize`는 값을 직접 지정할 때 쓰는 항목이라 기본은 0(시스템 자동)이다 — 실제 사용량은 성능 모니터의 `Pool Nonpaged Bytes`나 `poolmon`으로 본다.

> [그림: 05_page_lock_basics]

---

## 이 서버는 1-Pending을 어떻게 구현했나

> [그림: 04_one_pending_pattern]

- **Recv** — 세션당 하나. 받은 것을 전부 파싱한 뒤 그 자리에서 다시 건다. 그래서 한 세션의 수신 처리는 언제나 한 줄로 이어진다.
- **Send** — 운영값은 깊이 1(`SendDepth=1`)이고, 코드는 2/4/8까지 받는다(`MAX_SEND_DEPTH=8` · 2의 거듭제곱만 허용). 깊이를 올려도 순서가 깨지지 않도록 장치를 둘 뒀다 — 제출 구간을 잠가 나가는 순서를 고정하고(`_sendSubmitBusy`), 완료는 링 head부터 연속으로 done인 구간만 회수한다(`ReapSendSlots`).
- **완료 워커는 4개**(`WorkerThreads=4`). 워커를 하나로 줄여 순서를 맞추는 대신, 순서를 맞추는 장치를 따로 둔 쪽을 골랐다.

**Recv** — TCP는 스트림이라 순차 처리 필수. WSARecv를 여러 개 걸어도 여러 완료 워커가 완료 통지를 꺼내가면(GQCS = GetQueuedCompletionStatus) 처리 순서가 꼬여 복잡해지므로, 게임 서버에선 세션당 1개가 사실상 유일한 선택지. (고처리량 파일 서버·스트리밍 등은 다중 WSARecv 사례도 있음.)

---

## 깊이를 올리면 이득이 있는가

다중 pending이 이득을 내는 자리는 **완료 통지가 늦게 돌아와 다음 제출을 막을 때**, 하나 뿐이다. 깊이 1이면 보낼 것이 남아 있어도 앞 완료가 안 왔다는 이유만으로 그 틱을 통째로 밀린다.

다만 이득을 보려면 아래 셋이 **동시에** 성립해야 한다.

1. **게임 스레드는 여유** — 보낼 데이터가 제때 생산된다
2. **커널 송신 버퍼도 여유** — 상대가 느려 SO_SNDBUF가 찬 상황(backpressure)이면 깊이를 올려도 커널이 받아주지 않는다. 잠긴 버퍼만 쌓인다(위 3절 Page Lock)
3. **완료 왕복만 틱 주기(40ms)를 넘긴다** — `GameServer.h:195`

이 서버는 코얼레싱으로 세션당 틱 1회 제출이라 ③이 성립할 여지가 좁다. 게임 스레드가 밀리면 틱이 길어져 제출 간격이 더 벌어지므로 필요성은 오히려 **더 떨어진다**. 반대로 ③이 실제로 벌어질 만큼 완료가 늦다면 그건 완료 워커가 이미 포화된 상태라, 처방도 깊이보다 워커 수·완료 처리 경량화가 먼저다.

**참고 자료** — MSDN WSASend 함수 · "Windows via C/C++"(Jeffrey Richter) · Microsoft IOCP 샘플
