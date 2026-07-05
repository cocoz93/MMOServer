# 인포그래픽 소스 (`_src`) — 1500 동접 붕괴

상위 폴더(`5. 1500 동접 붕괴`)에 들어가는 **설명 이미지 5장의 편집 가능한 소스**다.
톤·폰트·팔레트는 **`1. SendQ LockFree 전환 시 성능 10% 저하`** 덱을 그대로 계승(플랫 데이터-viz).
HTML/CSS/SVG → 헤드리스 Chrome 4배(4x) 캡처 → PNG.

---

## 1. 구성 — 본문 5장 + 콜아웃 머메이드 대체 1장(통합)

| 최종 PNG (상위 폴더) | HTML | 논리크기 → 출력(4x) | 메시지 |
|---|---|---|---|
| `01_paradox_833.png`      | `01.html` | 1140×640 → 4560×2560 | 자원은 여유(3녹색 게이지)인데 1500이 833에서 붕괴(1빨강 넘침) |
| `02_mechanism_sendq.png`  | `02.html` | 1200×600 → 4800×2400 | 빠르게 쌓임(fanout↑) vs 1개씩만 빠짐(_sending) → SendQ 넘침 |
| `03_verdict_wan474.png`   | `03.html` | 1200×690 → 4800×2760 | 범인=외부 회선. before(474벽 경유) / after(loopback) |
| `04_crosscheck_3axis.png` | `04.html` | 1180×620 → 4720×2480 | 서버·클라 결백 3축 → 수렴 → 회선 473.94 Mbps |
| `05_core_isolation.png`   | `05.html` | 1140×620 → 4560×2480 | 한 PC·코어 격리 (서버 0–5 / 클라 6–9 CPU 다이맵) |
| `06_before_after.png`     | `06.html` | 1300×720 → 5200×2880 | **콜아웃 머메이드 통합**(덱 양식·가로 2행) — 위=변경 전(🖥️서버→474벽→💻클라→overflow 26/s), 아래=변경 후(한 PC 컨테이너 서버⇄클라 loopback→overflow 0) |

스토리 아크(본문 01~05): **현상·역설 → 메커니즘 → 판정 → 3축 검증 → 격리 측정판.**
06 은 페이지 **상단 콜아웃의 [변경 전 / 변경 후] 머메이드 2장을 한 장으로 통합**. **가로 2행**(위 변경전=문제/빨강 존, 아래 변경후=해결/초록 존), 리치 디바이스 노드(아이콘+상단 액센트바), 큰 결과 숫자(26/s→0), '한 PC' 점선 컨테이너로 "두 PC 분리 vs 한 PC 안" 대비. 덱 슬라이드 양식(제목+빨간 밑줄+서브+표준 풋터) 준수. 머메이드 노드·색(server 파랑/client 초록/wall 빨강/ok teal)에 충실. (06/07 개별 → 통합 → 세로2단 → **가로2행 리치본**으로 재작업.)

---

## 2. 빠른 재빌드

HTML 을 고친 뒤:
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```
- 01~05 렌더 → 상위 폴더 PNG 덮어쓰기. 의존: **Chrome**(없으면 자동 **Edge**).
- ⚠️ `--allow-file-access-from-files` 플래그가 핵심(폰트 `file://` 로드). 빠지면 Malgun Gothic 폴백으로 톤 깨짐. (build.ps1 에 이미 포함.)
- 슬라이드 크기를 바꾸면 build.ps1 의 `Render "NN" w h` 인자도 HTML `body` width/height 와 **반드시 일치**시킬 것.

---

## 3. 디자인 시스템 (SendQ 덱과 공통)

**폰트**: Pretendard(`fonts/PretendardVariable.woff2`, 가변), 굵기 700/800 주로.
**팔레트**:

| 용도 | HEX |
|---|---|
| 캔버스 / 카드 | `#eef1f5` / `#ffffff` |
| 잉크(제목) / 흐린 글씨 | `#1f2330` / `#7c8597` |
| 빨강(강조·경고) / 딥레드 | `#d7263d` / `#b3122a` |
| 파랑(서버·중립) | `#3a78b8` · ink `#2b6aa8` |
| 초록(여유·정상·클라) | `#16a34a` · deep `#0f7a37` |
| 앰버(경로 포화) | `#e08a1e` |

**카드**: 캔버스 24px 여백 → 흰 카드 `border-radius:30px`, 2겹 `box-shadow`, `border:1px solid #eef0f3`.
**타이틀**: 36–37px/800, `.r`=빨강 강조 + 빨강 밑줄(`.uline` 80px). **풋터**: 하단 `border-top` + 회색 캡션.

---

## 4. 주의 (하드 배운 것)

- 상위 `*.png` 는 레포 `.gitignore` 의 `*.png` 로 **git 추적 안 됨**(디스크에만). HTML/build/README/**폰트(woff2)** 는 추적됨.
- **화살표**는 폭가변 SVG marker 대신 인라인 SVG `<path>` 삼각형(고정크기)으로 — marker 는 x축 왜곡.
- **monospace 요소에 한글 금지**(시스템폰트 폴백). 설정칩(`ServerCores=0-5` 등)은 ASCII 만.
- HTML 은 UTF-8(BOM 불필요, `<meta charset="utf-8">` 있음). `build.ps1` 메시지는 영문.
- 세로 여유 부족 시 요소가 풋터와 겹침 → 슬라이드 `height` 를 키우면(상단 정렬이라) 하단 클리어런스가 늘어남. (03 이 690 인 이유.)
