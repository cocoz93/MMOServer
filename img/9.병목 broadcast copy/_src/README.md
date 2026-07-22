# 병목 broadcast copy → 수신섹터 배칭 인포그래픽 소스

노션 글 "병목 broadcast copy(tick 65%) → 수신섹터 배칭으로 −88%·tick avg −62%" 삽입용 4장.
톤: img/1 (플랫 데이터-viz · Pretendard · 흰 카드 · 빨강 앵커).

| 파일 | 내용 | 논리 크기 (출력 4x) |
|---|---|---|
| 01.html → `01_ceiling_sweep.png` | 동접 스윕 — 게임루프 코어 포화, 천장 4500–5000 | 940x680 |
| 02.html → `02_bottleneck_65pct.png` | tick의 65% = broadcast copy (실체는 27만 회/tick 호출 고정비) | 920x580 |
| 03.html → `03_receiver_batching.png` | 패킷 기준 → 수신자 기준 뒤집기, enqueue 27만→5천 | 1200x620 |
| 04.html → `04_ab_result.png` | A/B 결과 — copy −88% · gameloop 0.57→0.21 · tick 반토막 | 1060x680 |

재빌드:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

숫자 출처는 노션 글의 스윕 표(동접 5000 열)와 A/B 표(동접 4000 · 3rep 중앙값).
