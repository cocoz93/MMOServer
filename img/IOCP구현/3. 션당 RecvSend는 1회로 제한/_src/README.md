# 인포그래픽 소스 (`_src`)

상위 폴더(`3. 션당 RecvSend는 1회로 제한`)에 있는 설명 이미지들의 **편집 가능한 소스**다.
Notion 페이지 [세션당 Recv/Send는 1회로 제한](https://app.notion.com/p/34116a0b9f5980839d4ad67dc23f9996)을 그림으로 요약한다.
HTML/CSS 로 만들고 **헤드리스 Chrome 으로 3배(3x) 캡처**해서 PNG 로 뽑는다. 폰트는 **Pretendard**(가변)를 `fonts/` 에 임베드한다.
톤은 공용 `common.css` 하나로 통일한다(참고 톤: `../1. AcceptThread 는 분리/_src/01_v4.html` 의 파랑 계열 아키텍처 + `../7. 섹터맵.../_src/common.css` 의 그림자·토큰 체계).

---

## 1. 이미지 목록 (2026-07-03)

| 최종 이미지 (상위 폴더) | HTML 소스 | 논리크기 → 출력(3x) | 내용 |
|---|---|---|---|
| `01_pending_limit.png` | **소스 없음** (아래 주의) | — | 최상단 히어로 — "세션당 딱 1개" 개수 대비. |
| `02_completion_order.png` | `02.html` | 1520×824 → 4560×2472 | **완료 통지 순서** — 완료는 FIFO로 와도 스레드1·스레드2가 나눠 처리하면 누가 먼저 끝낼지 몰라 순서가 뒤섞임(❓). 왼→오 가로 흐름. |
| `03_page_lock.png` | `03.html` | 1520×960 → 4560×2880 | **Page Lock 부담** — 정상(순간 pin) vs backpressure(누적 → non-paged pool 압박). 상한선일 뿐 진짜 이유 아님. |
| `04_one_pending_pattern.png` | `04.html` | 1520×918 → 4560×2754 | **업계 1-Pending 패턴** — Send 큐 사이클(항상 1개) + Recv도 세션당 1개. |
| `05_page_lock_basics.png` | `05.html` | 1520×846 → 4560×2538 | **Page Lock 개념(Windows I/O & Page Locking)** — WSASend 시 커널이 유저 버퍼의 물리 페이지를 pin(고정)하고 스왑·이동을 막는다. **Notion 배치는 `03` 앞**. |

> ⚠️ **`01_pending_limit.png` 은 소스(HTML)가 이 폴더에 없다.** 이전 세션에서 만든 뒤 `/img/` 가 `.gitignore` 대상이라 유실된 것으로 보인다.
> Notion 원문 최상단에는 이미 업로드돼 있으므로 **그대로 유지**한다(재생성 불필요). 만약 다시 편집해야 하면 개수 대비(좌 채택: 세션마다 pending 1 / 우 기각·흐림: 한 세션에 1·2·3 누적 + 자물쇠) 구도로 새로 만들면 된다.

**구성/배치**: 원문은 "① 무엇을(1개로 제한) · ② 왜(이유) · ③ 어떻게(1-pending 동작)" 로 구성된다.
①은 최상단 요약(`01`), ②는 완료통지(`02`)·Page Lock 개념(`05`)·Page Lock 부담(`03`), ③은 업계 패턴(`04`).
**Notion 3번(Page Lock) 섹션 배치 순서는 개념 `05` → 부담/결론 `03`** ("무엇→왜 문제") 로 넣는다 — 파일 번호와 배치 순서가 다르니 주의.
"로직 단순화"(이유 1)는 글이 강점이라 그림화하지 않고 원문 텍스트에 맡겼다(각 그림 하단 콜아웃에 핵심 한 줄로 녹임).

---

## 2. 빠른 재빌드

HTML 을 고친 뒤 실행하면 상위 폴더의 PNG 가 갱신된다. **Windows 는 build.ps1 권장(실제 렌더 검증됨)**:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```
```bash
bash build.sh   # 동일 로직(bash) — PowerShell 쪽으로만 검증함
```

- 의존: **Chrome**(없으면 자동 **Edge**).
- ⚠️ **Chrome 149 대응 3종이 스크립트에 이미 반영돼 있다**(메모리 `project-infographic-render-fix`): ① `--headless=new` ② 폴더 경로의 공백·한글 때문에 **공백 없는 ASCII 임시폴더(`%TEMP%\mmo-render-work`)에 스테이징 후 렌더** ③ `Start-Process -Wait`.
- ⚠️ **`--allow-file-access-from-files` + `common.css`/`fonts` 스테이징이 핵심.** HTML 이 `common.css` 를 상대경로로 `<link>` 하고, `common.css` 가 `fonts/PretendardVariable.woff2` 를 `@font-face` 로 로드한다. 셋 중 하나라도 스테이징에서 빠지면 폰트/톤이 깨진다.

---

## 3. 디자인 시스템 (`common.css`)

**톤**: 파랑 계열(구조/정상) + 빨강(위험/대비) + 초록(03 정상 패널). 배경은 도트 텍스처 + 대각선 그라디언트, 카드 상단 6px 파랑 액센트 바, 2단 레이어드 그림자.
**공통 컴포넌트**: `.eyebrow`(번호 배지) · `.title`/`.rule`/`.sub`(헤더) · `.badge.ok/.bad/.blue` · `.takeaway`(하단 빨강 콜아웃) · `.foot`(원문 링크 안내).
**폰트**: Pretendard(`fonts/PretendardVariable.woff2`, 가변). 색·그림자 토큰은 `common.css :root` 참고.

각 HTML 은 자체 `<style>` 에 **캔버스 크기(width/height)와 그 장 특화 스타일만** 둔다. `--window-size` 는 HTML `body` 의 width/height 와 **반드시 일치**(build 스크립트에 이미 맞춰져 있음).

---

## 4. 파일 / 주의

- `common.css` — 공용 톤(3장 공유). `02.html`·`03.html`·`04.html` — 각 장 소스. `fonts/` — 임베드 폰트.
- `build.ps1`(검증됨)·`build.sh`(동일 로직, 미검증) — 재빌드, 3x.
- 레포 `.gitignore` 가 `/img/` 를 통째로 제외 → PNG·HTML·CSS·스크립트·폰트 **전부 git 미추적**(디스크에만). git 이력으로 대조 불가하니, 수정 때마다 이 README 를 실제 파일 기준으로 갱신할 것.
- `build.sh` 는 **LF·BOM 없음** 유지(CRLF/BOM 이면 bash 깨짐). HTML/CSS 는 UTF-8(BOM 불필요, `<meta charset="utf-8">`).
- 캔버스 높이(02=824·03=960·04=918·05=846). ⚠️ 04는 노드 3개+화살표 2개(cnodes)의 총높이가 cycle 카드 안쪽(cyc-body flex:1)을 초과하면 맨 아래 완료 처리 노드가 카드 하단 밖으로 밀려 겹친다 — 요소를 키울 땐 캔버스 높이도 같이 키워 cnodes가 카드 안에 들어가는지 확인할 것. 2026-07-03 **"시원한 스케일"로 재구성** — 요소·폰트·여백을 키우고(폴더 7 톤에 맞춤), `common.css`도 v2로 마감 상향(그림자 3단+inset 하이라이트, 배경 광원, 제목 41px). 02 는 "스레드1/스레드2 경쟁 → ❓" 왼→오 가로 흐름. 내용을 늘릴 땐 늘어난 만큼만 넓힌다.
