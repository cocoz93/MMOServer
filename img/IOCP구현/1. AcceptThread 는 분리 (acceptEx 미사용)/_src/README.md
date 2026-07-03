# 인포그래픽 소스 (`_src`)

이 폴더는 상위 폴더(`IOCP구현`)에 있는 **설명 이미지의 편집 가능한 소스**다.
이미지는 HTML/CSS 로 만들고 **헤드리스 Chrome 으로 3배(3x) 캡처**해서 PNG 로 뽑는다.
폰트는 **Pretendard**(가변폰트)를 `fonts/` 에 임베드해서 쓴다 — PPT 에 바로 쓸 프레젠테이션 품질.
기억(메모리) 없는 세션이 와도 이 문서 + HTML 만 보면 바로 이어서 작업할 수 있다.

---

## 1. 무엇에 소스가 있나

| 최종 이미지 (상위 폴더) | HTML 소스 | 논리크기 → 출력(3x) | 원본 페이지 |
|---|---|---|---|
| `01_accept_thread_separation.png` | `01.html` | 1200×650 → 3600×1950 | Notion: [AcceptThread 는 분리 (acceptEx 미사용)](https://app.notion.com/p/34116a0b9f59802eaf47c3ff8e15e082) |

---

## 2. 빠른 재빌드

HTML 을 고친 뒤 **둘 중 아무거나** 실행하면 상위 폴더의 PNG 가 갱신된다:

```bash
bash build.sh
```
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

- 의존: **Chrome**(없으면 자동 **Edge**) — 스크립트가 알아서 찾는다.
- ⚠️ **`--allow-file-access-from-files` 플래그가 핵심.** 이게 있어야 `fonts/PretendardVariable.woff2` 를 `file://` 로 로드한다. 빠지면 폰트가 Malgun Gothic 으로 폴백돼 톤이 깨진다. (build 스크립트엔 이미 들어있음.)

### 수동으로 한 장만 렌더
```bash
CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
"$CHROME" --headless --disable-gpu --hide-scrollbars --allow-file-access-from-files \
  --force-device-scale-factor=3 --screenshot="<이 폴더 절대경로>/01.png" --window-size=1200,650 \
  "file:///<이 폴더 절대경로>/01.html"
```

---

## 3. 편집 흐름 / 구조

**01 현재 구조 (파이프라인 다이어그램)**: `Listen Socket → Accept Thread(전용 스레드) → BindIOCP(등록) → Worker Pool` 로 흐르는 가로 파이프라인 한 줄(`.row.r1`, 실선 화살표) 아래에, 흐리게 깔리는 `AcceptEx 미사용` 고스트 행(`.row.r2`, 점선 화살표 + `.node.ghost`)을 겹쳐 "실제로 쓰는 경로(위, 진하게) vs 안 쓰는 경로(아래, 흐리게)" 를 한 다이어그램 안에서 대비시킨다.
- 좌우 끝은 2행을 병합한 `.hub`(`grid-row:1/3`): 좌 Listen Socket(중립 회색 아이콘 칩), 우 Worker Pool(빨강 계열 아이콘 칩 + 점 6개 그리드 — 제목의 "Worker" 강조색과 톤을 맞춤).
- 하단은 `.takeaway` 콜아웃 바(빨강 톤 배경 + 아이콘)로 핵심 문장을 카드처럼 분리해 강조.

> ⚠️ **문서 불일치 정정 (2026-07-01)**: 바로 아래 "버전 히스토리"는 이 문서가 마지막으로 갱신됐던 시점(같은 날 오전)의 기록으로, 그때는 좌우 2패널 원형 비교 레이아웃을 "확정"으로 적어뒀다. 하지만 실제 `01.html`/`01.png`는 그 이후 위에 설명한 **파이프라인 구조로 이미 교체**되어 있었다 — README 갱신이 누락된 것으로 보인다(`/img/`가 `.gitignore` 대상이라 git 이력으로 대조 불가). 아래 히스토리는 과거 기록으로만 남기고, 실제 최종 구조는 위 설명 기준.

> **버전 히스토리(5단계, 당시 기록)**: (1) mermaid 재현+비교그리드 → "장황함". (2) 3노드 압축 → "구조 안 보임". (3) 세로 깔때기 아키텍처(Listen Socket→Accept→IOCP 큐→Worker) → "여전히 아쉬움"(job-queue 비유 조사까지 했으나 사용자가 그 방향 자체를 원치 않아 재질문 유도). (4) 원 1개 vs 무리 단일 패널(분리 강조) → 방향은 맞았지만 **"이걸 좌우로 대비해서 보여달라"** 는 사용자의 직접 지정을 받음. (5) 당시 기록상 최종: (4)의 원 그래픽 언어를 재사용해 좌(분리)/우(통합) 2패널로 배치 재구성.
> **교훈(여전히 유효)**: 처음 세 번(1~3단계)은 내가 구성을 짐작해서 만들었고 계속 빗나갔다. 반면 (4)에서 만든 "원 크기/개수/거리" 라는 **비주얼 언어 자체는 옳았다** — 여러 차례 반려당하면, 새 시각적 은유를 또 만들어내려 하기보다 **이미 검증된 그래픽 요소를 재사용**하면서 사용자에게 "레이아웃을 말로 정확히 지정해달라"고 요청하는 편이 빠르다.

## 3-1. 2026-07-01 추가 작업 — 시각 품질 업그레이드 (v2, PPT급)

"밋밋하다"는 피드백으로, 내용·구조는 그대로 두고 **비주얼 실행 품질만** `Send Coalescing` 폴더의 `common.css` v2 스타일에 맞춰 끌어올림:
- 캔버스: 플랫 배경(`#eef1f5`) → 대각선 그라디언트(`#eef1f7`→`#e5e9f1`)
- 카드: 단일 그림자 → 2단 레이어드 그림자 + 상단 6px 그라디언트 액센트 바(블루 계열 — 구조/아키텍처 문서라서 Send Coalescing 의 red/orange 계열과 의도적으로 구분)
- 제목 위에 eyebrow 배지(`① 구조 · IOCP ACCEPT 처리`) 추가 — 이전엔 제목이 바로 시작해 위계가 약했음
- Listen Socket / Worker Pool 아이콘: 플랫 회색 박스 → 그라디언트 파스텔 칩(+ 안쪽 하이라이트)
- 프로세스 노드(Accept Thread/BindIOCP): 그림자 깊이 강화, 상단 액센트 바는 유지
- 하단 결론 문장: 빈 여백에 텍스트만 떠 있던 것 → 아이콘 + 배경색 콜아웃 바(`.takeaway`)로 카드화 — 이게 "밋밋함"의 체감 원인 대부분이었음
- 캔버스 높이: 580 → 650 (콜아웃 바 추가로 늘어난 콘텐츠 높이에 맞춤, 실제 렌더 후 하단 여백 보고 타이트하게 확정)

> **버그 메모**: `.card{display:flex;flex-direction:column}` 상태에서 `.eyebrow{display:inline-flex}` 만 쓰면 flex 컬럼의 cross-axis stretch 때문에 배지가 카드 전체 폭으로 늘어난다 — `align-self:flex-start` 필수(common.css 원본엔 있었는데 처음 이식할 때 빠뜨렸다가 렌더 확인 중 발견·수정).
> **높이 확정 방법**: 정확한 높이를 처음부터 추정하지 않고, 넉넉히 큰 캔버스(760)로 한 번 렌더 → 콘텐츠가 끝나는 지점을 눈으로 확인 → 남는 여백만큼 줄여 재렌더, 총 2회 반복으로 수렴시켰다.

고친 뒤 `bash build.sh` 또는 `build.ps1` → 상위 PNG 갱신 → 확인.

## 3-2. 2026-07-01 추가 작업 — 아이콘/레인(lane) 업그레이드 (v3)

v2 렌더 후에도 "밋밋하다"는 재피드백을 받아, 이미 승인된 형제 인포그래픽(`../../2. Send Coalescing.../_src/common.css`)과 직접 대조해서 **객관적 격차**를 찾아 수정(레이아웃 구조는 그대로 두고 실행 품질만 보강):
- 이모지 아이콘(🔌 👥 🛡️) → 커스텀 인라인 SVG 라인 아이콘(플러그 · CPU 칩 · 방패-체크)으로 교체 — 형제 폴더는 처음부터 SVG 라인 아이콘이었는데 이 폴더만 이모지를 썼던 게 톤 이질감의 가장 큰 원인이었음
- `.row.r1` / `.row.r2` 뒤에 옅은 색 레인(lane) 배경 추가(파랑 톤 = 실제 경로, 회색 점선 톤 = 미사용 경로) — 기존엔 opacity·점선만으로 구분해 대비가 약했음
- `.hub`(Listen Socket / Worker Pool) 아이콘 뒤에 은은한 radial-gradient 글로우 추가 — 아이콘+텍스트만 떠 있어 비어 보이던 카드 상단 공간을 채움
- `.line.solid`에 `filter:drop-shadow` 추가로 화살표에 미세한 입체감

레이아웃/캔버스 크기(1200×650)는 변경 없음 — 전부 절대배치 오버레이라 리플로우 없이 안전하게 얹었다.

고친 뒤 `bash build.sh` 또는 `build.ps1` → 상위 PNG 갱신 → 확인.

---

## 4. 디자인 시스템 (다른 폴더와 통일 — 톤 일관성용)

**폰트**: **Pretendard**(`fonts/PretendardVariable.woff2`, 가변). `@font-face{font-weight:100 900}` 로 임베드, `font-family:'Pretendard',"Malgun Gothic",...` 우선순위.
> woff2 는 `SendQ LockFree 전환 시 성능 10% 저하/_src/fonts/` 에서 그대로 복사해왔다(동일 파일).

**색 팔레트 (v2, 2026-07-01 갱신)**:

| 용도 | HEX |
|---|---|
| 캔버스 배경 | 그라디언트 `#eef1f7` → `#e5e9f1` (135deg) |
| 카드 | `#ffffff` |
| 잉크(제목) | `#0f172a` |
| 흐린 글씨 | `#5b6478`(sub) · `#8b93a5`(muted) |
| 파랑(Accept 흐름 · 실선 · 노드 액센트) | 라이트 `#6ba8dd` · 진한 `#2660a5`/`#2b6aa8`, 태그·eyebrow 배경 `#eaf2fb`, 테두리 `#d7e6f8` |
| 빨강(제목 강조 · Worker Pool 점 · takeaway 콜아웃) | `#ef4444`/`#dc2626`, 배경 `#fef2f2`~`#fdf0f0`, 테두리 `#f8d2d2` |
| 점선(고스트 행 `.line.dash`, 미사용 `.node.ghost`) | `#ccd3de` |
| 실선/구분선 (`.line`, `.foot` 상단) | `#e8ebf2` |

> 이전 버전(2패널 원형 비교, 위 3번 정정 참고)의 초록(Worker) · gapline · divider 토큰은 현재 파이프라인 구조에서 쓰지 않음 — 표에서 제거.

**카드**: 캔버스 22px 여백 → 흰 카드 `border-radius:30px`, `box-shadow:0 30px 68px -22px rgba(20,30,60,.24), 0 8px 20px -10px rgba(20,30,60,.10)`, `border:1px solid rgba(255,255,255,.7)`, 상단 6px 그라디언트 액센트 바(`::before`). (`Send Coalescing` 폴더 `common.css` v2 계열과 통일)

---

## 5. 파일 / 주의

- `01.html` — 소스 (위 표 참고)
- `fonts/PretendardVariable.woff2` — 임베드 폰트(2MB, 가변)
- `build.sh`, `build.ps1` — 재빌드 스크립트 (동일 기능, 3x)
- 주의:
  - 레포 최상위 `.gitignore` 가 `/img/` 자체를 통째로 제외한다 — PNG 뿐 아니라 이 폴더의 HTML/스크립트/README/폰트(woff2) 도 **전부 git 미추적**(디스크에만 존재, `git log`/`git ls-files` 로 대조 불가). 그래서 문서와 실제 파일이 어긋나도 git 이력으로는 감지가 안 되니, 수정할 때마다 이 README를 실제 파일 기준으로 직접 갱신해둘 것.
  - `build.sh` 는 **LF·BOM 없음** 유지(CRLF/BOM 이면 bash 깨짐). `.ps1` 메시지는 영문(한글 출력 깨짐 방지).
  - HTML 은 UTF-8(BOM 불필요, `<meta charset="utf-8">` 있음).
  - 렌더 시 **`--allow-file-access-from-files` 필수**(폰트 로드용, 위 2번).
