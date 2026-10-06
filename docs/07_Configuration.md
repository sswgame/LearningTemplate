# 설정 — 어떤 값을 어디에 두나

새 설정 값을 만들거나 기존 값을 바꾸려 할 때, 그 값을 어느 파일에 두어야 하는지 설명하는 문서입니다.
설정 파일마다의 필드 표(이름, 타입, 기본값, 범위, 설명)는 이 문서에 없고, 생성 문서 [`docs/Config/`](Config/README.md)에 있습니다.
생성 문서는 코드에서 만듭니다. 설정 구조체의 `PROPERTY`, 손으로 읽는 설정의 `ConfigKeyDoc` 표, 전역 변수, `ArgumentList.xxx`, CMake 옵션이 원본입니다.
코드와 생성 문서가 어긋나면 `CheckConfigReference` 게이트가 실패합니다.

## 1. 어디에 둘까

| 이 값은 | 둘 곳 |
|---|---|
| 엔진 전체의 시작 설정 | `Config/Engine/EngineConfig.json` |
| 엔진 데이터 테이블 | `Resource/engine/**` 의 XML |
| 팩을 마운트하기 전에 알아야 하는 게임 정보 | `Config/Game/<게임>.json` |
| 시작 씬과 게임플레이 데이터 | `Resource/game/<팩>/data/gamesettings.xml` |
| 플레이어가 옵션 메뉴에서 바꾸는 값 | 스키마 `*.settings.xml`, 값은 `usersettings.json` |
| 메모리 예산 | `Config/Engine/MemoryBudget.json` |
| 사람이 정하는 에디터 도구 값과 임포트 규칙 | `Config/Editor/*.json` |
| 에디터가 저장하는 상태 | `Saved/Editor/` |
| 개발 PC의 경로 | `Config/Environment/*.json` |
| 전용 서버 운영 값 | `Config/Server/<게임>.json` |
| 비밀번호와 키 | 환경 변수 |
| 빌드 형태 | CMake `SW_*` 옵션과 프리셋 |
| C++와 파이썬이 같이 읽는 쿠킹과 팩 계약 | `Config/Engine/` 의 `CookContract.json`, `PackFormat.json`, `PackConfig.json` |
| 모듈 켜고 끄기와 의존 관계 | `*.module.json` |
| 실행 한 번만 바꾸는 값, 디버그 스위치 | `-gv_<이름>=<값>` 과 명령줄 인자 |

각 줄을 조금 더 풀면 다음과 같습니다.

- **엔진 시작 설정**은 창 크기, 기본 백엔드, 프레임 시간, 고정 스텝, 팩 우선순위 같은 값입니다. 구조체는 `EngineConfig` 입니다.
- **엔진 데이터 테이블**은 물리 레이어와 재질, 내비 에이전트, 2D 정렬 레이어, 기본 에셋 경로, 셸 입력 맵입니다. `PhysicsSettings`, `NavMeshSettings`, `Render2DSettings`, `EngineDefaultAssets`, `InputMap` 이 읽습니다.
- **게임 프리셋**(`GameConfig`)에는 어떤 게임인지, 팩 루트, 창 제목, 게임이 덧붙이는 스키마처럼 팩을 마운트하기 전에 알아야 하는 것만 둡니다.
  나머지 게임 데이터는 팩 안의 `gamesettings.xml`(`GameSettings`)이나 키트 자신의 데이터 파일에 둡니다.
- **옵션 메뉴 값**은 스키마(`UserSettingsSchema`)가 팩에 실리고, 플레이어가 고른 값은 플레이어 PC의 `usersettings.json` 에 저장됩니다.
- **에디터 상태**는 도킹, 레이아웃, 캔버스, 테마, gv 프리셋처럼 에디터가 직접 쓰는 값입니다. git이 무시하고 사람이 고치지 않습니다.
  예전 위치(`Config/Editor/` 의 imgui.ini 등)에 상태가 남은 체크아웃은 PC마다 한 번 `py -3 Scripts/dev/MoveEditorState.py` 를 실행합니다.
- **개발 PC의 경로**는 LLVM, SDK, 파서 플래그 같은 값입니다. 로컬 파일은 git이 무시하고, 커밋하는 것은 시드 파일(`*.defaults.json`)뿐입니다.
- **전용 서버 운영 값**(`ServerConfig`)은 주소, 포트, 틱, 저장소, TLS입니다. 운영자가 고치는 파일이라 배포본도 디스크에서 읽습니다.
- **비밀**은 DB 비밀번호, 캐시 AUTH, 키 암호입니다. 설정 파일에는 그 환경 변수의 이름만 적습니다(`_secretEnvironment` 필드).
- **빌드 형태**는 Shipping 여부, 새니타이저, 링크할 백엔드, 활성 게임입니다. 활성 게임은 프리셋(`Ninja-Debug-<게임>`)으로 고릅니다. 표는 [`BuildOptions.md`](Config/BuildOptions.md)에 있습니다.
- **모듈 켜고 끄기**는 매니페스트의 `_bEnabledByDefault` 와, 게임 매니페스트의 `_listModuleOverride` 로 정합니다(`ModuleCatalog`).
- **전역 변수와 명령줄 인자**의 목록은 [`GlobalVariables.md`](Config/GlobalVariables.md)와 [`CommandLine.md`](Config/CommandLine.md)에 있습니다.

어느 줄에도 맞지 않으면 세 가지를 차례로 묻습니다. 런타임이 팩을 마운트하기 전에 알아야 하면 게임 프리셋에 둡니다. 플레이어가 바꾸는 값이면 사용자 설정에 둡니다.
배포본이 읽어야 하면 팩 데이터에, 그렇지 않으면 `Config/` 에 둡니다.

엔진 타입인 `GameConfig` 에는 키트 설정 필드를 두지 않습니다. 키트 설정은 팩 데이터입니다. 엔진이 키트를 알게 되면 계층 방향이 거꾸로 됩니다.

파일마다 언제 읽는지, 배포본에서 어떻게 되는지, 커밋하는지는 생성 문서의 [설정 참조 색인](Config/README.md)에 있습니다. 원본은 `Scripts/common/ConfigCatalog.py` 입니다.

## 2. 우선순위

값 하나를 여러 곳에서 정할 수 있으면 뒤에 오는 쪽이 이깁니다. 아래 순서는 코드에서 확인한 것입니다.

| 값 | 순서(뒤가 이김) |
|---|---|
| 창 크기 | `EngineConfig` → 플레이어 해상도 → `-W`, `-H` |
| VSync | `EngineConfig` → 플레이어 값 → `-vsync` |
| RHI 백엔드 | 쿠킹 계약 `default_rhi_backend` → `EngineConfig._window._defaultRHI` → `-dx12`, `-vk` 같은 플래그나 `-gv_rhiBackend=` |
| 첫 씬 | `startMap` → `titleScene` → `-gv_firstScene` |
| 고정 스텝 | `EngineConfig._fixedDeltaTime` 하나 |
| 사용자 설정 값 | 스키마 `default` → `GameConfig._mapUserSettingDefault` → `usersettings.json` |
| 사용자 설정이 바꾸는 전역 변수 | 코드 기본값 → 사용자 설정 값 → 명령줄 `-gv_*` |
| 그 밖의 전역 변수 | 코드 기본값 → 명령줄 → 실행 중 변경 |
| 2D 정렬 테이블 | 엔진 `render2d.xml` → 게임 팩 `data/render2d.xml` |

몇 줄은 덧붙일 것이 있습니다.

- 플레이어 해상도는 기본값이 아닐 때만 `EngineConfig` 를 이깁니다.
- Shipping 빌드에는 `SW_SHIPPING_RHI_BACKEND` 로 링크한 백엔드 하나뿐이라 백엔드를 고를 수 없습니다.
- `startMap` 과 `titleScene` 은 `gamesettings.xml` 의 값입니다. 에디터는 `-gv_editorStartupScene` 이 마지막 요청이 되어 이깁니다.
- 고정 스텝은 게임의 `fixedUpdate` 와 씬 물리가 같이 쓰고, 물리는 `PhysicsSettings::_subStepCount` 로 다시 나눕니다.
- 사용자 설정이 바꾸는 전역 변수는 옵션 메뉴에서 값을 바꾸면 그 값이 다시 이깁니다.
- 실행 중 변경은 에디터 패널, 개발 콘솔, gv 프리셋에서 하는 변경입니다.
- 게임 팩의 `render2d.xml` 은 엔진 것을 합치지 않고 통째로 대신합니다.

배포본(Shipping)은 `EngineConfig` 와 `GameConfig` 를 CMake 구성 단계에서 실행 파일에 넣은 사본으로 읽고, 디스크의 `Config/` 를 보지 않습니다. 서버 설정만 예외입니다.

## 3. 규칙

**틀린 설정은 오류입니다.** 모르는 키, 대소문자만 다른 키, 읽지 못한 값, 범위를 벗어난 값은 키 이름과 함께 오류를 내고, 시작 설정이면 엔진 시작을 멈춥니다.
기본값을 쓰는 것은 파일이 아예 없을 때뿐입니다. 오타가 조용히 기본값으로 바뀌면 원인을 찾기 어렵기 때문입니다.

**파일에는 기본값과 다른 값만 적습니다.** 언리얼의 `Default*.ini` 와 같은 방식입니다. 기본값은 생성 문서에서 볼 수 있습니다.
기본값을 파일에 다시 적어 두면, 나중에 코드의 기본값을 바꿔도 그 파일은 예전 값을 계속 씁니다. 그래서 `ConfigFileSchemaTest` 가 이런 줄을 막습니다. 앱이 통째로 쓰는 상태(`Saved/Editor`)는 예외입니다.

**키 이름은 코드에서 정합니다.** 리플렉션으로 읽는 설정은 멤버 이름 그대로(`_width`) 쓰고, 파이썬이 원본인 계약과 손으로 읽는 임포트 설정은 `snake_case` 로 씁니다.

**이름을 바꾸면 데이터를 다시 씁니다.** 예전 이름을 받아 주는 별칭은 두지 않습니다.
예외는 이미 배포된 플레이어 데이터(`usersettings.json`)이고, 이 경우 스키마의 `<Upgrade>` 로 옮깁니다.

**비밀은 저장소에 두지 않습니다.** 설정 파일에는 환경 변수의 이름만 적습니다.

새 설정 파일을 추가하는 순서는 다음과 같습니다.

1. 설정 구조체를 만들거나, 손으로 읽는 설정이면 `ConfigKeyDoc` 표를 만듭니다.
2. `Scripts/common/ConfigCatalog.py` 에 한 줄을 추가합니다.
3. 설정 파일 테스트(`ConfigFileSchemaTest`, `EditorConfigFileSchemaTest`, `ResourceDataSchemaTest` 중 맞는 것)의 목록에 한 줄을 추가합니다.
4. `py -3 Scripts/generate/GenerateConfigReference.py` 로 생성 문서를 다시 만듭니다.

## 상용 엔진과의 비교

언리얼은 `Config/Default*.ini` 와 `UDeveloperSettings` 를 씁니다. 클래스 선언이 곧 프로젝트 설정 화면이 되고, CVar는 명령줄이 프로젝트 설정을 이기며, 사용자와 에디터 상태는 `Saved/` 에 둡니다.
유니티는 `ProjectSettings/*.asset` 과 `SettingsProvider`, 그리고 `UserSettings/` 폴더를 씁니다.
이 엔진도 "원본은 구조체 선언이고, 설정 화면과 문서는 거기서 만든다"는 방식을 따릅니다. 파일 형식만 JSON과 XML입니다.
