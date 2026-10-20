<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# GameConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Game/*.json` |
| 층 | 게임 프리셋 |
| 읽는 곳 | `ConfigManager::ensureConfig<GameConfig>` (`Engine` Config 단계) |
| 언제 | 기동 · 에디터 핫 리로드 — Dev 는 `-game=<이름>`(없으면 빌드의 활성 게임 `SW_ACTIVE_GAME`)이 파일 하나를 고른다(`GameSelection`) |
| 배포본 | configure 때 exe 에 구워 넣음(`ShippingGameDefaults.h`) |
| 커밋 | 한다 |
| 참고 | 게임마다 하나 — `CheckGamePresets` 가 팩 · CMake 프리셋과 대조한다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

돌리는 게임의 프리셋(`Config/Game/<게임>.json`)입니다 — 팩을 찾는 팩 루트 하나만 가집니다. 게임의 나머지 설정은 팩의 `data/gamesettings.xml` 하나가 원본이다(엔진이 읽는 칸은 `<Engine>` 요소 — `PackEngineSettings`, 시작 씬 등은 `GameSettings`). Dev 는 실행할 때 고른 게임(`-game=`, 없으면 빌드의 활성 게임)의 파일을 읽는다(`GameSelection::getConfigPath`). Shipping 은 구운 게임의 파일을 생성 헤더로 구워 넣어 디스크의 Config/Game 을 요구하지 않습니다. 팩 루트는 `DataPath` 라 데이터 읽기만 넣는다 — 파일 없이 만든 `GameConfig{}` 는 쓸 수 있는 값이 아니다(읽기 전에 쓰면 Debug 어서션).

원본: [`Source/Engine/Config/GameConfig.h`](../../Source/Engine/Config/GameConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_packRoot` | `DataPath` | — |  |  | 활성 게임 팩의 리소스 경로(`game/<팩 폴더>`) — 팩 마운트 · 게임 도메인 경로가 이것으로 풀린다 |
