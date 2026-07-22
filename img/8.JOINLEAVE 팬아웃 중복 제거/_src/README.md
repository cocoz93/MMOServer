# 인포그래픽 소스 (`_src`)

상위 폴더(`8.JOINLEAVE 팬아웃 중복 제거`)에 들어가는 **설명 이미지 5장의 편집 가능한 소스**.
HTML/CSS/SVG → 헤드리스 Chrome 4배(4x) 캡처 → PNG. 폰트는 Pretendard(`fonts/`) 임베드.
톤은 `img/1. SendQ LockFree…/_src`(플랫 데이터-viz 카드)를 클론 — 팔레트·카드·그림자·푸터 동일.

## 5장 구성 (읽는 순서 = 이야기 흐름)

| 최종 PNG (상위 폴더) | HTML | 논리크기→4x | 메시지 |
|---|---|---|---|
| `01_bottleneck_54pct.png` | `01.html` | 1200×600 | 병목 = 섹터 JOIN/LEAVE가 게임루프 단일코어의 54% (도넛) |
| `02_two_directions.png` | `02.html` | 1200×700 | 경계 이동 = 아웃바운드(1→N 팬아웃)·인바운드(N→1 퍼널) 2방향 |
| `03_p1_outbound_fanout.png` | `03.html` | 1200×660 | P1 아웃바운드 — 1버퍼를 N명이 AddRef 공유. 생성 N회→1회, −28% |
| `04_p2_inbound_bundle.png` | `04.html` | 1200×640 | P2 인바운드 — N패킷→1패킷. 패킷당 고정비 제거로 −52% (P1의 2배) |
| `05_result_68pct.png` | `05.html` | 1200×660 | 누적 −68% (19.5→12.7→6.2ms) · gameloop 0.86→0.49 core · 부작용 0 |

## 재빌드

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```
- 5장 렌더 → 상위 폴더에 `NN_*.png` 산출 + `_src`에 `NN.png` 미리보기 복사.
- Chrome(없으면 Edge) 자동 탐색. ASCII 임시폴더+GUID 프로필+`Start-Process -Wait`로 크롬149 렌더 실패 회피.
- 한 장만 반복 수정할 땐 스크래치패드 `render_one.ps1 -Name 03 -W 1200 -H 660` 식으로.

## 디자인 시스템 (5장 공통)
- 폰트 Pretendard(`fonts/PretendardVariable.woff2`, 가변). `--allow-file-access-from-files` 필수(빠지면 Malgun 폴백).
- 팔레트: bg `#eef1f5` · 카드 `#fff` · 잉크 `#1f2330` · 빨강 `#d7263d`/딥 `#b3122a` · 파랑 `#3a78b8`/`#2b6aa8` · 초록(여유) `#1f9d63`.
  - **파랑=아웃바운드/P1, 빨강=인바운드/P2·개선 하이라이트, 초록=코어 여유**.
- 카드: 캔버스 24px 여백 → 흰 카드 radius 30, 그림자 2겹, 상단 제목+빨강 밑줄+회색 부제, 하단 얇은 푸터 1줄.
- HTML은 UTF-8(BOM 불필요, `<meta charset=utf-8>`). 상위 `*.png`는 레포 `.gitignore`로 추적 안 됨(디스크에만).
