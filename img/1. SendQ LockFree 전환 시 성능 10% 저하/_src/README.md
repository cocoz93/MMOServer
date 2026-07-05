# 인포그래픽 소스 (`_src`)

이 폴더는 상위 폴더(`SendQ LockFree 전환 시 성능 10% 저하`)에 있는 **설명 이미지 5장의 편집 가능한 소스**다.
이미지는 HTML/CSS/SVG 로 만들고 **헤드리스 Chrome 으로 4배(4x) 캡처**해서 PNG 로 뽑는다.
폰트는 **Pretendard**(가변폰트)를 `fonts/` 에 임베드해서 쓴다 — PPT 에 바로 쓸 프레젠테이션 품질.
기억(메모리) 없는 세션이 와도 이 문서 + HTML 만 보면 바로 이어서 작업할 수 있다.

---

## 1. 무엇에 소스가 있나

| 최종 이미지 (상위 폴더) | HTML 소스 | 논리크기 → 출력(4x) |
|---|---|---|
| `01_enqueue_62ns.png` | `01.html` | 780×560 → 3120×2240 |
| `02_ab_normalized.png` | `02.html` | 940×680 → 3760×2720 |
| `03_before_after.png` | `03.html` | 1200×600 → 4800×2400 |
| `04_spsc.png` | `04.html` | 1160×560 → 4640×2240 |
| `05_dilution.png` | `05.html` | 1140×560 → 4560×2240 |

> **5장 모두 HTML 소스가 있다.** (01·02·05 는 원본 PNG 를 보고 재구성했다.) 톤·폰트·팔레트가 5장 전부 동일하다.

---

## 2. 빠른 재빌드

HTML 을 고친 뒤 **둘 중 아무거나** 실행하면 상위 폴더의 PNG 5장이 갱신된다:

```bash
bash build.sh
```
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

- 둘 다 같은 일(01~05 렌더 → 상위 폴더 PNG 덮어쓰기).
- 의존: **Chrome**(없으면 자동 **Edge**) — 스크립트가 알아서 찾는다.
- ⚠️ **`--allow-file-access-from-files` 플래그가 핵심.** 이게 있어야 `fonts/PretendardVariable.woff2` 를 `file://` 로 로드한다. 빠지면 폰트가 Malgun Gothic 으로 폴백돼 톤이 깨진다. (build 스크립트엔 이미 들어있음.)

### 수동으로 한 장만 렌더
```bash
CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
"$CHROME" --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files \
  --user-data-dir="$TEMP/ig-prof" --force-device-scale-factor=4 --screenshot="<이 폴더 절대경로>/01.png" --window-size=780,560 \
  "file:///<이 폴더 절대경로>/01.html"
```
- **핵심**: `--window-size` 는 HTML `body` 의 width/height(논리크기)와 **반드시 일치**. `--force-device-scale-factor=4` 이 4배 해상도.
- `file://` 는 절대경로 + 슬래시(`C:/...`). 폴더명 공백·한글 OK.

---

## 3. 편집 흐름 / 각 파일 구조

- **01** 도넛(원인 분해): `.donut` 은 CSS `conic-gradient`(빨강 68% + 회색 32%) + `mask` 로 링. 우측 `.legend` 는 이름/값/진행바 2줄.
- **02** 막대(4지표 악화): `.plot` grid 4열, 각 `.bar` 의 `height:%` 가 막대 높이(game_logic=100% 최대·진한색). `.axis` 파란 기준선 + "기준 100".
- **03** 도넛(한 칸 담기는 양): 하단 `<script>` 의 `buildRing(id,cells,theme)` 가 SVG arc. 좌 6칸/우 20칸.
- **04** 파이프라인(SPSC): `.pipe` 노드 3개 + 화살표, 하단 인과 3칩 체인.
- **05** 스택바(희석): 수평 `.track`(회색+빨강). 세로 점선/`.bracket`(+62 ns)은 track 기준 **절대좌표(px)**. 좌표: 빨강시작=624(트랙내), 849지점=671, 911지점=720; track offset=138 → chart 좌표는 각각 +138(=762/809/858).

고친 뒤 `bash build.sh` → 상위 PNG 갱신 → 확인.

---

## 4. 디자인 시스템 (5장 공통 — 톤 일관성용)

**폰트**: **Pretendard**(`fonts/PretendardVariable.woff2`, 가변). `@font-face{font-weight:100 900}` 로 임베드, `font-family:'Pretendard',"Malgun Gothic",...` 우선순위. 굵기 700/800 주로.
> woff2 출처(jsdelivr **npm** 경로 — `gh` 경로는 403):
> `https://cdn.jsdelivr.net/npm/pretendard@1.3.9/dist/web/variable/woff2/PretendardVariable.woff2`

**색 팔레트**:

| 용도 | HEX |
|---|---|
| 캔버스 배경 | `#eef1f5` |
| 카드 | `#ffffff` |
| 잉크(제목) | `#1f2330` |
| 흐린 글씨 | `#7c8597` |
| **빨강**(밑줄·강조·빨강막대) | `#d7263d` |
| 딥레드(그라데 끝·최대막대) | `#b3122a` |
| 파랑(03 도넛/박스) | `#3a78b8` |
| 기준선 파랑(02) | `#2f5fd0` |
| 회색 채움 | `#e2e6ec` · 바 `#ccd4de` |

**카드**: 캔버스 24px 여백 → 흰 카드 `border-radius:30px`, `box-shadow:0 14px 44px rgba(30,40,70,.07)`, `border:1px solid #eef0f3`.

---

## 5. 현재 상태

### 2026-07-05 — 4x + 질감 다듬기 (디테일 상향, 톤 유지)

- **해상도 3x→4x** (DSF 4): 01=3120×2240 · 02=3760×2720 · 03=4800×2400 · 04=4640×2240 · 05=4560×2240. PPT/인쇄·확대에서 더 선명(화면상 3x와 체감차는 작음).
- **질감 미세 다듬기 — 레이아웃·색·수치·문구 0 변경**: ①숫자 `font-variant-numeric:tabular-nums`(자릿수 폭 정렬) ②`text-rendering:optimizeLegibility` ③카드 `box-shadow` 1겹→2겹(앰비언트+키, 가장자리 깊이).
- **빌드 견고화 (중요)**: 실행 중 Chrome 이 있으면 구형 `--headless`+기본 프로필은 그 인스턴스에 붙어 스크린샷이 **no-op** 됨. 그래서 `--headless=new` + **매 실행 새 `--user-data-dir`**(격리 프로필)이 필수. `build.ps1` 은 ASCII 임시폴더에서 렌더 + `Start-Process -Wait`(공백·한글 경로의 인자분할/미대기 회피), `build.sh` 는 `mktemp` 프로필(bash 는 포그라운드 대기).

### 2026-07-01 — PPT 품질 업그레이드

- **PPT 품질 업그레이드 완료**: 5장 전부 ①Pretendard 폰트 ②2x→3x 고해상도 ③여백·그림자·정렬 미세 다듬기. **수치·문구 등 내용은 원본과 동일하게 유지.**
- 01·02·05 는 원본 PNG(맑은고딕·2x)를 보고 HTML 로 재구성. 03·04 는 기존 소스에 폰트 교체 + 다듬기.
- 02: "기준 100" 라벨을 파란 기준선 **아래**로 옮김(첫 막대와 겹침 해소).
- 05: `+62 ns` 브래킷을 **증가분 구간(849→911)** 에만 걸도록 좌표 수정(큐 전체 121ns 가 아니라 증가분 62ns 를 가리키게).

---

## 6. 파일 / 주의

- `01~05.html` — 소스 (위 표 참고)
- `fonts/PretendardVariable.woff2` — 임베드 폰트(2MB, 가변)
- `build.sh`, `build.ps1` — 재빌드 스크립트 (동일 기능, 3x)
- `_original_backup/*.ORIGINAL.png` — **03·04 편집 이전** 원본 백업
- 주의:
  - 상위 `*.png` 는 레포 `.gitignore` 의 `*.png` 로 **git 추적 안 됨**(디스크에만). HTML/스크립트/README/**폰트(woff2)** 는 추적됨.
  - `build.sh` 는 **LF·BOM 없음** 유지(CRLF/BOM 이면 bash 깨짐). `.ps1` 메시지는 영문(한글 출력 깨짐 방지).
  - HTML 은 UTF-8(BOM 불필요, `<meta charset="utf-8">` 있음).
  - 렌더 시 **`--allow-file-access-from-files` 필수**(폰트 로드용, 위 2번).
