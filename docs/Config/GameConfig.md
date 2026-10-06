<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# GameConfig

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Config/Game/*.json` |
| 층 | 게임 프리셋 |
| 읽는 곳 | `ConfigManager::ensureConfig<GameConfig>` (`EngineLoop` Config 단계) |
| 언제 | 기동 · 에디터 핫 리로드 — `SW_ACTIVE_GAME` 이 파일 하나를 고른다 |
| 배포본 | configure 때 exe 에 구워 넣음(`ShippingHostDefaults.h`) |
| 커밋 | 한다 |
| 참고 | 게임마다 하나 — `CheckGamePresets` 가 팩 · CMake 프리셋과 대조한다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

활성 게임의 프리셋(`Config/Game/<SW_ACTIVE_GAME>.json`)입니다 — 팩을 마운트하기 전에 알아야 하는 것(팩 루트 · 스키마)만. 시작 씬은 팩의 `data/gamesettings.xml` 하나다. 빌드가 `SW_ACTIVE_GAME` 으로 프리셋 파일을 고른다(`config::kFileRuntimeGameConfig`). Shipping 은 그 파일을 생성 헤더로 구워 넣어 디스크의 Config/Game 을 요구하지 않습니다. 게임마다 하나라, 게임을 바꿔도 다른 게임의 팩 루트를 읽지 않습니다.

원본: [`Source/Engine/Config/GameConfig.h`](../../Source/Engine/Config/GameConfig.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_packRoot` | `string` | — |  |  | 활성 게임 팩의 리소스 경로(`game/<팩 폴더>`) — 팩 마운트 · 게임 도메인 경로가 이것으로 풀린다 |
| `_windowTitle` | `string` | `SWEngine` |  |  | 주 창 제목 — 게임마다 다르다(언리얼 ProjectName 자리). 창은 팩을 마운트하기 전에 생긴다 |
| `_userSettingsSchema` | `string` | — |  |  | 엔진 스키마에 덧붙이는 게임의 사용자 설정 스키마(팩 상대 경로, 예: `data/usersettings.settings.xml`)입니다. 비면 없습니다. |
| `_telemetrySchema` | `string` | — |  |  | 엔진 스키마에 덧붙이는 게임의 텔레메트리 사건 스키마(팩 상대 경로, 예: `data/mygame.telemetry.xml`)입니다. 비면 없습니다. |
| `_uiScaleSettings` | `string` | — |  |  | 엔진 기본(`engine/ui/uiscale.xml`) 대신 쓸 런타임 UI 배율 규칙(팩 상대 경로, 예: `data/uiscale.xml`)입니다. 비면 엔진 기본입니다. |
| `_uiThemes` | `string` | — |  |  | 엔진 기본(`engine/ui/uithemes.xml`) 대신 쓸 런타임 UI 테마 목록(팩 상대 경로, 예: `data/uithemes.xml`)입니다. 비면 엔진 기본입니다. |
| `_uiOptionsMenu` | `string` | — |  |  | 엔진 기본(`engine/ui/options.ui.xml`) 대신 쓸 옵션 메뉴 문서(팩 상대 경로, 예: `ui/options.ui.xml`)입니다. 비면 엔진 기본입니다. |
| `_uiPauseMenu` | `string` | — |  |  | 엔진 기본(`engine/ui/pause.ui.xml`) 대신 쓸 일시정지 메뉴 문서(팩 상대 경로)입니다. 비면 엔진 기본입니다(`_bUiPauseMenu` 가 켜졌을 때만 쓴다). |
| `_bUiPauseMenu` | `bool` | `false` |  |  | 화면이 없을 때 Esc · 패드 Start(`UI.Pause`)로 일시정지 메뉴를 연다. 게임 흐름(타이틀 · 경영 화면)이 Esc 를 따로 쓰면 끈다. |
| `_mapUserSettingDefault` | `map<string, string>` | — |  |  | 게임마다 다른 사용자 설정 기본값입니다(설정 id → 값). 엔진 스키마의 기본값을 덮어씁니다. 플레이어가 바꾸지 않은 값만 따라갑니다. 모르는 id · 받을 수 없는 값은 기동 오류로 알립니다(`UserSettingsManager::setGameDefault`). |
