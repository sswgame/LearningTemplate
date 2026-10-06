# Server (전용 서버 실행 파일)

창 · GPU · 오디오 장치 · 플레이어 설정 없이 게임 모듈을 **고정 틱**으로 돌리는 실행 파일입니다(언리얼 `<Game>Server` · 유니티 Dedicated Server 빌드 자리).
`App`(플레이어 실행 파일)과 따로 둡니다 — 같이 쓰는 것은 모듈 호스트와 매니페스트 해석(`AppHost` 정적 라이브러리, 파일은 `Source/App/Module/`)뿐입니다.

빌드 타깃(`SW_TARGET_TYPE`)이 **Game**(Dev 기본 — `App` 과 같이 지어진다) 또는 **Server**(`*-Server` 프리셋 — 이것만 지어진다)일 때 지어지고,
Client(배포 `*-Shipping`)에는 없습니다.

## 기동 순서

1. `EngineLoop::initialize( …, EngineHostRole::DedicatedServer )` — 기동 표(`Engine/EngineInitStepList.xxx`)의 `Client` 줄(`UserSettings` · `RHI` ·
   `FrameRenderer` · `RenderThread` · `LiveShader` · `SceneRhi` · `Telemetry`)을 건너뛰고 로그에 `Dedicated server: startup steps not run - …` 를 남긴다.
   오디오는 장치를 열지 않는 `NullAudioSystem`, 모듈 이미지는 `Server` 대상 모듈만(에디터 · RHI 는 Game 빌드에서도 올리지 않는다).
2. 서버 설정(`ServerConfig`, 아래)을 읽는다.
3. `ModuleHost::initializeDedicatedServer` — 게임 인스턴스를 창 · 디바이스 없이(`nullptr`) 만든다.
4. 종료 신호 처리기(`Core/Process/ShutdownSignal`)와 표준 입력 명령 스레드(`ServerConsole`)를 세우고 `Dedicated server ready` 를 남긴다.
5. 고정 틱 루프 — 한 틱 = 게임 고정 스텝 하나(델타는 늘 `1 / _tickRateHz`). 다음 마감까지 잠자고(`MonotonicClock::sleepUntilNanoseconds`),
   `_maxCatchUpTicks` 넘게 밀리면 밀린 시간을 버리고 경고한다.
6. 종료 요청 → `Dedicated server shutdown requested (<까닭>)` → 게임 · 모듈 → 엔진 순으로 내리고 `Dedicated server shutdown complete` → 종료 코드 0.

헤드리스 작업은 씬 쿠킹(`--cook-scenes`)만 됩니다 — 서버 타깃의 `CookAssets` 가 `Server --cook-scenes` 로 쿠킹합니다. 셰이더 쿠킹 · 원본 임포트는 App 의 일이라 오류입니다.

**서버 패키지에는 텍스처 · 셰이더 바이너리 · 오디오가 없습니다.** 표 하나(`Config/Engine/CookContract.json` 의 `target_excluded_asset_kinds`)를
쿠커(`CookAssets.py --build-target Server` — 서버 타깃 빌드가 넘긴다)는 팩에서 빼는 데, 서버 런타임(`ResourceUtil::setHostTarget( "Server" )`)은 그 종류를
요청 단계에서 없는 것으로 치는 데("파일 없음" 경고 · 오류 없음) 같이 씁니다. 메시 · 애니메이션은 충돌 · 소켓 · 히트박스 · 루트 모션 때문에 남깁니다.
Dev 의 Server 는 낱개 파일을 읽지만 같은 규칙으로 그 종류를 읽지 않습니다 — 배포 서버와 같은 길을 개발 중에도 지납니다.

## 설정 — `Config/Server/<게임>.json`

`ServerConfig`(`Engine/Config/ServerConfig.h`) — 받는 주소 · 게임 UDP 포트(+ 샤드 수) · 서비스 TCP 포트 · 운영 HTTP 포트 · 주소(`_opsPort` · `_opsListenAddress`) ·
틱 수 · 따라잡기 상한 · 콘솔 입력 · 종료 유예 · TLS 인증서 · 개인키 경로 · 저장소(`_listStore`) · 캐시(`_listCache`) 항목. `-server-config=<경로>` 로 다른 파일을 줍니다.

- **Shipping 도 디스크에서 읽습니다**(굽지 않는다 — 운영자가 고친다). 파일이 없으면 Shipping 서버는 기동하지 않고, Dev 는 기본값 + 경고로 섭니다.
  상대 경로는 작업 폴더(`Bin`)에서 프로젝트 루트를 찾아 올라가 풉니다 — 배포 폴더에 `Config/` 가 없으면 `-server-config` 에 절대 경로를 줍니다.
- **비밀은 파일에 쓰지 않습니다** — 항목의 `_secretEnvironment` 가 가리키는 환경 변수에서 `ServerSecret::read` 로 읽습니다(값은 로그에 남기지 않는다).
- TLS 인증서 · 키 칸이 비면 Dev 만 개발용 자체 서명 인증서를 씁니다. Shipping 은 서비스 포트를 열 때 오류입니다.

## 운영 관측 — `/metrics` · `/healthz` · `/readyz`

서버는 지표 등록부(`Engine/Observability/MetricRegistry` — `server_tick_seconds` 히스토그램 · `server_dropped_ticks_total`)와 상태 확인
(`ServiceHealthRegistry` — 틱마다 박동, 종료 요청을 받으면 비우는 중)을 늘 들고 있습니다. 설정의 `_opsPort` 가 0 이 아니면 운영 HTTP 끝점
(`OpsHttpEndpoint` — GET 하나 · 답한 뒤 닫기, 평문 · 인증 없음)을 엽니다.

- `/metrics` — Prometheus 텍스트 0.0.4. `/healthz` — 틱이 10 초 안에 돌았으면 200, 아니면 503(감시자가 재시작). `/readyz` — 살아 있고 비우는 중이
  아니고 준비 필수 검사가 모두 통과하면 200, 아니면 503(부하 분산기가 새 접속을 보내지 않는다). 본문은 `live` · `ready` · `draining` · `check` 줄.
- **기본 바인드는 `127.0.0.1`**(`_opsListenAddress`) — 같은 기계의 에이전트 · 감시자만. 다른 기계의 스크레이퍼는 사설 주소를 주고 방화벽으로 막습니다.
- 켰는데 열지 못하면(포트 사용 중 · 주소 오류) 서버는 서지 않습니다. 기본은 끔(`0`) — 한 기계에 서버 여럿이 뜨는 시험 · 개발에서 포트가 부딪친다.

```
Server.exe -server-config=ops.json     # ops.json 에 "_opsPort": 9100
curl -s http://127.0.0.1:9100/healthz  # ok
```

## 콘솔 명령(표준 입력)

`quit` · `stop`(정상 종료) · `status`(가동 시간 · 틱 수 · 틱 시간 평균/최대 · 버린 틱) · `help`. Dev 는 개발 콘솔과 같은 명령(개발 명령 · `gv_*` 전역 변수)도 받습니다.
표준 입력이 닫혀도(EOF — systemd · 서비스 · `< /dev/null`) 서버는 내려가지 않습니다.

## 종료

| 신호 · 이벤트 | 까닭(`ShutdownCause`) | 비고 |
|---|---|---|
| 리눅스 `SIGTERM`(systemd · docker stop · kill) | `Terminate` | |
| `SIGINT` · Windows Ctrl+C · Ctrl+Break | `Interrupt` | 두 번째는 즉시 종료(리눅스 `_exit(130)`, Windows 기본 처리기) |
| 리눅스 `SIGHUP` | `Hangup` | `SIGPIPE` 는 무시한다 |
| Windows 콘솔 창 닫기 · 로그오프 · 시스템 종료 | `ConsoleClose` · `SystemShutdown` | 처리기가 정리 끝(`notifyShutdownComplete`)을 4.5 초까지 기다린다 |
| Windows 서비스 정지 · 사전 종료 | `ServiceStop` | `Server --service`(아래) |
| 콘솔 `quit` · `stop` | `ConsoleCommand` | |
| `-gv_serverExitAfterTicks=N` | `TickLimit` | 시험용(Shipping 에도 있다) |

## 실행 예

```
build/Ninja-Debug/Bin/Server.exe -gv_serverExitAfterTicks=300          # Dev(Game) 빌드 — 같은 폴더에 App · 에디터도 있다
build/Ninja-Shipping-Server/Bin/Server.exe -server-config=Config/Server/Empty.json
```

## Windows 서비스

`Server --service` 는 SCM 이 띄운 프로세스에서만 됩니다(`Server/Windows/WindowsServiceHost`). 작업 폴더를 실행 파일 폴더로 옮기고, SCM 정지 · 사전 종료를
`ServiceStop` 으로 잇고, 대기 힌트와 함께 `STOP_PENDING` → `STOPPED` 를 보고합니다. 서비스에는 콘솔이 없어 로그는 파일로 봅니다. 수동 확인(관리자 PowerShell):

```
sc create SwServer binPath= "C:\sw\Bin\Server.exe --service -server-config=C:\sw\server.json" start= demand
sc start SwServer      # 로그에 Dedicated server ready
sc stop SwServer       # 로그에 shutdown requested (ServiceStop) · shutdown complete, sc query 가 STOPPED
sc delete SwServer
```

## systemd 예시

```ini
[Unit]
Description=SW dedicated server
After=network-online.target postgresql.service valkey.service

[Service]
Type=simple
WorkingDirectory=/opt/sw-server/Bin
ExecStart=/opt/sw-server/Bin/Server -server-config=/etc/sw-server/server.json
EnvironmentFile=/etc/sw-server/secrets.env
KillSignal=SIGTERM
TimeoutStopSec=30
Restart=on-failure

[Install]
WantedBy=multi-user.target
```
