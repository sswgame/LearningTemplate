# Architecture (엔진 구조)

SW Engine 의 구조 정본입니다 — 무엇이 무엇을 링크하고, 어디가 모듈 경계이고, 엔진 전체에 걸쳐 지킬 것이 무엇인지.
한 모듈 안의 계약은 그 폴더 README 에 있습니다([문서 지도](docs/02_DocumentMap.md)). 빌드 방법은 [시작하기](docs/01_GettingStarted.md).

## 타깃 그래프

```text
App (exe)  — Engine + RuntimeAPI + AppHost 만 링크한다. 게임 · 에디터 · GameFramework 를 컴파일 때 모른다. (빌드 타깃 Game · Client)
 ├─ Engine        (Core 를 OBJECT 로 흡수해 다시 내보낸다)
 ├─ EditorModule  (Dev 전용, 지연 로드)
 ├─ GameFramework + GF_* 키트  (Dev: 모듈 / Shipping: 실행 파일에 정적 링크)
 ├─ RHI_*         (Dev: 백엔드마다 모듈 — DX11 · DX12 · Vulkan · OpenGL)
 └─ SWGame        (활성 게임 — SW_ACTIVE_GAME)
Server (exe) — 전용 서버. App 과 같은 AppHost(ModuleHost · 매니페스트 해석 · 프레임 시간)를 링크하고, 창 · RHI · 플레이어 설정 단계 없이
               게임 모듈을 고정 틱으로 돌린다. (빌드 타깃 Game · Server — Source/Server/README.md)

Core (OBJECT)     — 로그 · 파일 · 문자열 · 메모리 · 태스크 · 압축 · 네트워크 공통 계층. Engine 을 모른다.
ReflectionParser  — Core 만 링크한다(Engine.dll 과 순환하지 않게). Engine 보다 먼저 빌드된다.
```

- **Dev**: `Engine` 은 DLL, `EditorModule` · `SWGame` · `GF_*` · `RHI_*` 는 동적으로 읽는 모듈이라 App 을 끄지 않고 다시 읽는다([핫리로드](docs/03_LiveReload_and_ABI.md)).
- **Shipping**: 에디터와 리로드 기계가 빠지고, `SW_SHIPPING_RHI_BACKEND` 로 고른 백엔드 하나와 게임 · 키트가 실행 파일 하나에 정적 링크된다.
- **빌드 타깃(`SW_TARGET_TYPE`)**: Game(Dev 기본 — App + Server), Client(배포 `*-Shipping` — App 만, 서버 전용 모듈 없음), Server(`*-Server` 프리셋 —
  Server 만, 에디터 · RHI 백엔드 · X11 없음). 모듈은 매니페스트 `_listTarget`(Client · Server)로 타깃에 들어간다([Source/Server/README.md](Source/Server/README.md)).
- 빌드 옵션(`SW_*`)의 정본은 `cmake/Config/BuildOptions.cmake`, CMake 층은 [cmake/README.md](cmake/README.md).

## 모듈 경계 — C-ABI

App 과 모듈 사이를 넘는 것은 `Source/RuntimeAPI` 의 `extern "C"` 함수 표 · 불투명 핸들 · 서비스 표뿐입니다. RuntimeAPI 는 헤더만 있고
Engine · App 헤더를 include 하지 않습니다. export 매크로 넷(`SW_API` · `SW_MODULE_API` · `SW_GF_API` · `SW_GAMESERVICE_API`)은
서로 바꿔 쓰지 않습니다 — 뜻과 진입점 목록은 [RuntimeAPI/README.md](Source/RuntimeAPI/README.md).

## 엔진 기동 · 종료

- 순서는 단계 표 하나(`Source/Engine/EngineInitStepList.xxx`)가 정합니다. 줄마다 단계와 그 단계가 기다리는 단계를 적고,
  `EngineInitSequence` 가 위상 정렬해 세우고 역순으로 내립니다. `EngineLoop` 와 시험 하네스가 같은 부트스트랩(`EngineBootstrap`)을 씁니다.
- 엔진 서비스는 `Source/RuntimeAPI/Service/EngineServiceList.xxx` 의 줄입니다(칸은 낱말 — `Required` · `GameVisible` · `EngineCreated` …).
- 씬은 모든 타입 공급자(엔진 · 게임 · 에디터 모듈)가 등록을 끝낸 단계(`ModuleTypes`) 뒤에만 읽습니다. `ModuleHost` 는 게임을 에디터보다 먼저 세웁니다.
- 디바이스에 매인 단계는 표의 본문이라, 백엔드 교체는 그 단계들을 내리고 다시 세웁니다.
- 단계 · 루트 파일 하나하나의 계약은 [Source/Engine/README.md](Source/Engine/README.md) "루트 파일".

## 엔진 층

`Source/Engine` 은 링크 단위 하나지만 폴더 include 그래프는 DAG 이고 `Scripts/lint/gate/CheckEngineLayers.py` 가 방향을 강제합니다.
**RHI 는 창을 모르고(표면만 받는다), 월드는 렌더러를 모르고, 렌더러가 씬을 읽어 그립니다.** Engine 은 `Editor/` · `GameFramework/` · `Games/` 를
include 하지 않고, 에디터에는 RuntimeAPI · 델리게이트 · 이벤트로 닿습니다. 티어 표와 상용 엔진과의 대조는 [Source/Engine/README.md](Source/Engine/README.md).
GameFramework 기반 폴더의 층은 [Source/GameFramework/README.md](Source/GameFramework/README.md).

## 리소스 · 데이터

- 경로는 도메인을 포함한 전역 id(`engine/pipeline/forwardpipeline.xml`)이고 찾을 때 소문자로 정규화합니다(`normalizePath`) — 그래서 `Resource/` 아래 이름은 전부 소문자입니다.
  도메인 · 검색 순서 · 텍스처 규칙은 [Resource/README.md](Resource/README.md).
- 텍스처는 DDS 만, 모델은 `.mesh` 만 읽습니다. 원본은 `textures_raw/` · `models_raw/` 에 두고 `App --import-textures` · `--import-models` 로 임포트합니다.
- 데이터는 지금 형식으로만 읽습니다. 이름이나 형식을 바꾸면 `Resource/` 데이터를 다시 쓰고, 별칭(`Alias` · `ValueAlias`)은 다시 쓸 수 없는 데이터(배포한 세이브)가
  생긴 뒤에만 씁니다. 모르는 이름은 텍스트 형식에서 알리고 건너뛰고, 바이너리 형식에서 거절합니다(`SchemaMigrate.h`). `ResourceDataSchemaTest` 가 지킵니다.
- 렌더링은 `RenderPassAsset`(바인딩 틀 — `renderpass/`)과 `RenderPipelineAsset`(패스 순서 — `pipeline/`)으로 나뉘고, `FrameRenderer` 가 파이프라인으로
  `RenderGraph` 를 지어 정렬합니다. 셰이더 바인딩 계약의 정본은 `Resource/engine/shaders/bindingslots.hlsli` 하나입니다([Graphics/README.md](Source/Engine/Graphics/README.md)).
- 리플렉션 코드젠: 헤더의 `REFLECT` · `PROPERTY` · `FUNCTION` · `ENUM` 을 `Tools/ReflectionParser` 가 읽어 `*.gen.cpp` 를 만듭니다. 씬 로드 · 인스펙터 · 핫리로드 ·
  이름으로 컴포넌트 만들기가 모두 그 `TypeInfo` 를 씁니다([Reflection/README.md](Source/Engine/Reflection/README.md) · [ReflectionParser](Tools/ReflectionParser/README.md)).

## 외부 의존성

vcpkg 매니페스트(`vcpkg.json`)가 정본이고 `Scripts/setup/SetupVcpkg.py` 가 설치합니다. 포트가 없거나 고친 판이 필요한 라이브러리는 `ThirdParty/` 에 있습니다.
서드파티 헤더는 감싼 폴더에서만 include 합니다(`CheckThirdPartyIsolation.py`).

## 반드시 지킬 것

> [!CAUTION]
> **틱 중의 구조 변경은 틱 뒤로 미뤄진다.** `GameObjectManager::tick` 은 `onTick()` 을 여러 스레드에서 돌린다. 그 안의 `attachToParent` ·
> `detachFromParent` · `addComponent` · `addTag` 는 구조 변경 큐에 쌓였다가 틱 직후 부른 순서대로 적용된다 — 같은 틱 안에서 바뀐 계층을 기대하지 않는다.
> 틱 중 `addComponent` 는 `nullptr` 을 돌려주므로 만들고 채우는 일은 `GameObjectManager::executeOrDeferPostTick` 한 블록에 둔다.

> [!WARNING]
> **RHI ABI 도장.** `RHIModuleAbi.h` 를 바꾸면 엔진과 모든 `RHI_*` 백엔드를 함께 다시 빌드한다. 낡은 백엔드 DLL 은 함수 포인터가 어긋나 바로 죽는다.

> [!IMPORTANT]
> **모듈 안의 static 은 리로드에서 사라진다.** 클래스 static · 싱글턴이 모듈 DLL 에 있으면 교체 때 사라지거나 주소가 바뀐다. 남아야 하는 상태는 `Engine` 이나 `App` 에 둔다
> (리로드에서 지킬 것 전부는 [핫리로드](docs/03_LiveReload_and_ABI.md) 3 절).

- **렌더 패킷은 자기가 역참조하는 것을 소유한다.** 게임 스레드가 만든 메시 · 머티리얼 · 인스턴스는 `shared_ptr` 로 `GpuSceneSnapshot` 에 실려 렌더 스레드로 간다.
  생포인터를 싣지 않고, 렌더에 실리는 객체는 Engine 의 `create()` 로 만든다(모듈 DLL 이 만든 `shared_ptr` 은 모듈이 내려간 뒤 놓을 수 없다).
  규칙은 [Graphics/README.md](Source/Engine/Graphics/README.md) "소유와 수명", 검사는 `Scripts/lint/gate/CheckRenderOwnership.py`.
- **모듈 리로드는 App 의 것이다.** 감시 · 그림자 복사 · 교체(`LiveReloadManager`)는 `Source/App/Module/` 에 있고 Shipping 에서는 파일째 빠진다. Engine 은 지연 로드 훅이 묻는
  `IModuleHandleProvider` 하나만 안다. 모듈 DLL 은 씬이 사라지고 서비스는 남은 구간(`EngineLoop::setOnScenesReleased`)에서만 내린다([App/README.md](Source/App/README.md)).
- **렌더 스레드는 씬을 읽지 않는다.** 게임 스레드가 만든 스냅샷(`GpuSceneSnapshot`)이 유일한 통로다([Renderer/README.md](Source/Engine/Graphics/Renderer/README.md)).

## 시험

시험 실행 파일 · CTest 항목 · 라벨(`nogpu` · `hostgpu` · `lint` …) · 스위트 규칙의 정본은 [Test/README.md](Test/README.md)입니다. CI 가 못 돌리는 스위트는
자기 파일에서 `SW_TEST_REQUIRES_HOST( 스위트, "이유" )` 로 선언하고, 린트 CTest 항목은 `Scripts/lint/LintCatalog.py` 가 `gate/` · `selftest/` 폴더를 훑어 만듭니다.
