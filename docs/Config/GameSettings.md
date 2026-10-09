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
| 참고 | 시작 씬(`startMap` · `titleScene`)은 이 파일 하나다 — 모르는 원소는 로드 오류, 커스텀 값은 `<custom><prop key>` |

XML 자식 요소 이름은 아래 필드 이름에서 앞의 `_` 를 뗀 것입니다(`_startMap` → `<startMap>`). 모르는 요소는 로드 오류입니다.

## 필드

씬 흐름 · 기본 세이브 · 다국어 · 입력과 범용 게임플레이 튜닝 설정입니다. 리소스 경로 필드는 도메인을 포함한 전역 id 입니다(`game/<팩>/maps/start.scene.xml`). `GameInstanceBase` 가 읽은 뒤 게임 서비스로 묶고 다국어 · 입력 맵을 적용하며, 씬 필드는 `getFirstScene` · `getEntranceScene`, 세이브 경로는 경로 없는 `saveStateToFile` 이 씁니다.

원본: [`Source/GameFramework/Base/Foundation/Data/GameSettings.h`](../../Source/GameFramework/Base/Foundation/Data/GameSettings.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_startMap` | `string` | — |  |  | 시작 맵 / 레벨 경로 |
| `_titleScene` | `string` | — |  |  | 타이틀 씬 |
| `_entranceScene` | `string` | — |  |  | 타이틀 다음 씬 |
| `_defaultSavePath` | `string` | — |  |  | 기본 세이브 슬롯 경로(파일 경로 — 경로 없는 `GameInstanceBase::saveStateToFile` · `loadStateFromFile`) |
| `_localizationProject` | `string` | — |  |  | 로컬라이제이션 프로젝트(`*.locproject.json`, 리소스 경로) — 원문 표 · 문화권 번역 표가 그 옆에 있다 |
| `_defaultLanguage` | `string` | `ko_kr` |  |  | 기본 활성 언어 |
| `_fallbackLanguage` | `string` | `en_us` |  |  | 대체(Fallback) 언어 |
| `_inputMap` | `string` | — |  |  | 게임플레이 InputMap 경로(통합 맵 `InputManager::getInputMap()` 에 읽힌다) |
| `_loadingScreen` | `string` | `engine/ui/loading.ui.xml` |  |  | 씬을 비동기로 바꾸는 동안 띄우는 로딩 화면 문서(Loading 층 — `LoadingScreenController`). 비우면 로딩 화면 없음 |
| `_mapCustomProperty` | `map<string, string>` | — |  |  | 범용 커스텀 키-값 프로퍼티 저장소 |
