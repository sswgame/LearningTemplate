# 전용 서버 배포

> 질문: 전용 서버를 컨테이너 · 쿠버네티스로 띄울 때 무엇을 어떻게 두는가?
> 독자: 서버를 운영하는 사람.
> 결론: `MakePackage.py` 의 서버 패키지를 `docker/Dockerfile` 로 이미지로 만들고, `kubernetes/server.yaml` 을 자기 환경에 맞춰 적용한다. 비밀은 이미지 · 설정 파일에 두지 않는다.

계획과 남은 일은 [서버 디버깅 계획](../docs/plans/ServerDebug.md) 의 운영 단위(O1 ~ O7)에 있다.

## 파일

| 파일 | 내용 |
|---|---|
| `docker/Dockerfile` | 리눅스 Shipping 서버 패키지를 담는 이미지. 루트가 아닌 사용자, 서버가 PID 1 |
| `kubernetes/server.yaml` | ConfigMap(운영 설정) · Deployment(`/readyz` · `/healthz` 검사, 종료 유예) · Service |
| `dashboards/server-overview.json` | Grafana 가져오기용 대시보드 예시(틱 p99 · 틱 사용률 · 연결 수 · 밀린 틱 · 메모리 · 크래시 묶음 수) |

## 운영 설정 (`ServerConfig`)

컨테이너에서 켜는 값은 `ServerConfig` 의 다음 항목이다.

- `_bConsoleInput: false` — 컨테이너에는 표준 입력이 없다.
- `_bJSONLogs: true` — 줄마다 JSON 한 줄을 표준 출력에 쓴다(`docker logs` · 로그 수집기가 그대로 읽는다). 요청 추적 id 는 `trace` 칸에 들어가 감사 줄을 요청 하나로 묶는다.
- `_opsPort` · `_opsListenAddress` — `/metrics` · `/healthz` · `/readyz`. 평문이고 인증이 없으므로 클러스터 안에서만 연다.
- `_drainWaitSeconds` · `_shutdownGraceSeconds` — 아래 "종료 · 드레인".
- `_tickLoadWarnPercent`(예 70) — 틱 사용률이 넘으면 지표 `server_tick_utilization_ratio` 와 로그 경고.
- `_slowTickDumpMs` · `_frameHistoryTicks` — 느린 틱을 만나면 최근 틱 이력을 `Saved/ServerReports/` 에 덤프한다.

## 종료 · 드레인

1. 쿠버네티스가 SIGTERM 을 보낸다. 서버의 종료 신호 처리기가 깃발을 세운다.
2. 서버는 곧바로 `/readyz` 를 503 으로 바꾼다. 검사가 실패하면 서비스가 파드를 엔드포인트에서 뺀다(새 접속이 오지 않는다).
3. 서버 서비스 연결이 0 이 되거나 `_drainWaitSeconds` 가 지날 때까지 틱을 계속 돌린다(진행 중인 매치 · 거래가 끝난다). `/healthz` 는 계속 200 이다.
4. 서비스를 멈추고(키트가 `_shutdownGraceSeconds` 안에서 저장) 정상 종료한다.

`terminationGracePeriodSeconds` 는 `_drainWaitSeconds + _shutdownGraceSeconds` 보다 크게 잡는다(예시: 60 + 15 < 90). 모자라면 SIGKILL 이 드레인을 끊는다.
콘솔 `quit` · Ctrl+C · 테스트 틱 상한은 기다리지 않고 바로 내린다.

## 비밀과 교체 절차

비밀은 설정 파일이 아니라 환경 변수와 마운트한 파일에서 읽는다.

- DB · 캐시 비밀번호: 서버 설정 항목의 `_secretEnvironment` 가 가리키는 환경 변수. `server.yaml` 은 `secretKeyRef` 로 채운다.
- TLS 인증서 · 개인키: `_tlsCertificateFile` · `_tlsPrivateKeyFile` 경로의 파일(Secret 볼륨). 개인키 암호는 `_tlsPrivateKeySecretEnvironment` 의 환경 변수.

교체는 **서버를 다시 띄우는 것**이 절차다(비밀은 기동 때 한 번 읽는다 — 재시작 없이 바꾸는 길은 관리 채널 O4 의 몫이고 아직 없다).

1. 비밀 저장소(Secret)의 값을 새 것으로 바꾼다. 인증서는 만료 전에 겹치는 기간을 둔다.
2. `kubectl rollout restart deployment/game-server` — `maxUnavailable: 0` 이라 새 파드가 준비된 뒤에 옛 파드가 위 드레인으로 내려간다.
3. 새 파드의 로그에서 기동(`Dedicated server ready`)을 확인하고 `/readyz` 가 200 인지 본다.
4. 게임 UDP 접속 표 서명 키 · 계정 티켓 키처럼 클라이언트가 이미 가진 값에 묶인 키는 옛 키 · 새 키를 같이 받는 기간이 있어야 한다. 그 이중 키 설정은 아직 없다(보류: [Candidates-network](../docs/plans/Candidates-network.md)).

## 아직 없는 것

- Agones 연결(서버 등록 · 할당 · 종료 알림). 매니페스트 · 코드 모두 없다 — 클라우드 계정과 SDK 가 필요하다.
- 빌드 판 협상(프로토콜 판이 다르면 접속 전에 "업데이트 필요"). 클라이언트 · 서버 접속 절차의 변경이라 따로 다룬다.
- 종료 때 클라이언트에 끊김 사유를 알리는 것(키트의 연결 종료 API 가 먼저 필요하다).
