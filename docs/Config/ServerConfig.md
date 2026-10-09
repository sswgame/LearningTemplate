<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# ServerConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Server/*.json` |
| 층 | 서버 운영 |
| 읽는 곳 | `ConfigManager::getConfig<ServerConfig>` (전용 서버 기동, `-server-config=<경로>` 로 바꾼다) |
| 언제 | 전용 서버 기동 |
| 배포본 | **디스크에서 읽는다**(운영자가 고친다) — 없으면 Shipping 서버는 기동 실패 |
| 커밋 | 한다 |
| 참고 | 비밀(DB 비밀번호 · 캐시 AUTH · 키 암호)은 파일에 쓰지 않는다 — `_secretEnvironment` 필드가 환경 변수 이름을 가리킨다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

전용 서버의 운영 설정 전체입니다(파일 하나).

원본: [`Source/Engine/Config/Server/ServerConfig.h`](../../Source/Engine/Config/Server/ServerConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_listenAddress` | `string` | `0.0.0.0` |  |  | 받는 주소(`0.0.0.0` 모두 · `127.0.0.1` 이 기계만)입니다. |
| `_gamePort` | `int32` | `7777` | 1 ~ 65535 |  | 게임(UDP) 포트입니다. |
| `_gamePortShardCount` | `int32` | `1` | 1 ~ 64 |  | 게임 UDP 샤드 수입니다 — 서버는 `_gamePort` 부터 `_gamePort + _gamePortShardCount - 1` 까지 포트마다 소켓 하나(샤드마다 NetHost)를 엽니다. 두 플랫폼 공통 받기 분산 계약(포트 샤딩): 로그인 토큰이 클라이언트에게 샤드 포트를 알려 줍니다. 리눅스에서 `_bGamePortReuse` 를 켜면 같은 포트 하나에 `SO_REUSEPORT` 소켓 N 개(커널이 4-튜플로 나눔)로 바뀝니다. 1 이면 소켓 하나. |
| `_bGamePortReuse` | `bool` | `false` |  |  | 리눅스 전용 — 샤드를 포트 여러 개 대신 `SO_REUSEPORT` 한 포트로 엽니다. Windows 는 무시하고 포트 샤딩으로 엽니다. |
| `_servicePort` | `int32` | `7780` | 0 ~ 65535 |  | 서비스(TCP — 로그인 · 채팅 · 거래) 포트입니다. 0 이면 서비스 서버를 열지 않습니다. |
| `_opsPort` | `int32` | `0` | 0 ~ 65535 |  | 운영 HTTP 엔드포인트(`/metrics`, `/healthz`, `/readyz`. 평문이고 인증 없음) 포트입니다. 0 이면 열지 않습니다. 기본은 끔 — 한 기계에 서버 여럿(시험 · 개발)이 뜨면 고정 포트가 부딪친다. 운영 설정 파일이 켠다(Prometheus 관례 9100 대). |
| `_opsListenAddress` | `string` | `127.0.0.1` |  |  | 운영 엔드포인트가 받을 주소입니다. 기본 `127.0.0.1` 은 이 기계만 받습니다. 스크레이퍼가 다른 기계면 사설 주소를 준다(공용 인터페이스에 열지 말 것). |
| `_tickRateHz` | `int32` | `30` | 1 ~ 240 |  | 서버 틱 수(초당)입니다. 한 틱이 게임 고정 스텝 하나입니다. |
| `_maxCatchUpTicks` | `int32` | `5` | 1 ~ 60 |  | 밀렸을 때 잠자지 않고 몰아 도는 틱 상한입니다. 넘으면 밀린 시간을 버리고 경고합니다. |
| `_bConsoleInput` | `bool` | `true` |  |  | 표준 입력 명령(quit · status …)을 받을지입니다. 서비스 · 컨테이너는 꺼도 된다(EOF 로는 종료하지 않는다). |
| `_shutdownGraceSeconds` | `int32` | `10` | 0 ~ 120 |  | 종료 요청 뒤 연결 정리 · 저장에 주는 시간(초)입니다(키트가 읽는다). |
| `_tlsCertificateFile` | `string` | — |  |  | 서비스 TLS 1.3 인증서(PEM, 체인 포함) 경로입니다 — 네트워크 보안 제공자의 서버 TLS 문맥에 그대로 넘깁니다. 둘 다 비면 **Dev 에서만** `Saved/Certificates/` 의 개발용 자체 서명 인증서, Shipping 은 서비스 포트를 열 때 오류입니다. 상대 경로는 이 설정 파일이 아니라 프로젝트 루트 기준(ConfigManager 와 같다)입니다. |
| `_tlsPrivateKeyFile` | `string` | — |  |  | TLS 개인키(PEM) 경로입니다. 키는 파일 권한(리눅스 0600 · Windows ACL)으로 지키고, 이 설정 파일에는 경로만 둡니다 — 키 암호가 있으면 `_tlsPrivateKeySecretEnvironment` 의 환경 변수에서 읽습니다(비밀은 파일에 쓰지 않는다). |
| `_tlsPrivateKeySecretEnvironment` | `string` | — |  |  | 개인키 암호를 담은 환경 변수 이름입니다. 비면 암호 없는 키. |
| `_listStore` | `vector<ServerStoreEntry>` | — |  |  | 영속 저장소 항목입니다. |
| `_listCache` | `vector<ServerCacheEntry>` | — |  |  | 메모리 캐시 항목입니다. |

## `ServerStoreEntry`

영속 저장소(SQL) 하나입니다 — 키트가 `_name` 으로 찾습니다(`accounts` · `trades`).

원본: [`Source/Engine/Config/Server/ServerConfig.h`](../../Source/Engine/Config/Server/ServerConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `string` | — |  |  | 키트가 찾는 저장소 이름(`accounts` · `trades`) |
| `_driver` | `string` | `memory` |  |  | 드라이버 이름(`postgres` · `sqlite` · `memory`)입니다. 해석은 서버 전용 모듈이 합니다. |
| `_connection` | `string` | — |  |  | 비밀이 빠진 접속 문자열(`host=127.0.0.1 port=5432 dbname=game user=game` · SQLite 파일 경로)입니다. |
| `_secretEnvironment` | `string` | — |  |  | 비밀번호를 담은 환경 변수 이름(`SW_DB_ACCOUNTS_PASSWORD`)입니다. 비면 비밀 없음. |

## `ServerCacheEntry`

메모리 캐시(RESP — Valkey · Garnet, 또는 프로세스 안) 하나입니다.

원본: [`Source/Engine/Config/Server/ServerConfig.h`](../../Source/Engine/Config/Server/ServerConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `string` | — |  |  | 키트가 찾는 캐시 이름(`presence` · `sessions`) |
| `_driver` | `string` | `memory` |  |  | 드라이버 이름(`resp` · `memory`)입니다. |
| `_endpoint` | `string` | — |  |  | `host:port`(RESP)입니다. `memory` 는 비웁니다. |
| `_secretEnvironment` | `string` | — |  |  | AUTH 비밀번호를 담은 환경 변수 이름입니다. 비면 AUTH 없음. |
