# 인포그래픽 소스 (`_src`)

이 폴더는 상위 폴더(`SendThread 분리 유무와 이슈정리`)에 있는 **`01_send_in_worker.png`의 편집 가능한 소스**다.
Notion 페이지 [SendThread 분리 유무와 이슈정리](https://app.notion.com/p/34116a0b9f5980cca5f2e65b5122a6fe)의
**"Send는 WorkerThread에서 (스레드분리 X)"** 문단 하나를 그림 한 장으로 요약한다.
HTML/CSS로 만들고 **헤드리스 Chrome으로 3배(3x) 캡처**해서 PNG로 뽑는다. 폰트는 **Pretendard**(가변폰트)를 `fonts/`에 임베드한다.

---

## 1. 무엇을 그렸나

메시지의 핵심은 **"관계/구조"가 아니라 "분리 여부"** 다 — Send 전용 스레드를 둘 수도 있었지만 **두지 않고** Worker에 합쳤다는 대비.
그래서 큐·화살표·비교표로 메커니즘을 정교하게 그리는 대신, **개수·거리 위주의 극단적으로 단순한 대비**로 표현했다:

- **좌측(채택)**: Worker 4개가 서로 붙어 뭉친 클러스터. 각 원 모서리에 흰 `Send` 배지가 겹쳐 붙어 있어 "Send가 Worker에 융합됐다"는 느낌.
- **우측(기각, 흐리게 처리)**: `SendThread` 원 1개만 멀리 떨어져 점선 테두리 + 흐림(`opacity`) + `✕` 배지로 "가상의 대안, 실재하지 않음"을 표현.
- 가운데 큰 여백 + 세로 점선 + "따로 두지 않음" 라벨 하나로 물리적 분리감을 대신 전달.
- 노션 원문에 있는 4항목 비교표(동시성/지연/처리량/구조 확장성)와 동기화 이슈(SendQ race 등)는 **의도적으로 생략** — 자세한 트레이드오프는 원문 링크로 대체(풋터 한 줄).

---

## 2. 빠른 재빌드

HTML을 고친 뒤 **둘 중 아무거나** 실행하면 상위 폴더의 PNG가 갱신된다:

```bash
bash build.sh
```
```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

- 의존: **Chrome**(없으면 자동 **Edge**) — 스크립트가 알아서 찾는다.
- ⚠️ **`--allow-file-access-from-files` 플래그가 핵심.** 이게 있어야 `fonts/PretendardVariable.woff2`를 `file://`로 로드한다. 빠지면 폰트가 Malgun Gothic으로 폴백돼 톤이 깨진다.

### 수동으로 렌더
```bash
CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
"$CHROME" --headless --disable-gpu --hide-scrollbars --allow-file-access-from-files \
  --force-device-scale-factor=3 --screenshot="<이 폴더 절대경로>/01.png" --window-size=1040,560 \
  "file:///<이 폴더 절대경로>/01.html"
```
- **핵심**: `--window-size`는 HTML `body`의 width/height(논리크기)와 **반드시 일치**. `--force-device-scale-factor=3`이 3배 해상도.

---

## 3. 디자인 시스템 (다른 img/ 인포그래픽과 톤 공유)

**폰트**: Pretendard(`fonts/PretendardVariable.woff2`, 가변). `@font-face{font-weight:100 900}`로 임베드.
> woff2 출처(jsdelivr npm 경로): `https://cdn.jsdelivr.net/npm/pretendard@1.3.9/dist/web/variable/woff2/PretendardVariable.woff2`

**색 팔레트**: 캔버스 배경 `#eef1f5` · 카드 `#ffffff` · 잉크 `#1f2330` · 흐린 글씨 `#7c8597` · 빨강(채택 강조) `#d7263d` · 회색(기각/흐림) `#e2e6ec`·`#7c8597`.

**카드**: 캔버스 20px 여백 → 흰 카드 `border-radius:28px`, `box-shadow:0 14px 44px rgba(30,40,70,.07)`.

---

## 4. 주의

- 상위 `*.png`는 레포 `.gitignore`의 `*.png`로 **git 추적 안 됨**(디스크에만). HTML/스크립트/README/폰트(woff2)는 추적됨.
- `build.sh`는 **LF·BOM 없음** 유지(CRLF/BOM이면 bash 깨짐).
- HTML은 UTF-8(BOM 불필요, `<meta charset="utf-8">` 있음).
- 캔버스 크기는 내용량에 맞춰 타이트하게 잡혀 있다(1040×560) — 내용을 늘릴 땐 처음부터 여백을 넉넉히 잡지 말고, 늘어난 만큼만 넓힌다.
