# 인포그래픽 소스 (`_src`) — 고밀도 브로드캐스트 병목 → 섹터 묶음

상위 폴더(`6. 고밀도 브로드캐스트 병목`) **설명 이미지 5장의 편집 가능한 소스**.
HTML/CSS/SVG → 헤드리스 Chrome **4x 캡처**로 PNG. 폰트는 **Pretendard**(가변) `fonts/` 임베드.
톤은 `img/1. SendQ LockFree ...` 플랫 데이터-viz 스타일을 클론(카드·팔레트·풋터 동일).

## 스토리 (노션 소스: `tick-p99-83-...`)

| 이미지 | HTML | 논리크기 → 4x | 핵심 |
|---|---|---|---|
| `00_overview.png` | `00.html` | 1240×640 → 4960×2560 | **페이지 전체 요약** = 진단(팬아웃370)→수정(섹터묶음)→결론(p99 57→10ms·채택) 3단계 플로우 + 지표칩 |
| `01_bottleneck_71.png` | `01.html` | 900×600 → 3600×2400 | 병목 = 틱의 71%가 수신자별 복사 (도넛) · p99 57ms>예산 40ms |
| `02_fanout_370.png` | `02.html` | 1020×620 → 4080×2480 | 왜 큰가 = 브로드캐스트 1회 = 370명 복사 (팬아웃) |
| `03_sector_bundle.png` | `03.html` | 1200×620 → 4800×2480 | 해결 = 이동마다 즉시전송 → 틱 끝 섹터 묶음 (before/after) |
| `04_ab_results.png` | `04.html` | 1160×660 → 4640×2640 | 결과 = A/B 동접2000, 4지표 급감 (OFF/ON 쌍막대) |
| `05_count_not_cost.png` | `05.html` | 1120×600 → 4480×2400 | 결론 = 병목은 '단가'가 아니라 '횟수' (2레인 대비) |

수치는 노션 A/B 실측(동접 2000): broadcast_calls 19,602→3,310/s, 복사 15.7→3.0ms,
tick p99 57.4→9.95ms, gameloop CPU 0.50→0.16 core, 팬아웃 370·타겟당 ≈75ns.

## 재빌드

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

- 상위 폴더 PNG 5장 갱신. 의존: **Chrome**(없으면 Edge) — 자동 탐색.
- ⚠️ `--allow-file-access-from-files` + `--headless=new` + 매 실행 새 프로필(GUID)이 핵심
  (폰트 `file://` 로드 · 실행 중 Chrome에 붙어 스크린샷 no-op 되는 것 방지).
- 폴더명이 공백+한글이라 ASCII 임시폴더(`%TEMP%\ig-render6`)에서 렌더 후 복사.

## 디자인 시스템 (5장 공통)

- **폰트**: Pretendard(`fonts/PretendardVariable.woff2`, 가변), 굵기 700/800.
- **팔레트**: 배경 `#eef1f5` · 카드 `#fff` · 잉크 `#1f2330` · 흐림 `#7c8597` · 빨강 `#d7263d` · 딥레드 `#b3122a`.
  이 덱에서 **빨강 = 섹터 묶음(변경 후)·강조**, **회색 = 변경 전**.
- **카드**: 24px 여백 → 흰 카드 `border-radius:30px` + 2겹 그림자. 타이틀 800 + 빨강 밑줄, 하단 풋터(경계선).

## 주의

- 상위 `*.png` 는 `.gitignore`(`*.png`)로 git 추적 안 됨(디스크만). HTML/스크립트/README/폰트(woff2)만 추적.
- HTML 은 UTF-8(BOM 불필요, `<meta charset="utf-8">`).
- 한글을 monospace(코드 토큰)에 넣지 말 것(폴백 폰트로 톤 깨짐) — 03의 `USE_SECTOR_AGGREGATION` 등은 영문만.
