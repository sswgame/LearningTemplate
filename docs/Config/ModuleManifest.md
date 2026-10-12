<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->

# ModuleManifest

[설정 색인](README.md) · [어디에 두나](../07_Configuration.md)

| | |
|---|---|
| 파일 | `Source/*.module.json` |
| 층 | 모듈 매니페스트 |
| 읽는 곳 | CMake `ModuleManifest.cmake` · `ModuleCatalog::loadDirectory` |
| 언제 | configure · 기동(모듈 적재 순서) |
| 배포본 | 빌드에 굳음(링크할 모듈 · 순서) |
| 커밋 | 한다 |
| 참고 | `ModuleCatalogTest.EveryRepositoryManifestParses` 가 모두 읽는다 |

JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다.

## 필드

`<모듈>.module.json` 이 담는 내용입니다. `ModuleCatalog::parseManifest` 가 리플렉션으로 읽어(모르는 키 · 읽지 못한 값은 오류) `ModuleManifest` 로 옮깁니다. 읽은 값의 의미 검사(필수 · 버전 형식 · 파일 이름 · 에디터 확장 규칙)는 `parseManifest` 가 한다. 키 이름은 CMake `ModuleManifest.cmake` 와 같다.

원본: [`Source/Engine/Module/ModuleCatalog.h`](../../Source/Engine/Module/ModuleCatalog.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `string` | — |  |  | Module name (= CMake target = file name before .module.json) |
| `_version` | `string` | — |  |  | Version as major.minor.patch |
| `_kind` | `ModuleKind` | `kModuleKindUnset` |  |  | Module kind: GameFramework, Kit, Game, Editor, RHI or EditorExtension (required) |
| `_description` | `string` | — |  |  | Human readable description |
| `_listDependency` | `vector<ModuleDependencyFile>` | — |  |  | Modules that load first |
| `_listPlatform` | `vector<ModulePlatform>` | — |  |  | Platforms the module is built for: Windows, Linux (at least one) |
| `_listConfiguration` | `vector<ModuleConfiguration>` | — |  |  | Build configurations the module is built for: Dev, Shipping (at least one) |
| `_listTarget` | `vector<ModuleTarget>` | — |  |  | Executables that load the module: Client, Server (at least one) |
| `_bEnabledByDefault` | `bool` | `true` |  |  | Whether the module is on unless the project turns it off |
| `_listModuleOverride` | `vector<ModuleOverride>` | — |  |  | Only the Game module (the project) lists these: modules it turns on or off |

## `ModuleDependencyFile`

매니페스트 파일의 의존 항목입니다. 버전은 글(`"1.2.3"`)로 적고 `ModuleVersion::parse` 가 읽는다.

원본: [`Source/Engine/Module/ModuleCatalog.h`](../../Source/Engine/Module/ModuleCatalog.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `string` | — |  |  | Name of the module that has to load first |
| `_minVersion` | `string` | — |  |  | Lowest accepted version as major.minor.patch (empty accepts any) |

## `ModuleOverride`

프로젝트가 모듈 하나를 켜거나 끕니다(`.uproject` 의 Plugins 항목에 해당). 매니페스트 `_listModuleOverride` 의 원소와 같은 모양이다.

원본: [`Source/Engine/Module/ModuleCatalog.h`](../../Source/Engine/Module/ModuleCatalog.h)

| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |
|---|---|---|---|---|---|
| `_name` | `string` | — |  |  | Name of the module to turn on or off |
| `_bEnabled` | `bool` | `true` |  |  | true turns the module on, false turns it off |

## 열거 `ModuleKind`

| 값 | 설명 |
|---|---|
| `GameFramework` | 장르 공통 게임플레이 프레임워크(공유 모듈이며 리로드하지 않는다) |
| `Kit` | 장르 키트(`GF_*`) |
| `Game` | 게임 모듈(`SWGame`)이며 프로젝트다 |
| `Editor` | 에디터 모듈(Dev 전용) |
| `RHI` | RHI 백엔드(`RHI_*`, Dev 에서만 모듈) |
| `EditorExtension` | 에디터 확장(`GF_EditorPlugIn_<이름>`, `SWGameEditor`)이다. Dev 전용이며 `-EnableEditor` 일 때만 로드한다 |

## 열거 `ModulePlatform`

| 값 | 설명 |
|---|---|
| `Windows` |  |
| `Linux` |  |

## 열거 `ModuleConfiguration`

| 값 | 설명 |
|---|---|
| `Dev` |  |
| `Shipping` |  |

## 열거 `ModuleTarget`

| 값 | 설명 |
|---|---|
| `Client` | 플레이어 실행 파일(App) |
| `Server` | 전용 서버 실행 파일(Server) |
