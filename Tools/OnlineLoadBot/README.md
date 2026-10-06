# OnlineLoadBot — 온라인 서비스 부하 테스트 봇

## 이것은 무엇이고 왜 있나

로그인, 채팅, 매칭 같은 온라인 서비스는 접속이 수천 개일 때 지연과 오류가 어떻게 되는지 미리 알아야 합니다.
`OnlineLoadBot` 은 가짜 플레이어(봇)를 수천 개 만들어 서버에 붙이고, 시나리오대로 요청을 보내며 지연과 오류를 측정하는 개발 도구입니다.
Locust나 Gatling 같은 부하 테스트 도구와 같은 일을 하지만, HTTP 대신 이 엔진의 온라인 키트 클라이언트를 그대로 씁니다.

봇 하나는 연결 하나와 키트 클라이언트 일곱(계정, 서버 디렉터리, 채팅, 친구, 순위, 매칭, 라이브 운영)을 가집니다.
모든 봇이 전송 계층 하나와 엔드포인트 하나를 공유하므로(`OnlineServiceClient` 의 공유 엔드포인트 모드) 프로세스 하나에서 연결 수천 개를 열 수 있습니다.

개발 구성의 Game 타깃에서만 빌드되고, 배포물과 Client, Server 타깃에는 없습니다. 클라이언트 키트와 서버 키트가 모두 있어야 하기 때문입니다(루트 `CMakeLists.txt`).
필요한 키트가 하나라도 꺼져 있으면 빌드하지 않습니다.

## 따라 해 보기 — 빠른 확인 시나리오 돌리기

같은 프로세스 안에 서버를 띄우고 봇 300개로 30초 동안 돌려 봅니다. 저장소 루트에서 실행합니다.

```powershell
build/Ninja-Debug/Bin/OnlineLoadBot.exe --scenario=Tools/OnlineLoadBot/Scenarios/quick_check.json --local-server --light-hash
```

- `--local-server` 는 같은 프로세스에 서버를 조립합니다(`LoadBotLocalServer`). 메모리 저장소, 캐시, 메시지 버스 위에 서버 키트 일곱을 올리고, 봇은 루프백 TCP로 붙습니다(기본 포트 7100).
- `--light-hash` 는 가벼운 Argon2id 설정을 씁니다. 로그인 해시 비용 자체를 재려면 빼고 실행합니다.

끝나면 동작별 지연 표가 로그에 나오고, 오류율이 상한 이하면 종료 코드 0입니다.

## 작동 원리

### 명령줄

```text
OnlineLoadBot --scenario=<json> (--server=<ip:port> | --local-server[=<port>]) [--bots=N] [--report=<json>] [--max-error-percent=N] [--light-hash]
```

| 인자 | 뜻 |
|---|---|
| `--scenario` | 시나리오 파일(필수) |
| `--server`, `--local-server` | 둘 중 하나. 외부 서버 주소, 또는 같은 프로세스 서버 |
| `--bots` | 시나리오의 봇 수를 덮어씁니다 |
| `--report` | 결과를 JSON으로도 씁니다 |
| `--max-error-percent` | 오류율 상한(기본 1%) |
| `--light-hash` | 가벼운 비밀번호 해시 |

종료 코드는 0(통과), 1(오류율이 상한을 넘음), 2(인자나 시나리오 오류)입니다.

### 시나리오

시나리오는 JSON 데이터입니다. 봇 수, 늘리는 시간, 실행 시간, 씨앗, 지역, 단계 목록을 적습니다.

<!-- snippet: Tools/OnlineLoadBot/Scenarios/quick_check.json 의 앞부분 — 5b U7 에서 대조 -->
```json
{
    "_name": "quick_check",
    "_botCount": 300,
    "_rampUpSeconds": 2,
    "_durationSeconds": 30,
    "_seed": 3,
    "_region": "kr",
    "_listStep": [
        { "_action": "login", "_mode": "guest" },
        { "_action": "chat_join", "_channel": "world.quick" },
        { "_action": "repeat", "_count": 3, "_listStep": [
            { "_action": "chat_send", "_channel": "world.quick", "_text": "quick {bot} #{seq}" },
            { "_action": "wait", "_minMs": 200, "_maxMs": 500 }
        ] },
        { "_action": "matchmaking_queue", "_mode": "solo" },
        { "_action": "wait_match", "_timeoutMs": 20000 },
        { "_action": "logout" }
    ]
}
```

- 동작은 19가지이고 `LoadBotScenario.h` 의 `LoadBotAction` 에 있습니다. `repeat` 는 자식 단계 목록을 가집니다.
- 키는 `_camelCase` 이고 대소문자를 구분합니다. 모르는 동작, 키, 값은 읽기 오류입니다.
- 글 안의 `{bot}` 은 봇 번호, `{seq}` 는 그 봇이 보낸 순번으로 바뀝니다. 같은 글을 반복해서 보내면 서버의 도배 방지에 걸리기 때문입니다.

저장소의 시나리오는 `Scenarios/` 에 있습니다. `chat_and_match` 는 채팅과 매칭, `login_storm` 은 비밀번호 로그인(해시 비용), `quick_check` 는 봇 300개로 짧게 도는 빠른 확인입니다.

### 결과

- 동작별 지연 p50, p95, p99, 최대. 표본을 모두 보관하고 정렬하므로 정확한 백분위입니다.
- 오류 키별 수. 공통 오류는 `code<번호>`, 업무 결과는 `<동작>.result<번호>` 로 셉니다.
- 알림 종류별 수, 연결 열림과 실패와 닫힘, 경기 수

표는 로그에 나오고, `--report` 를 주면 JSON으로도 씁니다. 봇은 클라이언트 쪽 지표만 봅니다. 서버 쪽 지표는 서버의 운영 엔드포인트 `/metrics` 를 같이 봅니다([Server README](../../Source/Server/README.md)).

## 함정과 주의

- **Linux 봇 머신은 열린 파일 상한(`ulimit -n`)을 봇 수보다 크게 두세요.** 기본값 1024면 그보다 많은 연결이 실패합니다.
- **같은 글을 반복해서 보내는 시나리오를 쓰지 마세요.** 도배 방지에 걸려 오류로 집계됩니다. `{seq}` 로 글을 바꿉니다.

## 더 볼 곳

- 테스트: `EngineTest --test_filter=OnlineLoadBotTest.*`. 봇 200개가 같은 서버 조립에 루프백으로 붙어 가짜 시계로 끝까지 돌고, 지표가 정확히 맞으며, 같은 씨앗이면 같은 표가 나오는지 확인합니다.
- 서버 조립: `LoadBotLocalServer.h`
- 지표 집계: `LoadBotMetrics.h`
