<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# 설정 참조

설정을 **어디에 두는가**(층, 우선순위, 배포본, 핫 리로드)는 손으로 쓴 [`docs/07_Configuration.md`](../07_Configuration.md)에 있습니다.
이 폴더는 코드에서 만듭니다. 필드를 고치려면 코드를 고치고 `py -3 Scripts/generate/GenerateConfigReference.py` 로 다시 만듭니다(문서가 낡으면 `CheckConfigReference` 게이트가 실패합니다).

| 다른 목록 | 원본 |
|---|---|
| [전역 변수 `-gv_*`](GlobalVariables.md) | `SW_GLOBAL_VARIABLE`, `SW_TEST_GLOBAL_VARIABLE(_SHIPPED)` 정의 |
| [명령줄 인자](CommandLine.md) | `Source/Core/Predefined/ArgumentList.xxx`, `Config/Engine/CookContract.json` |
| [CMake 빌드 옵션](BuildOptions.md) | `option()`, `set( … CACHE … )`, `CMakePresets.json` |
| [사용자 설정](UserSettings.md) | `Resource/**/*.settings.xml` |

## 엔진 기본값

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Config/Engine/EngineConfig.json` | `ConfigManager::ensureConfig<EngineConfig>` (`EngineLoop` Config 단계) — 기동 · 에디터 핫 리로드(창 크기 · 백엔드처럼 기동 때만 쓰는 값은 다음 실행부터) | configure 때 exe 에 구워 넣음(`ShippingHostDefaults.h`) — 디스크의 파일은 읽지 않는다 | 한다 | [EngineConfig](EngineConfig.md) |
| `Config/Engine/MemoryBudget.json` | `MemoryBudgetMonitor::loadBudgetFile` (`EngineLoop` Config 단계) — 기동 | 읽지 않음(배포본에는 메모리 프로파일러가 없다) | 한다 | [MemoryBudget](MemoryBudget.md) |
| `Resource/engine/data/enginedefaultassets.xml` | `EngineDefaultAssets::loadFromResource` (`EngineLoop` 엔진 셸 단계) — 기동 | 엔진 팩에 실림 | 한다 | [EngineDefaultAssets](EngineDefaultAssets.md) |
| `Resource/engine/physics/physicssettings.xml` | `PhysicsSettings::loadFromResource` (`PhysicsSystem`) — 기동(씬 물리를 처음 세울 때) | 엔진 팩에 실림 | 한다 | [PhysicsSettings](PhysicsSettings.md) |
| `Resource/engine/navigation/navmeshsettings.xml` | `NavMeshSettings` (`ResourceCatalog`) — 처음 쓸 때 · 내비메시 쿠킹 | 엔진 팩에 실림 | 한다 | [NavMeshSettings](NavMeshSettings.md) |
| `Resource/engine/data/render2d.xml` | `Render2DSettings::getActive` (손으로 읽음 — 모르는 요소 · 속성은 오류) — 처음 쓸 때 · `reloadActive`. 활성 게임 팩의 `data/render2d.xml` 이 있으면 통째로 대신한다 | 엔진 팩에 실림 | 한다 | [형식 설명](../../Source/Engine/Graphics/2D/Render2DSettings.h) |
| `Resource/engine/input/default.input.xml` | `InputMap::loadFromResource` (셸 입력 맵 — `EngineDefaultAssets::_shellInputMap`) — 기동 · 에셋 핫 리로드 | 엔진 팩에 실림 | 한다 | [형식 설명](../../Source/Engine/Input/README.md) |
| `Resource/engine/input/ui.input.xml` | `InputMap::loadFromResource` (런타임 UI 행동 맵 — `EngineDefaultAssets::_uiInputMap`, `UiSystem::initialize`) — 기동(Ui 단계) | 엔진 팩에 실림 | 한다 | [형식 설명](../../Source/Engine/UI/README.md) |
| `Resource/engine/settings/engine.settings.xml` | `UserSettingsManager::loadSchema` (`EngineLoop` UserSettings 단계) — 기동. 플레이어 옵션 메뉴의 스키마 — 설정 표는 생성 페이지 `UserSettings.md` | 엔진 팩에 실림 | 한다 | [UserSettings](UserSettings.md) |

## 게임 프리셋

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Config/Game/*.json` | `ConfigManager::ensureConfig<GameConfig>` (`EngineLoop` Config 단계) — 기동 · 에디터 핫 리로드 — `SW_ACTIVE_GAME` 이 파일 하나를 고른다. 게임마다 하나 — `CheckGamePresets` 가 팩 · CMake 프리셋과 대조한다 | configure 때 exe 에 구워 넣음(`ShippingHostDefaults.h`) | 한다 | [GameConfig](GameConfig.md) |

## 팩 데이터

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Resource/game/*/data/gamesettings.xml` | `GameSettings::loadFromResource` (`GameInstanceBase::initialize`) — 게임 인스턴스 초기화 · 게임 모듈 핫 리로드. 시작 씬(`startMap` · `titleScene`)은 이 파일 하나다 — 모르는 원소는 로드 오류, 커스텀 값은 `<custom><prop key>` | 게임 팩에 실림 | 한다 | [GameSettings](GameSettings.md) |
| `Resource/game/*/data/render2d.xml` | `Render2DSettings::getActive` — 처음 쓸 때 · `reloadActive`. 엔진 기본 `render2d.xml` 을 통째로 대신한다 | 게임 팩에 실림 | 한다 | [형식 설명](../../Source/Engine/Graphics/2D/Render2DSettings.h) |
| `Resource/game/*/data/*.settings.xml` | `UserSettingsManager::loadSchema` (게임 프리셋 `_userSettingsSchema`) — 기동. 엔진 스키마에 덧붙는 게임 설정 | 게임 팩에 실림 | 한다 | [UserSettings](UserSettings.md) |
| `Resource/game/*/data/*.input.xml` | `InputMap::loadFromResource` (`GameSettings::_inputMap`) — 게임 인스턴스 초기화 | 게임 팩에 실림 | 한다 | [형식 설명](../../Source/Engine/Input/README.md) |

## 사용자 설정

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `%LOCALAPPDATA%/SWEngine/<팩>/usersettings.json` | `UserSettingsManager` (사용자 파일) — 기동 · 메뉴에서 적용할 때 씀. Linux 는 `$XDG_CONFIG_HOME/swengine/<팩>/` · 자동화는 `-gv_userSettingsFile=<경로>` | 플레이어 PC 에 생긴다 — 배포된 플레이어 데이터(이름을 바꾸면 스키마 `<Upgrade>`) | 안 한다 | [UserSettings](UserSettings.md) |

## 에디터 도구

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Config/Editor/editortooldefaults.json` | `EditorToolDefaults::loadFromHostPath` (에디터 모듈 기동) — 에디터 기동 · 에디터 핫 리로드. 기본값과 다른 값이 있을 때만 만든다 — 없으면 기본값 | 없음(에디터 없음) | 한다 | [EditorToolDefaults](EditorToolDefaults.md) |
| `Saved/Editor/EditorConfig.json` | `EditorConfig::loadFromHost` · `saveToHost` — 에디터 기동 · 테마 저장 때 **앱이 통째로 다시 쓴다**. 앱이 쓰는 상태(`Saved/`, git 무시) — 사람이 고치지 않는다 | 없음(에디터 없음) | 안 한다 | [EditorConfig](EditorConfig.md) |
| `Config/Editor/TextureImportConfig.json` | `TextureImportConfig::loadFromFile` (손으로 읽음) — 텍스처 임포트(에디터 · `App --import-textures`) | 없음(임포트는 Dev 만) | 한다 | [TextureImportConfig](TextureImportConfig.md) |
| `Config/Editor/ModelImportConfig.json` | `ModelImportConfig::loadFromFile` (손으로 읽음) — 모델 임포트(에디터 · `App --import-models`) | 없음(임포트는 Dev 만) | 한다 | [ModelImportConfig](ModelImportConfig.md) |
| `Config/Editor/AssetValidationRules.json` | `Scripts/common/AssetValidation.py` (에디터 `EditorAssetValidation` 도 이것을 돌린다) — 저장 · 임포트 직후(에디터) · `CheckAssetRules` 게이트 · `py -3 -m Scripts validate-assets` | 없음 | 한다 | [형식 설명](../../Scripts/common/AssetValidation.py) |
| `Saved/Editor/*` | ImGui · `EditorDockLayout` · `EditorLayoutStore` · 노드 캔버스 · 도구 문서 임시본 · gv 프리셋(`GlobalVariablePresets/<팩>/`) — 에디터가 쓰고 읽는다. 앱이 쓰는 에디터 상태 — `EditorUtil::getEditorStateDirectory`(git 무시) | 없음 | 안 한다 | [형식 설명](../../Source/Editor/README.md) |

## 서버 운영

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Config/Server/*.json` | `ConfigManager::getConfig<ServerConfig>` (전용 서버 기동, `-server-config=<경로>` 로 바꾼다) — 전용 서버 기동. 비밀(DB 비밀번호 · 캐시 AUTH · 키 암호)은 파일에 쓰지 않는다 — `_secretEnvironment` 필드가 환경 변수 이름을 가리킨다 | **디스크에서 읽는다**(운영자가 고친다) — 없으면 Shipping 서버는 기동 실패 | 한다 | [ServerConfig](ServerConfig.md) |
| `Config/Server/chat_banned_words.txt` | `ChatWordFilter::loadFile` (`GF_Server_Chat`) — 채팅 서비스 기동 | 디스크에서 읽는다(운영자가 고친다) — 저장소에는 시험 낱말만 | 한다 | [형식 설명](../../Source/GameFramework/Kits/Feature/Online/Server/Chat/ChatWordFilter.h) |

## 빌드 · 쿠킹 계약

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Config/Engine/CookContract.json` | `GenerateCookContract.py`(→ `CookContract.gen.h`) · `Scripts/common/CookContract.py` — configure · 쿠킹 · `CheckCookContract` 게이트 | 빌드에 굳음 | 한다 | [형식 설명](../../Scripts/common/CookContract.py) |
| `Config/Engine/PackFormat.json` | `GeneratePackFormat.py`(→ `PackFormat.gen.h`) · `Scripts/common/PackFormat.py` — configure · 쿠킹 | 빌드에 굳음 | 한다 | [형식 설명](../../Scripts/common/PackFormat.py) |
| `Config/Engine/PackConfig.json` | `Scripts/generate/CookAssets.py` — 쿠킹(팩 만들기) | 팩 모양을 정한다(파일은 안 실림) | 한다 | [형식 설명](../../Scripts/generate/README.md) |

## 모듈 매니페스트

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Source/*.module.json` | CMake `ModuleManifest.cmake` · `ModuleCatalog::loadDirectory` — configure · 기동(모듈 적재 순서). `ModuleCatalogTest.EveryRepositoryManifestParses` 가 모두 읽는다 | 빌드에 굳음(링크할 모듈 · 순서) | 한다 | [형식 설명](../../Source/Engine/Module/ModuleCatalog.h) |

## 개발 머신 로컬

| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |
|---|---|---|---|---|
| `Config/Environment/*.defaults.json` | `Scripts/setup/SetupEnvironment.py` (로컬 `*.json` 의 시드) — 환경 준비 · configure | 없음 | 한다 | [형식 설명](../../Scripts/setup/README.md) |
| `Config/Environment/toolchain_config.json` | `GenerateToolchainCMake.py` · ReflectionParser — configure · 리플렉션 코드젠. SetupEnvironment 가 채운 절대 경로 캐시 | 없음 | 안 한다 | [형식 설명](../../Scripts/setup/README.md) |
| `Config/Environment/parser_config.json` | ReflectionParser `ParserConfig::load` — 리플렉션 코드젠 | 없음 | 안 한다 | [형식 설명](../../Tools/ReflectionParser/README.md) |
| `Config/Environment/search_paths.json` | `Scripts/setup/HostTools.py` — 환경 준비 | 없음 | 안 한다 | [형식 설명](../../Scripts/setup/README.md) |

## 타입 읽는 법

`string` = 글, `vector<T>` = 배열(XML 은 자식 원소 목록), `map<K, V>` = 오브젝트, `float2/3/4` = `"x,y,z"` 글, enum = 값 이름 글, `hashed_string` = 이름 글. 기본값이 `—` 이면 0, false, 빈 글, 빈 목록입니다.
