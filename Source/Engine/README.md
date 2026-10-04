# Engine (핵심 엔진 모듈)

오브젝트(Object), 그래픽스 렌더링(RHI), 리플렉션(Reflection), 씬(Scene) 관리 등 **진짜 게임 엔진의 코어 로직**이 모여있는 곳입니다.

Foundation(로그/파일/문자열 등)은 `Source/Core`의 `Core_objects`에서 한 번만 컴파일되며, Engine이 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 export합니다.

## 내부 티어 (허용 의존 방향)

물리적으로 SHARED를 쪼개지 않은 상태이므로, 폴더 간 include 방향으로만 순환을 가둡니다.
아래 표는 **include 그래프를 위상 정렬해 얻은 것**입니다. 손으로 고른 순서가 아니므로, 코드가
바뀌면 표도 다시 계산해야 합니다(`Scripts/lint/report/RunEngineLayerGraph.py`).

| 티어 | 폴더 | 뜻 |
|---|---|---|
| 0 | `Common` · `Compression` | 토대. Engine 의 어느 것도 참조하지 않는다. |
| 1 | `Audio` · `Reflection` · `Utility` | 리플렉션과, 토대 위의 잎 서브시스템·헬퍼. |
| 2 | `Animation` · `Localization` · `Serialization` | 리플렉션 위에 올라가는 직렬화와 에셋형 잎. |
| 3 | `Config` · `Dialogue` · `Physics` | 설정 — 리플렉션·직렬화로 읽힌다. 물리(설정 표 · 물리 에셋 · 셰이프 서술자가 리플렉션 데이터)도 여기다. |
| 4 | `Resource` · `Spatial` | 에셋 데이터베이스·팩·캐시 등록부. 위의 모두가 읽는다. 공간 분할은 물리의 `AABB` 위에 선다. |
| 5 | `Graphics`(Renderer 제외) · `Window` | RHI · 셰이더 · 머티리얼 · 메시 · 텍스처 — **디바이스와 GPU 에셋**. 창은 표면(`Common/IRenderSurface`)으로만 RHI 에 보인다. |
| 6 | `Input` · `Object` | 컴포넌트 모델. 컴포넌트가 머티리얼·메시(5)를 든다 — 언리얼의 `UStaticMeshComponent` 가 `UMaterialInterface` 를 드는 것과 같은 자리. |
| 7 | `Scene` · `Sequencer` | 월드(씬·씬 매니저)와, 오브젝트 위에서 도는 기능 모듈. **월드는 액터를 알고 액터는 월드를 모른다.** |
| 8 | `Graphics/Renderer` · `Module` | **그리는 쪽**(FrameRenderer · RenderGraph · GpuScene · RenderThread · Cook)과 핫리로드. 씬·컴포넌트를 읽어 그린다 — 언리얼의 Renderer 가 Engine 을 보는 방향. |
| 9 | `EngineLoop` 등 루트 파일 | 전부를 엮는 자리. |

강결합 묶음은 없습니다 — 이 표는 DAG 이고 `CheckEngineLayers` 가 그대로 강제합니다.
다시 계산하려면 `py -3 Scripts/lint/report/RunEngineLayerGraph.py` — 게이트와 같은 규칙(prelude·배선 예외,
`Graphics/Renderer` 분리)으로 묶음과 티어를 찍습니다. `Graphics` 만 폴더보다 잘게 봅니다: `Graphics/Renderer`
는 위(8), 나머지 `Graphics` 는 아래(5) — 상용 엔진의 RHI/RenderCore ↔ Renderer 사이의 선과 같습니다.
상용 엔진과 어디가 같고 어디가 다른지는 [docs/07_EngineStructureVsCommercial.md](../../docs/07_EngineStructureVsCommercial.md).

**규칙: 위층 것을 아래층이 들지 않는다.** 티어를 거스르는 include 가 필요해 보이면 대개 소유가 거꾸로 된 것입니다.
지금 자리는 이렇습니다.

- 오브젝트 핸들(`GameObjectHandle` · `ComponentHandle`)은 자기를 만든 매니저에게 직접 풉니다 — Object 는 활성 씬(`SceneManager`)을 묻지 않습니다.
- 렌더 패스 **에셋** 캐시(`RenderPipelineAssetCache`)는 `FrameRenderer` 가 소유합니다(`IRHIDevice` 가 아니라).
- RHI 와 렌더러는 창을 `Common/IRenderSurface` 로만 봅니다. `IWindow` 가 구현하고 `EngineLoop` 이 넘깁니다(`RHI::initialize( pSurface )`).
  렌더러의 첨부 크기는 창이 아니라 디바이스의 백버퍼 크기(`IRHIDevice::getBackBufferWidth`)입니다.
- 렌더러는 호스트가 내주는 선택 서비스입니다(`EngineServiceList.xxx` 의 `_pFrameRenderer`) — Scene 이 `FrameRenderer*` 를 들지 않습니다.
- 기능 모듈은 오브젝트 위에(`SequencePlayerComponent` 는 `Sequencer/`), 정책은 메커니즘 위에(셰이더 쿠킹의 "무엇을 · 전부"는
  `Renderer/Cook/ShaderCookDriver`, 컴파일 메커니즘은 `Graphics/Shader`) 있습니다.
- 리플렉션 타입의 **인코딩**(`ReflectAny` · `Rpc`)은 Serialization 이 갖습니다. Core 기능만 쓰는 값 타입은 Core 에 둡니다
  (`Core/String/TagID.h` · `Core/Container/ComponentHandle.h`). 설정은 `RHITypes.h` 대신 `Config/RHIBackendType.h` 만 봅니다.

티어가 아닌 것이 둘 있습니다. 검사도 이 둘을 예외로 둡니다.

- **prelude·경로 헬퍼**: `EngineMinimal.h`, `Common/Common.h`, `Resource/ResourceUtil.h`.
  타입 별칭·전방 선언 우산 헤더와 리소스 경로 해석 헬퍼라 어느 티어에서든 쓸 수 있습니다.
- **배선 파일**: `Common/EngineServices.cpp`, `Reflection/ReflectGenerated.h`,
  `Resource/AssetManager.cpp`. 노출하는 모든 서브시스템을 알아야 하는 자리입니다.

금지 include 자동화: `py -3 Scripts/lint/gate/CheckEngineLayers.py` — 위반은 실패입니다.

- Engine → `Editor/` · `GameFramework/` · `Games/` 금지, 그리고 위 티어 방향(표에 없는 새 최상위 폴더도 실패).
- `Games/` · `GameFramework/` → `Engine/Common/EngineServices.h` 금지 — 게임 쪽은 `GameFramework/Base/GameService.h` 의 `game::` 만 씁니다.

물리 모듈 분할(`EngineRHI` / `EngineReflection`)은 설계만 있습니다 — [docs/07](../../docs/07_EngineStructureVsCommercial.md) 참고.

## 주요 시스템 디렉터리 구조
- **Object/**: GameObject · Component · Prefab. 틱/구조 동결·사용법은 [Object/README.md](Object/README.md)
- **Scene/**: Scene · SceneManager · SceneDocument · SceneCooker · ObjectUndoUtil ([Scene/README.md](Scene/README.md))
- **Spatial/**: 2D/3D 공간 분할 가속 구조체(BVHTree3D · SpatialHashGrid2D · SpatialQuadTree · SpatialOctree) ([Spatial/README.md](Spatial/README.md))
- **Reflection/**: 매크로 · TypeRegistry · Builtins. [Reflection/README.md](Reflection/README.md) · 생성기 [ReflectionParser](../../Tools/ReflectionParser/README.md)
- **Graphics/**: RHI · Material · Shader · FrameRenderer. [Graphics/README.md](Graphics/README.md)
- **Input/**: InputManager · InputMap · 장치(Keyboard/Mouse/Gamepad) 추상화. [Input/README.md](Input/README.md)
- **Resource/**: AssetDatabase · AssetManager · ResourceUtil · ResourcePackManager (VFS .pack) · AssetStreamingQueue
  - **에셋 종류를 늘리는 자리는 `IAssetCache` 다.** 경로를 키로 무언가를 들고 있는 캐시는 그 인터페이스를
    구현하고 `AssetManager::registerAssetCache` 로 올린다. 그러면 종료·비우기·진단이 **등록부를 훑어**
    그 캐시까지 지나간다 — 종료 경로에 캐시 이름을 따로 적지 않는다.
    핫리로드가 디바이스를 **인자로** 받는 것도 그 계약이다 — 캐시가 마지막으로 본 디바이스를 들고 있으면
    백엔드를 바꾼 뒤 죽은 포인터가 된다.
  - **모듈(게임·에디터·키트)도 자기 에셋 종류를 올릴 수 있다.** 창구는 이미 열려 있다 —
    `AssetManager` 는 게임 모듈에도 노출되는 서비스이고(`EngineServiceList.xxx` 의 `GameVisible`),
    `IAssetCache.h` 는 모듈이 그냥 include 하면 된다. 규칙은 하나뿐이고 그것이 전부다:

    ```cpp
    // 모듈 초기화에서
    getService<AssetManager>()->registerAssetCache( &_myCache );
    // 모듈 종료에서 — **반드시**
    getService<AssetManager>()->unregisterAssetCache( &_myCache );
    ```

    등록부는 **포인터만** 든다. 모듈 DLL 이 내려가면 그 포인터도 가상 함수 표도 같이 사라지므로,
    내리지 않고 사라지면 다음 비우기가 죽은 코드로 뛴다(엔진 쪽 "Statics die on hot reload" 와 같은 함정).
    두고 가면 종료가 **이름으로** 경고한다 — 등록 시점에 이름을 복사해 두므로 그 진단은 죽은
    포인터를 건드리지 않는다.
  - **티어 때문에 Resource 에 사는 것 둘**: `SpriteClipCache` 는 `IAssetCache` 를 구현하므로 Animation(티어 2)이 아니라 Resource(티어 4)에,
    `PackCompressionUtil` 은 팩 타입(`ResourcePackTypes.h`)을 쓰므로 Compression(티어 0)이 아니라 Resource 에 둔다.
- **Serialization/**: 직렬화 (BinarySerializer · JsonSerializer · XmlSerializer · Archive)
- **Module/**: 모듈 DLL 쪽 계약만 둔다 — `ModuleTypeRegistry`(로드한 모듈의 타입 · 전역 변수 등록과 정리) · `ModuleHandleProvider`(지연 로드 훅이
  섀도 복사본을 묻는 창구) · `DelayLoadNotifyHook.cpp`(모듈 DLL 마다 컴파일되는 지연 로드 훅) · `EngineAbiStamp`(핫 리로드의 엔진 ABI 도장).
  감시 · 섀도 복사 · 다시 로드(`LiveReloadManager`)는 App 의 `App/Module`, 에셋 파일 감시(`FileWatchDispatcher`)는 에디터의
  `Editor/Common/Workspace` 에 있다. 모듈 이미지 수명 계약의 Core 쪽(`IModuleUnloadListener`)은 `Core/Module` 이다.
- **Utility/**: Format (KeyValueFile), Json, Xml, CommandStack, Debug — 진짜 최하위 헬퍼만 둡니다.
- **루트 파일 — 기동 · 종료**:
  - `EngineInitStepList.xxx`: 기동 단계의 등록표(X-macro). 줄 순서가 초기화 순서이고, 줄마다 단계 이름 · 그 초기화에 거는 메모리 태그 ·
    먼저 서야 하는 단계 `{ A, B }` 를 적습니다. 의존이 자기보다 아래 줄이거나 오타면 컴파일 오류이고(`EngineInitSequence.cpp` 의 static_assert),
    의존만으로 위상 정렬한 순서가 줄 순서와 같아야 합니다(`EngineInitSequenceTest.TableIsWrittenInStartupOrder`).
    `ModuleTypes` 단계(호스트가 타입 공급자 — GF · 킷 · 게임 모듈 — 를 올려 등록을 끝냄)가 서야 씬을 읽고 쿠킹합니다(`Headless` 가 그 뒤).
  - `EngineInitSequence`: 표를 읽어 초기화(`initializeAll`) → 초기화한 단계만 역순 종료(`shutdownAll`) → **모든 단계**를 역순 해제(`destroyAll`).
    단계 본문은 호스트가 줄마다 구조체 하나 `<단계>StartupStep`(`initialize` · `shutdown` · `destroy`, `EngineInitStepDefaults` 상속)로 줍니다 —
    `EngineLoop.cpp` 와 시험 하네스(`Test/TestFramework/main.cpp`)가 같은 표를 씁니다. 구조체가 빠지면 컴파일 오류입니다.
  - `EngineBootstrap`: 표 **앞**의 고정 부트스트랩(이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 교착 감지기 · 메모리 프로파일러 · 명령줄 · 전역 변수)과
    표 **뒤**의 끝 정리. 서비스 표를 언제 끊는지 · 로거를 언제 내리는지는 여기 한 곳이 정합니다. 두 호스트가 함께 씁니다.
  - `EngineServiceCollection`: 호스트가 소유하는 서비스 저장소(`Common/EngineServiceList.xxx` 의 `EngineCreated` 줄에서 생성, `destroyAll` 은 목록의 역순).
  - `EngineLoop`(메인 루프 · 단계 구조체) · `EngineMinimal.h`(prelude)
- **메모리 태그**(`Core/Memory/MemoryTag.h`, UE LLM 식): 할당을 용도로 나눕니다. 거는 자리는 하위 시스템의 **진입점**뿐입니다 — 기동 단계(표의 태그 칸),
  에셋 종류별 로드 · 씬 로드 · 렌더러 · 렌더 스레드 · 모듈 호출에서 `SW_MEMORY_SCOPE( Tag )`. 태스크와 엔진이 띄우는 스레드는 띄운 쪽의 태그를 잇습니다.
  태그를 읽는 것은 Debug 의 `MemoryProfiler` 뿐이라 다른 구성에서 비용은 0 입니다. 보고는 `-gv_profileFrames` 프로파일 보고 · 에디터 프로파일러 패널.
- **Task 시스템**: Core의 [Task/README.md](../Core/Task/README.md) (`TaskManager` / `TaskHandle`)

## 동작 방식
- **개발 모드(Dev)**: `SHARED` (DLL) 형태로 빌드되어 동적으로 로드됩니다.
- **배포 모드(Shipping)**: 성능 최적화를 위해 `App`에 `STATIC`으로 묶입니다.

## 핵심 주의사항
`Engine` 내부에 작성된 코드는 **`EditorModule`이나 `Games` / `GameFramework` 로직에 직접 의존하면 안 됩니다.**
엔진은 플랫폼이자 뼈대이므로, 게임별로 달라지는 구체적인 로직이나 에디터 전용 UI 코드가 이 폴더를 더럽히지 않도록 주의하세요.
에디터와 통신이 필요할 때는 RuntimeAPI·공통 인터페이스나 델리게이트를 통합니다.
