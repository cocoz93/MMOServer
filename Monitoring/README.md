# Monitoring — 지표 수집 / 비교 도구

서버가 노출하는 Prometheus 지표를 A/B 실험 단위로 수집·비교한다.
"한 시점 −N분 구간"을 Prometheus에 질의해 rate·p99·평균을 **한 번에** 받는다.
(기존처럼 두 번 찍고 손계산할 필요 없음)

## 파일 구성

| 파일 | 역할 | 실행 |
| --- | --- | --- |
| `metrics-collect.ps1` | Prometheus에 최근 N분 질의 → 지표별 값 1개씩 CSV/표 | 평소 명령 (변종마다 1번) |
| `metrics-compare.ps1` | 두 RunLabel(A/B)을 Δ% + 판정 표로 비교 | 선택 (비교할 때) |
| `queries.json` | 측정 지표 목록 + 집계식 (설정 데이터, 코드 아님) | 실행 안 함 (편집 대상) |
| `collect_metrics.ps1` | (legacy) 서버 :9090 직접 전체 덤프. Prometheus 없을 때 fallback | 선택 |
| `metrics_out\` | 결과 출력 폴더 (`window_metrics.csv` 등) | — |

## 사전 조건

- Prometheus(`:9091`)가 떠 있어야 함 — `Run\3. MMO_stress.bat`가 기동한다.
- **부하를 WindowMin(기본 5분) 이상 돌린 뒤** 수집할 것. 안 그러면 데이터가 부족해 `empty result` 경고가 뜬다.

## 사용법

### 1) 수집 — 변종마다 1번

```powershell
.\metrics-collect.ps1 -RunLabel A_baseline
.\metrics-collect.ps1 -RunLabel B_coalescing
```

옵션:
- `-WindowMin 5` : 집계 구간(분). 기본 5(≈60샘플). 빠른 탐색 3, 꼬리 추적 10.
- `-PromUrl http://localhost:9091` : Prometheus 주소(기본값).
- `-At "2026-06-10T00:07:00Z"` : 과거 시점 재집계(기본 = 현재).

### 2) 비교 — A/B Δ% 표

```powershell
.\metrics-compare.ps1 -Baseline A_baseline -Variant B_coalescing
```

## 출력

- 누적 CSV : `metrics_out\window_metrics.csv` (수집할 때마다 한 행씩 append)
- 콘솔 : 수집/비교 결과 표

## 판정 기호 (compare)

- **KPI** (lower_is_better 기준) : `good` 개선 / `BAD` 악화 / `~` 변화 미미(<1%)
- **control** (통제변수, 안 변해야 정상) : `=` 안정(<3%) / `DRIFT!` 이탈 → 실험 오염 의심
  - 예: `session_count`, `broadcast_targets_per_call` 이 DRIFT면 A/B 부하가 달랐다는 뜻

## 지표 추가/수정 (queries.json)

한 줄 = 지표 1개. `$W`는 수집 시 윈도우로 치환된다.

```json
{ "name": "지표명", "unit": "ms", "role": "kpi", "lower_is_better": true,
  "promql": "histogram_quantile(0.99, sum by (le) (rate(metric_bucket[$W]))) * 1000" }
```

- `role` : `kpi`(기본) 또는 `control`
- 히스토그램 분위수는 **`sum by (le)`** 필수 (le 라벨이 빠지면 NaN)