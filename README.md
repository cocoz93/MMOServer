# MMO Game Server

[![맵당 동접 200 → 4,000 — 병목 추적의 기록](docs/bottleneck_chronicle.png)](https://cocoz93.github.io/portfolio/)

**Windows IOCP 기반 C++ MMO 게임서버** — 맵당 동접 **200 → 4,000**까지 병목을 단계별로 추적·해소한 개인 성능 R&D 프로젝트입니다.
모든 최적화는 추측이 아니라 **A/B 실측**(Prometheus·Grafana 계측)으로 검증했습니다.

## 🔗 링크
- **소개 페이지** — https://cocoz93.github.io/portfolio/
- **상세 기술 문서** — Notion 「MMO 게임서버 구현」

## 🛠 기술 스택
C++17 · Windows IOCP · WinSock · Prometheus · Grafana

## 📂 구조
| 폴더 | 설명 |
|------|------|
| `IOCP_Server/` | 서버 본체 — 네트워크·게임 로직 |
| `GameClient/` | 테스트용 클라이언트 |
| `StressTest/` | 부하 테스트 하네스 |
| `Monitoring/` | Prometheus·Grafana 계측 설정 |
| `Shared/` | 공용 코드 |
| `Run/` | 실행 스크립트 |
| `img/` | 성능 실험 인포그래픽 (소스는 각 폴더 `_src/`) |
