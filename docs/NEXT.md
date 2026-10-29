# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 다음 세션은 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 구조 개편 A3 · B · C 와 전수 감사, 테스트 틀 · 시나리오 개편(옛 문제 목록을 형식으로 막음)까지 끝났다. Debug · Release 오류 0, `App --test` 18 케이스 · 시나리오 19 × 네 백엔드 통과. 다음은 옛 테스트 · 게임 시나리오를 새 틀로 더 옮기는 것과 4 절의 남은 일이다.

## 1. 지금 상태 (2026-10-29)

- **빌드**: `Ninja-Debug`(활성 게임 Empty) · `Ninja-Release`(전체) 오류 0. **테스트**: `ctest`(단위 `App --test` + 시나리오 엔진 3 · Empty 16) 통과 — 형식과 쓰는 법은 `Test/README.md`.
- **콘텐츠**: 임포트 · 쿠킹 산출물은 저장소에 없다(.gitignore) — 새 체크아웃은 Release App 으로 `--import-textures` · `--import-models` · `--import-heightfields` · `--cook-shaders` 를 차례로 돈다(Debug 의 텍스처 압축은 장당 20 분이라 쓰지 않는다). 엔진 기본 텍스처 둘(`missing` · `loading`)만 원본으로 둔다.
- **엔진 층은 빌드 형식이다(B)**: 층 표 `Scripts/generate/rules/EngineLayers.toml` → configure 가 파일마다 층을 정하고(`Scripts/generate/EngineLayers.py`), Engine 의 .cpp 는 층마다 OBJECT 부분 `EngineLayer<N>`(오브젝트는 Engine DLL 안)이 짓는다. 부분 N 의 include 자리 맨 앞에 위 층 헤더마다 `#error` 껍데기가 있어 위 층 include(전이 포함)가 컴파일에서 멈춘다. 린트 게이트 `engine-layers` 와 기준선은 지웠다. 표에 없는 파일이 생기면 configure 가 멈춘다.
- **Core 티어 표** `Scripts/generate/rules/CoreLayers.toml`, **에디터 층 표** `Scripts/generate/rules/EditorLayers.toml` — 폴더를 옮기면 세 표를 먼저 본다(`move-source` 는 표를 모른다).
- **도구**: `move-source`(이동 + 참조 · include 순서 · clang-format — CMake 소스 목록 · 문서 · 표는 고치지 않는다, 옮긴 뒤 `git grep`), `rename-symbol`(`--regex` 로 이름 일부. 새 이름이 파일 이름과 같으면 충돌로 오인한다 — 그때는 그 파일만 `sed`), 구조 감사 `PYTHONIOENCODING=utf-8 py -3 Scripts/dev/StructureAudit.py`.
- **조심할 것**: Windows git 은 대소문자만 다른 이름 바꾸기를 못 본다. 생성물도 같다 — 이름의 대소문자만 바뀐 생성 파일은 옛 철자로 남으므로(`EngineAbiStamp.gen.h`) 지우고 다시 짓는다. `py -3 -m Scripts format` 은 셰이더까지 훑는다 — 커밋에 상관없는 파일이 섞이지 않았는지 `git show --stat` 로 본다.

## 2. 정한 규칙

| 규칙 | 근거 |
|---|---|
| Asset = 콘텐츠(`Engine/Asset`, 데이터 폴더 `Content/`), Resource = GPU · RHI 자원만 | 언리얼 Content · AssetManager · FRenderResource |
| Pack(`.pack`) · Watch(자동화 감시 값, Probe 는 라이트 · 리플렉션 프로브만) · Scene(씬 컨테이너, World 는 월드 공간 뜻만) | 같은 단어 한 뜻 |
| Module 은 동적으로 올리는 코드 단위만. 렌더러 확장점은 `RenderExtension`, 모듈 로더는 `IModuleLoader` · `ModuleLoadStage` · `ModuleUnloadStage`. 모듈 문맥의 Image → Library | 언리얼 SceneViewExtension |
| Host = 엔진을 돌리는 프로세스(App · Server · 테스트 하네스)와 그 쪽(`HostService` — 엔진 쪽 짝은 `EngineServices`). 네트워크 끝점 `NetEndpoint*`, 에디터 패널 진입점 `IEditorPanelService`, 온라인 키트 서버 서비스 `IOnlineServerService`(짝 `IOnlineClientService`) | 같은 단어 한 뜻 |
| Runtime = 모든 호스트에서 도는 것(키트 `Runtime/`) · 실행 중. 모듈 C-ABI 계약은 `Source/ModuleAPI` | 언리얼 모듈 형식 Runtime · ServerOnly |
| Automation = 시나리오 자동 테스트(`Engine/Automation`). UI 의 시나리오 단계는 `UI/Scenario/UIScenarioSteps` — "UI Automation" 은 Windows UIA 만 | 언리얼 Automation System |
| 정적 함수만 있는 타입은 대상을 인자로 받는 상태 없는 라이브러리면 도메인 이름 그대로. 고칠 것은 서브시스템 상태 위의 정적 파사드뿐(`EditorPlaySession` 이 서브시스템) | 언리얼 FJsonSerializer · FPaths |
| 낮은 층 · 높은 층이 서로 알아야 하면 위가 아래에 건다: 팩 매니저(`AssetIO::bindPackManager`), 메시 무효화(`SW_MESH_INVALIDATION_HOOK`), 서브시스템 순서(위 층의 의존 칸) | 언리얼 FPlatformFileManager · 모듈 등록 |
| 키트 하나의 에디터 확장은 그 키트 폴더의 `Editor/`(런타임 · 서버로 나뉜 키트는 `<기능>/Editor/`). `Plugin/Editor` 에는 GameFramework 자체와 여러 키트를 잇는 확장만 | 언리얼 플러그인의 <X>Editor 모듈 |
| 폴더 · 파일 이름은 내용으로: `Engine/FileSystem`(층 1 콘텐츠 파일 시스템) · `Engine/Foundation` · `Core/{Foundation,Hash}` · `Engine/2D`(층 6, 언리얼 Paper2D) · `Graphics/LOD` · `RHI/{Backend,Transfer,Diagnostics}` · 에셋 파일 `*Asset` · XML 정의 `*Def` | |
| 둔 것: `Editor*` · `Automation*` 접두어(타입 이름은 전역 유일), App 폴더(실행 파일 호스트 부품 — 언리얼 Launch), `*Info` 정적 사실 표(언리얼 FPixelFormatInfo), `Core/Container` 등 폴더 이름과 같은 접두어 묶음(언리얼 Containers 처럼 평평하게) | |

## 3. 테스트 · 시나리오

`Test/README.md` 가 정본이다(쓰는 법 · 옛 문제와 고친 것 표). 요약: 단위 테스트는 기능 옆 `Tests/` 의 `SW_TEST_CASE` 를 `App --test` 한 번이 엔진 위에서 섞어 돌린다(로그 게이트 · 단언 0 건 실패 · 스냅숏 · 케이스별 쓰기 폴더). 시나리오는 적은 순서대로 도는 단계(`<Wait>` · `<WaitUntil>`), 백엔드별 골든 이미지(`-scenario-update-golden`), 시나리오 안 `<AllowLog>`, 실행별 산출물 폴더 · 사용자 설정이다.

## 4. 남은 일

| 일 | 내용 |
|---|---|
| 테스트 늘리기 | 옛 `Test/`(3f70e665f^ 에 약 950 파일)에서 옮길 것을 기능 옆 `Tests/` 로 — 매크로 이름이 같아 순수 함수 테스트는 include 와 위치만 바꾸면 된다. 엔진 서비스가 이미 서 있으므로 옛 하네스는 필요 없다 |
| 게임 시나리오 | Empty 외 게임의 옛 시나리오(27b17ee91^)를 옮긴다 — 변환은 `<At>` → 순차(`<Wait>`), `probe` → `watch`, 지표 문턱 → 골든, 씬 `WorldSettings` → `SceneSettings`. 활성 게임을 바꿔(`SW_ACTIVE_GAME`) 골든을 뜬다. 에디터 시나리오도 같다(에디터 실행) |
| `GameFramework/Base` · `Plugin/Editor/Base` | 이름이 모호하다(GameFramework 의 기반 라이브러리 · 그 에디터 확장). 바꾸면 모듈 이름(`GF_EditorPlugin_Base`) · 매니페스트가 따라간다 |
| 남은 처리기 인터페이스 | `IEngineFrameObserver` · `IWindowTitleTarget` 은 등록 + 델리게이트 후보(프로토콜 인터페이스는 둔다) |
| 그 밖 | 없는 스크립트를 부르는 곳(`Scripts/qa/ValidateAssets.py` · 설정 색인 · 아이콘 폰트 · `RunBuildBaseline.py`), 지운 `docs/plans/*` 를 가리키는 주석, 생성기 · CMake 파일 이름의 `Abi`(`Scripts/generate/EngineAbiStamp.py` · `cmake/Engine/EngineAbiStamp.cmake`) |

## 5. 이번 세션의 근본 원인 (같은 결함이 남았을 곳)

- 층 위반은 include 글자 검사로는 직접 include 만 보였다 → 빌드 형식(층 부분 + `#error` 껍데기)으로 바꿔 전이 include 까지 막는다. Core 는 아직 티어 표만 있고 껍데기가 없다 — 같은 방식을 Core 에 넣을 후보.
- 실행기 없는 시나리오 검사가 엔진 서비스가 있어야 푸는 이름(입력 슬롯)을 "틀렸다" 로 냈다 → `AutomationStepDependency::InputSlot`. 검사 함수가 이름을 새로 묻게 되면 `findUnresolvedDependency` 에 더한다.

## 6. 이번에 고친 기동 결함

- 깨끗한 체크아웃의 App 이 서지 않았다: 엔진 기본 텍스처의 원본 · 생성기가 없었고(원본으로 둠), 약어 정리가 핫 리로드 ABI 스탬프의 머리를 C++ 쪽만 바꿔 모든 모듈을 거절했다(머리를 C++ 한 곳으로).
- `CommandLineManager::initialize` 가 인자마다 단언을 펼쳐 Debug 스택 틀이 인자 수에 비례했다(인자를 더하면 기동 전 스택 넘침) — 단언을 함수 하나로.
- 기본 파이프라인의 모든 실행이 Empty 확장 데모 오류를 남겼다 — 확장 셰이더를 선언하지 않은 파이프라인에서는 확장이 쉰다.
