<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# PackEngineSettings

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/game/*/data/gamesettings.xml` |
| 층 | 팩 데이터 |
| 읽는 곳 | `PackEngineSettings::loadFromPack` (`PackEngineSettingsSubsystem`, 팩을 마운트한 뒤) |
| 언제 | 기동 |
| 배포본 | 게임 팩에 실림 |
| 커밋 | 한다 |
| 참고 | `gamesettings.xml` 의 `<Engine>` 요소 하나(XML 속성 키) — 창 제목 · 렌더 파이프라인 · 사용자 설정과 텔레메트리 스키마 · UI 규칙. `GameSettings` 는 이 요소를 건너뛴다 |

XML 속성(값 하나)과 자식 요소(목록, 구조체)의 이름은 아래 필드 이름 그대로입니다. 모르는 이름은 로드 오류입니다.

## 필드

팩 `gamesettings.xml` 의 `<Engine>` 요소입니다. 경로 칸은 도메인을 포함한 리소스 id(`game/<팩>/data/…`)입니다. 경로 칸은 선택이다 — 적지 않으면 `DataPath` 가 "읽지 않음" 으로 남고, 그 뜻은 칸마다 적은 대로(엔진 기본 · 없음)다. 비었는지는 `isSet` 으로 묻는다 (`get` 은 Debug 에서 어서션이 난다). 창 제목은 필수다. 모르는 속성 · 자식은 로드 오류다(`ConfigManager::readConfigXMLElement`).

원본: [`Source/Engine/Config/PackEngineSettings.h`](../../Source/Engine/Config/PackEngineSettings.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_windowTitle` | `string` | — |  |  | 주 창 제목(언리얼 ProjectName · 유니티 productName 자리). 창은 이 설정을 읽은 뒤에 만들어진다(`RHISubsystem`) |
| `_renderPipeline` | `DataPath` | — |  |  | 이 게임이 그리는 렌더 파이프라인 XML(예: `engine/pipeline/forward2dpipeline.xml`)입니다. 없으면 엔진 기본(포워드)입니다. 명령줄(`-gv_renderPipeline` · `-gv_deferred`)이 이보다 우선합니다(`FrameRendererUtil::computePipelinePath`). |
| `_userSettingsSchema` | `DataPath` | — |  |  | 엔진 스키마에 덧붙이는 게임의 사용자 설정 스키마입니다. 없으면 덧붙이지 않습니다 |
| `_telemetrySchema` | `DataPath` | — |  |  | 엔진 스키마에 덧붙이는 게임의 텔레메트리 사건 스키마입니다. 없으면 덧붙이지 않습니다 |
| `_uiScaleSettings` | `DataPath` | — |  |  | 런타임 UI 배율 규칙입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiScaleSettings`)입니다 |
| `_uiThemes` | `DataPath` | — |  |  | 런타임 UI 테마 목록입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiThemes`)입니다 |
| `_uiOptionsMenu` | `DataPath` | — |  |  | 옵션 메뉴 문서입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiOptionsMenu`)입니다 |
| `_uiPauseMenu` | `DataPath` | — |  |  | 일시정지 메뉴 문서입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiPauseMenu`)이고, `_bUIPauseMenu` 가 켜졌을 때만 씁니다 |
| `_bUIPauseMenu` | `bool` | `false` |  |  | 화면이 없을 때 Esc · 패드 Start(`UI.Pause`)로 일시정지 메뉴를 연다. 게임 흐름(타이틀 · 경영 화면)이 Esc 를 따로 쓰면 끈다. |
| `_mapUserSettingDefault` | `map<string, string>` | — |  |  | 게임마다 다른 사용자 설정 기본값입니다(설정 id → 값). 엔진 스키마의 기본값을 덮어씁니다. 플레이어가 바꾸지 않은 값만 따라갑니다. 모르는 id · 받을 수 없는 값은 기동 오류로 알립니다(`UserSettingsManager::setGameDefault`). |
