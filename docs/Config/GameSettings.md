<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# GameSettings

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Resource/game/*/data/gamesettings.xml` |
| 층 | 팩 데이터 |
| 읽는 곳 | `GameSettings::loadFromResource` (`GameInstanceBase::initialize`) |
| 언제 | 게임 인스턴스 초기화 · 게임 모듈 핫 리로드 |
| 배포본 | 게임 팩에 실림 |
| 커밋 | 한다 |
| 참고 | 시작 씬(`startMap` · `titleScene`)은 이 파일 하나다 — 모르는 원소는 로드 오류, 커스텀 값은 `<custom><prop key>`. `<Engine>` 요소는 아래 `PackEngineSettings` 가 읽는다 |

XML 자식 요소 이름은 아래 필드 이름에서 앞의 `_` 를 뗀 것입니다(`_startMap` → `<startMap>`). 모르는 요소는 로드 오류입니다.

## 필드

씬 흐름, 기본 세이브, 다국어, 입력과 범용 게임플레이 튜닝 설정입니다. 경로 필드는 `DataPath` 이다 — 값은 팩의 gamesettings 만 넣고 코드는 문자열로 채울 수 없다. 리소스 경로는 도메인을 포함한 전역 id 이다 (`game/<팩>/maps/start.scene.xml`). 시작 맵만 필수이고, 나머지 경로 칸은 적지 않으면 "읽지 않음"(`isSet` false)이며 그 뜻은 칸마다 적었다. XML 자식 요소 이름은 필드 이름에서 앞의 `_` 를 뗀 것이고(`_startMap` → `<startMap>`), 읽기는 리플렉션 칸 표를 따른다(`loadRoot`). `GameInstanceBase` 가 읽은 뒤 게임 서비스로 등록하고 다국어와 입력 맵을 적용한다. 씬 필드는 `getFirstScene` 과 `getEntranceScene` 이 쓰고, 세이브 경로는 경로 없이 부른 `saveStateToFile` 이 쓴다.

원본: [`Source/GameFramework/Base/Foundation/Data/GameSettings.h`](../../Source/GameFramework/Base/Foundation/Data/GameSettings.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_startMap` | `DataPath` | — |  |  | 시작 맵 / 레벨 경로. 타이틀 씬 · 타이틀 다음 씬이 없을 때 처음 여는 씬이다 |
| `_titleScene` | `DataPath` | — |  |  | 타이틀 씬. 없으면 처음 씬은 시작 맵이고 "타이틀로" 명령을 걸지 않는다 |
| `_entranceScene` | `DataPath` | — |  |  | 타이틀 다음 씬. 없으면 시작 맵이다 |
| `_defaultSavePath` | `DataPath` | — |  |  | 기본 세이브 슬롯 경로(파일 경로 — 경로 없는 `GameInstanceBase::saveStateToFile` · `loadStateFromFile`). 없으면 경로 없는 저장은 오류다 |
| `_localizationProject` | `DataPath` | — |  |  | 로컬라이제이션 프로젝트(`*.locproject.json`, 리소스 경로) — 원문 표 · 문화권 번역 표가 그 옆에 있다. 없으면 게임 문자열 표 없음 |
| `_defaultLanguage` | `string` | `ko_kr` |  |  | 기본 활성 언어 |
| `_fallbackLanguage` | `string` | `en_us` |  |  | 대체(Fallback) 언어 |
| `_inputMap` | `DataPath` | — |  |  | 게임플레이 InputMap 경로(통합 맵 `InputManager::getInputMap()` 에 읽힌다). 없으면 입력 맵을 바꾸지 않는다 |
| `_loadingScreen` | `DataPath` | — |  |  | 씬을 비동기로 바꾸는 동안 띄우는 로딩 화면 문서(Loading 층 — `LoadingScreenController`). 정본은 팩의 gamesettings 이고 적지 않으면 로딩 화면 없음 |
| `_audioEvents` | `DataPath` | — |  |  | 게임의 사운드 이벤트 라이브러리(`*.audioevents.xml`). `GameInstanceBase` 가 `onInitialize` 앞에 올리고, 적었는데 못 읽으면 게임 초기화가 실패한다. 적지 않으면 이벤트 없음 |
