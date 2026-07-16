# 인포그래픽 소스 (`_src`) — 7. SendWorker Pool 도입

상위 폴더의 **설명 이미지 5장**의 편집 가능한 소스다. HTML/CSS/SVG 로 만들고 **헤드리스 Chrome 4배(4x) 캡처**로 PNG 를 뽑는다.
폰트는 **Pretendard**(가변)를 `fonts/` 에 임베드. 톤은 `img/1. SendQ LockFree 전환 시 성능 10% 저하` 의 **플랫 카드 스타일을 클론**했다 (사용자 지정 참고 톤).

## 5장 구성 (스토리 아크)

> **2026-07-14 간략화** : 02~05 를 "히어로 도식 1개 + 한 줄 캡션"으로 덜어냄(1장의 여백·직관성에 맞춤). 상세 근거는 각 장 푸터로 이동.

| 최종 PNG (상위 폴더) | HTML | 논리크기 → 4x | 메시지 · 도식 |
|---|---|---|---|
| `01_diagnosis.png`  | `01.html` | 1000×640 → 4000×2560 | **진단** — 송신만 CPU 포화 · CPU 미터 3개 |
| `02_structure.png`  | `02.html` | 1240×652 → 4960×2608 | **구조** — 게임→K송신 팬아웃 하나 (sessionId%K) |
| `03_shift.png`      | `03.html` | 1240×582 → 4960×2328 | **병목 이동** — 바통(송신→게임) + 앵커 870만/s |
| `04_matrix.png`     | `04.html` | 1140×706 → 4560×2824 | **균형** — 2×2 히트맵 (4×2 최고 vs 4×4 파국, 앰버/블루·✅❌) |
| `05_conclusion.png` | `05.html` | 1200×552 → 4800×2208 | **결론** — 143→102 (−28%) + 운영 세트 바(네이비, 게임 스레드 1개당) |

## 재빌드

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

- HTML 을 고친 뒤 실행하면 상위 폴더 PNG 5장이 갱신된다.
- Chrome(없으면 Edge) 자동 탐색 · ASCII 임시폴더에서 렌더(공백·한글 경로 회피) · `Start-Process -Wait`.
- ⚠️ `--allow-file-access-from-files` 필수(폰트 `file://` 로드). `--force-device-scale-factor=4` 가 4배 해상도.
- `--window-size` 는 각 HTML `body` 의 width/height 와 반드시 일치(위 표의 논리크기).

## 디자인 시스템 (5장 공통)

- **폰트** Pretendard(`fonts/PretendardVariable.woff2`, 가변). 굵기 800 주로.
- **팔레트** 캔버스 `#eef1f5` · 카드 `#fff` · 잉크 `#1f2330` · 흐린글씨 `#7c8597` · **빨강(병목·악화) `#d7263d`/`#b3122a`** · **파랑(정상·개선) `#3a78b8`/`#2b6aa8`** · 청록(IO워커) `#357f84` · 앰버(전환) `#c98a1e`.
- **카드** 캔버스 24px 여백 → 흰 카드 `border-radius:30px` + 2겹 그림자. 제목 밑 빨강 밑줄(`.uline`). 하단 방법론 푸터(`.foot`, border-top).
- **주의(메모리 반영)** : ①HTML 은 UTF-8 무BOM(`<meta charset=utf-8>`). ②`build.ps1` 메시지는 영문. ③폭 가변 화살표는 SVG marker(x축 왜곡) 대신 **CSS 그라디언트 바 + 삼각형**(03 참고). ④한글은 monospace 요소에 넣지 말 것.

## 수치 출처

노션 `SendThread 분리 / IOThread 조정` 페이지(38a16a0b…) 본문 실측값. 01=진단 CPU, 03=동접 스윕 표, 04=4차 워커×송신 교차 표, 05=워커 12→4 델타.
