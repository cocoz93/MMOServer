# 모니터링 셋업 가이드 (새 PC)

이 저장소는 **모니터링 설정 파일만** 트래킹합니다.
Grafana / Prometheus / windows_exporter **바이너리(약 1.5GB)는 트래킹하지 않으므로** 직접 내려받아 배치해야 합니다.

- 트래킹되는 것: `Monitoring/config/` 안의 대시보드 JSON, 프로비저닝 YAML, 본 스크립트
- 트래킹 안 되는 것: `Monitoring/grafana/`, `Monitoring/prometheus-*/`, `windows_exporter.exe`, 각 `data/` 런타임 디렉토리

---

## 1. 서버/클라 exe 빌드

`IOCP_Server` 와 `StressTest` 솔루션을 빌드합니다. (Release 권장)
빌드된 exe 에 `/metrics` 엔드포인트가 포함되어 Prometheus 가 자동 수집합니다.

| 대상 | metrics 포트 |
|---|---|
| mmo_server (IOCP_Server) | 9090 |
| stress_client (StressTest) | 9101 |
| windows_exporter | 9182 |

## 2. 모니터링 바이너리 배치

아래 버전을 받아 `Monitoring/` 하위에 압축 해제합니다. (디렉토리명 그대로 유지)

| 도구 | 버전 | 배치 경로 |
|---|---|---|
| Grafana | 11.6.0 (windows-amd64) | `Monitoring/grafana/` |
| Prometheus | 3.4.1 (windows-amd64) | `Monitoring/prometheus-3.4.1.windows-amd64/` |
| windows_exporter | (임의 최신) | `Monitoring/windows_exporter.exe` |

> Prometheus 폴더명에 버전이 들어가지만, `setup.ps1` 이 `prometheus-*` 패턴으로 자동 탐색하므로
> 다른 버전을 써도 폴더명만 `prometheus-...` 형식이면 동작합니다.

## 3. 설정 주입 (자동화 스크립트)

PowerShell 에서 실행합니다.

```powershell
# 단일 PC (서버·부하 같은 머신)
Monitoring\config\setup.ps1

# 부하 클라가 별도 PC 인 경우 그 IP 지정
Monitoring\config\setup.ps1 -StressClientIp 203.0.113.10
```

스크립트가 하는 일:
- **대시보드 절대경로** → 현재 PC 의 `Monitoring/grafana/conf/provisioning/dashboards` 로 자동 치환
- **stress_client target IP** → `-StressClientIp` 값으로 치환 (기본 `localhost`)
- 템플릿을 live 바이너리 트리(`grafana/conf/provisioning/`, `prometheus-*/prometheus.yml`)로 복사

> 머신 종속 값은 이 두 가지(절대경로·IP)뿐입니다. datasource URL(`localhost:9091`),
> mmo_server/windows target 은 모든 PC 공통이라 템플릿에 고정돼 있습니다.

## 4. 기동

```powershell
# Prometheus (포트 9091 로 listen)
Monitoring\prometheus-3.4.1.windows-amd64\prometheus.exe `
  --config.file=Monitoring\prometheus-3.4.1.windows-amd64\prometheus.yml `
  --web.listen-address=:9091

# windows_exporter (포트 9182)
Monitoring\windows_exporter.exe

# Grafana
Monitoring\grafana\bin\grafana-server.exe --homepath Monitoring\grafana
```

Grafana 접속 후 대시보드가 자동 프로비저닝됐는지 확인합니다 (기본 `http://localhost:3000`).

---

## 설정을 수정했을 때

대시보드/프로비저닝을 바꿨다면 **`Monitoring/config/` 쪽(source of truth)을 수정**하고 다시 커밋하세요.
Grafana UI 에서 편집한 내용은 live 트리에만 남고 트래킹되지 않습니다.
(필요하면 live 의 JSON 을 `Monitoring/config/.../dashboards/` 로 복사해 반영)
