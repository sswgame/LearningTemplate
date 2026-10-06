# Config (호스트 · 개발 설정)

이 폴더에는 엔진, 게임, 에디터, 서버를 실행하는 설정 파일과 개발 PC의 도구 경로가 있습니다. 게임 콘텐츠의 데이터는 여기가 아니라 `Resource/` 의 팩 안에 있습니다.
어떤 값을 어느 파일에 둘지와 우선순위, 배포본에서의 동작은 [설정 문서](../docs/07_Configuration.md)에 있습니다. 파일마다의 필드 표는 생성 문서 [`docs/Config/`](../docs/Config/README.md)에 있습니다.

| 폴더 | 내용 | 커밋 |
|---|---|---|
| `Engine/` | 엔진 시작 설정, 메모리 예산, 쿠킹과 팩 계약 | 함 |
| `Game/` | 게임마다 프리셋 하나 | 함 |
| `Editor/` | 사람이 정하는 에디터 도구 값과 임포트 규칙 | 함 |
| `Environment/` | 개발 PC의 툴체인 경로 | 시드만 |
| `Server/` | 게임마다 전용 서버 운영 설정 | 함 |

- `Game/` 의 프리셋은 CMake의 `SW_ACTIVE_GAME` 이 고릅니다.
- 에디터가 스스로 저장하는 상태는 `Config/Editor/` 가 아니라 `Saved/Editor/` 에 쓰고, git이 무시합니다.
- `Environment/` 에는 시드 파일(`*.defaults.json`)만 커밋합니다. 실제로 읽는 로컬 `*.json` 은 스크립트가 만들고 git이 무시합니다.
- `Server/` 의 설정에는 비밀번호를 적지 않고, 비밀을 담은 환경 변수의 이름만 적습니다.

이름이 비슷한 두 파일을 헷갈리지 않도록 주의합니다. `Engine/EngineConfig.json` 은 실행 중인 엔진의 설정이고, `Environment/toolchain_config.json` 은 개발 PC의 컴파일러와 SDK 경로입니다.

## 함정과 주의

**파일에는 기본값과 다른 값만 적습니다.** 언리얼의 `Default*.ini` 와 같은 방식이고, 기본값은 생성 문서에서 볼 수 있습니다.
기본값을 다시 적으면 나중에 코드의 기본값을 바꿔도 이 파일이 예전 값을 계속 씁니다. `ConfigFileSchemaTest` 가 이런 키를 막습니다(`ConfigManager::collectDefaultEchoKeys`). 앱이 통째로 쓰는 상태는 검사에서 뺍니다.

**틀린 설정 파일은 엔진 시작을 멈춥니다.** 모르는 키, 대소문자만 다른 키, `Min`/`Max` 범위를 벗어난 값은 키 이름과 함께 오류를 냅니다.
기본값을 쓰는 것은 파일이 없을 때뿐이고, 그때는 생성 JSON의 값, 그다음 C++ 기본값 순서로 씁니다.

**설정은 타입으로 찾습니다.** `ensureConfig<T>( path, generated )` 처럼 설정 구조체의 타입이 키입니다. 같은 타입을 두 파일에서 읽지 않습니다.

**Shipping은 디스크의 `Config/` 를 읽지 않습니다.** `EngineConfig.json` 과 게임 프리셋은 CMake 구성 단계에서 실행 파일에 넣습니다. 그래서 이 파일을 바꾸면 CMake 구성을 다시 해야 배포본에 반영됩니다. 서버 설정만 디스크에서 읽습니다.
