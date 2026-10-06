# 설정 — 어떤 값을 어디에 두나

이 장 하나만 손으로 쓴다. **칸 표(이름 · 타입 · 기본값 · 범위 · 설명)는 [`docs/Config/`](Config/README.md) 의 생성 문서**가 정본이다 — 코드(설정 구조체의
`PROPERTY` · 손으로 읽는 설정의 `ConfigKeyDoc` 표 · 전역 변수 · `ArgumentList.xxx` · CMake 옵션)에서 만들고, 낡으면 `CheckConfigReference` 게이트가 진다.

## 1. 결정표

| 이 값은 | 둘 곳 | 형식 · 정본 | 배포본 | 핫 리로드 |
|---|---|---|---|---|
| 엔진 전체의 기동 값(창 크기 · 기본 백엔드 · 프레임 시간 · 고정 스텝 · 팩 우선순위) | `Config/Engine/EngineConfig.json` | `EngineConfig` | exe 에 구움 | 에디터가 다시 읽음(기동 값은 다음 실행) |
| 엔진 데이터 표(물리 레이어 · 재질 · 서브스텝, 내비 에이전트, 2D 정렬 레이어, 기본 에셋 경로, 셸 입력) | `Resource/engine/**` XML | `PhysicsSettings` · `NavMeshSettings` · `Render2DSettings` · `EngineDefaultAssets` · `InputMap` | 엔진 팩 | 에셋 핫 리로드(표마다) |
| 어떤 게임인가 · 팩 루트 · 창 제목 · 게임이 덧붙이는 스키마 | `Config/Game/<게임>.json` — **팩을 마운트하기 전에 알아야 하는 것만** | `GameConfig` | exe 에 구움 | 에디터가 다시 읽음 |
| 시작 씬 · 게임플레이 데이터 · 입력 맵 · 언어 · 키트 설정 | `Resource/game/<팩>/data/gamesettings.xml`(또는 키트 자기 데이터 파일) | `GameSettings` | 게임 팩 | 게임 모듈 리로드 |
| 플레이어가 옵션 메뉴에서 바꾸는 것 | 스키마 `*.settings.xml` + 플레이어 파일 `usersettings.json` | `UserSettingsSchema` | 스키마는 팩, 값은 플레이어 PC | 메뉴 적용 |
| 메모리 예산 | `Config/Engine/MemoryBudget.json` | `ConfigKeyDoc` 표 | 안 읽음 | 아니오 |
| 사람이 정하는 에디터 도구 값 · 임포트 규칙 | `Config/Editor/*.json`(커밋) | `EditorToolDefaults` · 임포트 키 표 | 없음 | 일부 |
| 에디터가 저장하는 상태(도킹 · 레이아웃 · 캔버스 · 테마 · gv 프리셋) | `Saved/Editor/` — 앱이 쓰고 git 이 무시한다. 사람이 고치지 않는다. 옛 자리(`Config/Editor/` 의 imgui.ini 등)에 상태가 남은 체크아웃은 PC 마다 한 번 `py -3 Scripts/dev/MoveEditorState.py` | — | 없음 | — |
| 개발 머신 경로(LLVM · SDK · 파서 플래그) | `Config/Environment/*.json`(로컬, git 무시 — 시드는 `*.defaults.json`) | 파이썬 | 없음 | — |
| 전용 서버 운영(주소 · 포트 · 틱 · 저장소 · TLS) | `Config/Server/<게임>.json` | `ServerConfig` | **디스크에서 읽음**(운영자가 고친다) | 아니오 |
| 비밀(DB 비밀번호 · 캐시 AUTH · 키 암호) | **환경 변수** — 설정 파일에는 그 이름만(`_secretEnvironment` 칸) | — | 운영 환경 | — |
| 빌드 모양(Shipping · 새니타이저 · 링크할 백엔드 · 활성 게임) | CMake `SW_*` 옵션 — 게임은 프리셋(`Ninja-Debug-<게임>`) | [`BuildOptions.md`](Config/BuildOptions.md) | 빌드에 굳음 | configure |
| C++ 와 파이썬이 같이 읽는 쿠킹 · 팩 계약 | `Config/Engine/{CookContract, PackFormat, PackConfig}.json` | 파이썬 모듈이 정본 | 빌드에 굳음 | configure |
| 모듈 켜고 끄기 · 의존 | `*.module.json`(`_bEnabledByDefault`, 게임 매니페스트의 `_listModuleOverride`) | `ModuleCatalog` | 빌드에 굳음 | configure |
| 실행 한 번만 바꾸기 · 디버그 스위치 · 자동화 | `-gv_<이름>=<값>` · 명령줄 인자 | [`GlobalVariables.md`](Config/GlobalVariables.md) · [`CommandLine.md`](Config/CommandLine.md) | 일반 · `SHIPPED` 만 | 실행 중(에디터 · 콘솔) |

어디에도 맞지 않으면: 런타임이 팩 마운트 전에 알아야 하나(→ 프리셋) · 플레이어가 바꾸나(→ 사용자 설정) · 배포본이 읽나(→ 팩 데이터, 아니면 `Config/`) 순서로 묻는다.
엔진 타입(`GameConfig`)에 키트 칸을 두지 않는다 — 키트 설정은 팩 데이터다(층이 거꾸로 된다).

## 2. 우선순위 (뒤가 이긴다 — 코드에서 확인한 것)

| 값 | 순서 |
|---|---|
| 창 크기 | `EngineConfig` → 플레이어 해상도(기본값이 아닐 때) → `-W` · `-H` |
| VSync | `EngineConfig` → 플레이어 값 → `-vsync` |
| RHI 백엔드 | 쿠킹 표 `default_rhi_backend` → `EngineConfig._window._defaultRHI` → `-dx12` · `-vk` · … 또는 `-gv_rhiBackend=`(Shipping 은 `SW_SHIPPING_RHI_BACKEND` 로 링크한 하나뿐) |
| 첫 씬 | `startMap` → `titleScene`(gamesettings) → `-gv_firstScene` (에디터는 `-gv_editorStartupScene` 이 마지막 요청) |
| 고정 스텝 | `EngineConfig._fixedDeltaTime` 하나 — 게임 `fixedUpdate` 와 씬 물리가 같이 쓰고, 물리는 `PhysicsSettings::_subStepCount` 로 나눈다 |
| 사용자 설정 값 | 스키마 `default` → `GameConfig._mapUserSettingDefault` → `usersettings.json` |
| 사용자 설정이 넣는 전역 변수 | 코드 기본값 → 사용자 설정 값 → 명령줄 `-gv_*`(메뉴에서 바꾸면 그 값) |
| 그 밖 전역 변수 | 코드 기본값 → 명령줄 → 실행 중(에디터 패널 · 콘솔 · gv 프리셋) |
| 2D 정렬 표 | 엔진 `render2d.xml` → 게임 팩 `data/render2d.xml`(통째로 대신) |

배포본(Shipping)은 `EngineConfig` · `GameConfig` 를 configure 때 구운 사본으로 읽고 디스크의 `Config/` 를 보지 않는다(서버 설정만 예외).

## 3. 규칙

- **틀린 설정은 오류다.** 모르는 키 · 대소문자만 다른 키 · 읽지 못한 값 · 범위 밖 값은 키 이름과 함께 오류이고, 기동 설정이면 기동이 멈춘다. 파일이 **없을** 때만 기본값이다.
- **파일에는 기본값과 다른 값만 적는다**(언리얼 `Default*.ini` 와 같다) — 기본값은 생성 문서가 보여 준다. 기본값을 다시 적은 줄은 코드의 기본값을 바꿔도
  따라가지 않는 옛 값이 되므로 `ConfigFileSchemaTest` 가 막는다. 앱이 통째로 쓰는 상태(`Saved/Editor`)는 예외다.
- 키 이름: 리플렉션 설정은 멤버 이름 그대로(`_width`), 파이썬이 정본인 계약 · 손으로 읽는 임포트 설정은 `snake_case`.
- 이름을 바꾸면 데이터를 다시 쓴다(별칭 없음) — 예외는 배포된 플레이어 데이터(`usersettings.json` — 스키마 `<Upgrade>`).
- 새 설정 파일 = 설정 구조체(또는 `ConfigKeyDoc` 표) + `Scripts/common/ConfigCatalog.py` 한 줄 + 설정 파일 시험(`ConfigFileSchemaTest` · `EditorConfigFileSchemaTest` ·
  `ResourceDataSchemaTest`) 표 한 줄 + `py -3 Scripts/generate/GenerateConfigReference.py`.
- 비밀은 저장소에 없다. 설정 파일은 환경 변수 **이름**만 든다.

상용 엔진: 언리얼 `Config/Default*.ini` + `UDeveloperSettings`(클래스 선언이 프로젝트 설정 화면), CVar 우선순위(명령줄이 프로젝트 설정을 이긴다), `Saved/`(사용자 · 에디터 상태) ·
유니티 `ProjectSettings/*.asset` + `SettingsProvider`, `UserSettings/`. 이 저장소는 "정본은 구조체 선언, 화면 · 문서는 거기서 만든다" 를 같이 쓰고, 파일 형식만 JSON/XML 이다.
