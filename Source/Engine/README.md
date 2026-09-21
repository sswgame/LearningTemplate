# Engine (핵심 엔진 모듈)

오브젝트(Object), 그래픽스 렌더링(RHI), 리플렉션(Reflection), 씬(Scene) 관리 등 **진짜 게임 엔진의 코어 로직**이 모여있는 곳입니다.

Foundation(로그/파일/문자열 등)은 `Source/Core`의 `Core_objects`에서 한 번만 컴파일되며, Engine이 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 export합니다.

## 내부 티어 (허용 의존 방향)

물리적으로 SHARED를 쪼개지 않은 상태이므로, 폴더 간 include 방향으로만 순환을 가둡니다.
아래 표는 **include 그래프를 위상 정렬해 얻은 것**입니다. 손으로 고른 순서가 아니므로, 코드가
바뀌면 표도 다시 계산해야 합니다(`Scripts/lint/report/RunEngineLayerGraph.py`).

| 티어 | 폴더 | 뜻 |
|---|---|---|
| 0 | `Common` · `Compression` · `Physics` | 토대. Engine 의 어느 것도 참조하지 않는다. |
| 1 | `Audio` · `Reflection` · `Spatial` · `Utility` | 리플렉션과, 토대 위의 잎 서브시스템·헬퍼. |
| 2 | `Animation` · `Localization` · `Serialization` | 리플렉션 위에 올라가는 직렬화와 에셋형 잎. |
| 3 | `Config` · `Dialogue` | 설정 — 리플렉션·직렬화로 읽힌다. |
| 4 | `Resource` | 에셋 데이터베이스·팩·캐시 등록부. 위의 모두가 읽는다. |
| 5 | `Graphics`(Renderer 제외) · `Window` | RHI · 셰이더 · 머티리얼 · 메시 · 텍스처 — **디바이스와 GPU 에셋**. 창은 표면(`Common/IRenderSurface`)으로만 RHI 에 보인다. |
| 6 | `Input` · `Object` | 컴포넌트 모델. 컴포넌트가 머티리얼·메시(5)를 든다 — 언리얼의 `UStaticMeshComponent` 가 `UMaterialInterface` 를 드는 것과 같은 자리. |
| 7 | `Scene` · `Sequencer` | 월드(씬·씬 매니저)와, 오브젝트 위에서 도는 기능 모듈. **월드는 액터를 알고 액터는 월드를 모른다.** |
| 8 | `Graphics/Renderer` · `Module` | **그리는 쪽**(FrameRenderer · RenderGraph · GpuScene · RenderThread · Bake)과 핫리로드. 씬·컴포넌트를 읽어 그린다 — 언리얼의 Renderer 가 Engine 을 보는 방향. |
| 9 | `EngineLoop` 등 루트 파일 | 전부를 엮는 자리. |

**강결합 묶음은 이제 없습니다 (2026-09-21).** 이 표는 DAG 이고 `CheckEngineLayers` 가 그대로 강제합니다.
다시 계산하려면 `py -3 Scripts/lint/report/RunEngineLayerGraph.py` — 게이트와 같은 규칙(prelude·배선 예외,
`Graphics/Renderer` 분리)으로 묶음과 티어를 찍습니다. `Graphics` 만 폴더보다 잘게 봅니다: `Graphics/Renderer`
는 위(8), 나머지 `Graphics` 는 아래(5) — 상용 엔진의 RHI/RenderCore ↔ Renderer 사이의 선과 같습니다.
상용 엔진과 어디가 같고 어디가 다른지는 [docs/07_EngineStructureVsCommercial.md](../../docs/07_EngineStructureVsCommercial.md).

> **일곱 폴더 묶음이 어떻게 풀렸나 (2026-09-21).** 엣지 다섯이었고 전부 "위층 것을 아래층이 들고 있던" 모양이었다.
>
> - `Object → Scene`: 핸들의 지연 해석이 활성 씬을 `SceneManager` 에게 물었다 → 슬롯은 Object 가 갖고 Scene 이
>   채운다(`GameObjectManager::setActiveManager`, 언리얼 `GWorld` · Godot `SceneTree` 의 자리). 나머지 셋은 쓰지도
>   않는 include 였다.
> - `RHI → Renderer`: `IRHIDevice` 가 `RenderPassManager`(패스 **에셋** 캐시)를 소유했다 → `FrameRenderer` 가 소유한다.
> - `Graphics → Window`: RHI 와 렌더러가 `IWindow::getActiveWindow()` 전역을 읽었다 → `Common/IRenderSurface` 를
>   `IWindow` 가 구현하고 `EngineLoop` 이 넘긴다(`RHI::initialize( surface )`). 렌더러의 첨부 크기는 창이 아니라
>   디바이스의 백버퍼 크기(`IRHIDevice::getBackBufferWidth`)다.
> - `Scene → Renderer`: `SceneManager` 가 에디터 툴바 하나를 위해 `FrameRenderer*` 를 들었다 → 렌더러는 호스트가
>   내주는 선택 서비스다(`EngineServiceList.xxx` 의 `_pFrameRenderer`).
> - `Object ↔ Sequencer` · `Shader → Renderer`: `SequencePlayerComponent` 는 `Sequencer/` 로, 베이크의 정책(무엇을 ·
>   전부)은 `Renderer/Bake/ShaderBakeDriver` 로. 기능 모듈이 오브젝트 위에, 정책이 메커니즘 위에 있게 됐다.
>
> 그 전(2026-09-13)에는 **열 개**였고 `Reflection`·`Serialization`·`Config` 가 세 줄 때문에 끌려 들어가 있었다:
> 직렬화기가 `TagID`·`ComponentHandle` 때문에 `Object` 를 include 했고(두 타입 모두 Core 기능만 쓰는 값 타입이라
> `Core/String/TagID.h` · `Core/Container/ComponentHandle.h` 로 내렸다), `Reflection` 이 `ReflectAny`·`Rpc` 의
> **인코딩** 때문에 `Serialization` 을 include 했고(규칙: 리플렉션 타입의 인코딩은 Serialization 이 갖는다),
> `EngineConfig` 가 `RHIBackend` 이름 하나 때문에 `RHITypes.h` 전체를 끌어왔다(→ `Config/RHIBackendType.h`).

티어가 아닌 것이 둘 있습니다. 검사도 이 둘을 예외로 둡니다.

- **prelude·경로 헬퍼**: `EngineMinimal.h`, `Common/Common.h`, `Resource/ResourceUtil.h`.
  타입 별칭·전방 선언 우산 헤더와 리소스 경로 해석 헬퍼라 어느 티어에서든 쓸 수 있습니다.
- **배선 파일**: `Common/EngineServices.cpp`, `Reflection/ReflectGenerated.h`,
  `Resource/ResourceManager.cpp`. 노출하는 모든 서브시스템을 알아야 하는 자리입니다.

금지 include 자동화: `py -3 Scripts/lint/gate/CheckEngineLayers.py`
(Engine → `Editor/` / `GameFramework/` / `Games/` 금지 + 위 티어 방향. **위반은 실패입니다** —
예전에는 손으로 고른 네 쌍만 경고로 찍고 실패시키지 않아서, 쌓여도 아무도 몰랐습니다.)

후속(별 PR): `EngineRHI` / `EngineReflection` 물리 분할 — 이 저장소 로드맵에서는 설계만.

## 주요 시스템 디렉터리 구조
- **Object/**: GameObject · Component · Prefab. 틱/구조 동결·사용법은 [Object/README.md](Object/README.md)
- **Scene/**: Scene · SceneManager · 2D/3D 공간 분할 가속 구조체 ([Scene/README.md](Scene/README.md))
- **Reflection/**: 매크로 · TypeRegistry · Builtins. [Reflection/README.md](Reflection/README.md) · 생성기 [ReflectionParser](../../Tools/ReflectionParser/README.md)
- **Graphics/**: RHI · Material · Shader · FrameRenderer. [Graphics/README.md](Graphics/README.md)
- **Input/**: InputManager · ActionMap · 장치(Keyboard/Mouse/Gamepad) 추상화. [Input/README.md](Input/README.md)
- **Resource/**: AssetDatabase · ResourceManager · ResourceUtil · ResourcePackManager (VFS .pack) · AssetStreamingQueue
  - **에셋 종류를 늘리는 자리는 `IAssetCache` 다.** 경로를 키로 무언가를 들고 있는 캐시는 그 인터페이스를
    구현하고 `ResourceManager::registerAssetCache` 로 올린다. 그러면 종료·비우기·진단이 **등록부를 훑어**
    그 캐시까지 지나간다. 이름으로 캐시를 적던 시절 종료 경로가 프리팹 캐시만 빠뜨리고 있었다.
    핫리로드가 디바이스를 **인자로** 받는 것도 그 계약이다 — 캐시가 마지막으로 본 디바이스를 들고 있으면
    백엔드를 바꾼 뒤 죽은 포인터가 된다.
  - **모듈(게임·에디터·키트)도 자기 에셋 종류를 올릴 수 있다.** 창구는 이미 열려 있다 —
    `ResourceManager` 는 게임 모듈에도 노출되는 서비스이고(`EngineServiceList.xxx` 의 `gameAllowed=1`),
    `IAssetCache.h` 는 모듈이 그냥 include 하면 된다. 규칙은 하나뿐이고 그것이 전부다:

    ```cpp
    // 모듈 초기화에서
    getService<ResourceManager>()->registerAssetCache( &_myCache );
    // 모듈 종료에서 — **반드시**
    getService<ResourceManager>()->unregisterAssetCache( &_myCache );
    ```

    등록부는 **포인터만** 든다. 모듈 DLL 이 내려가면 그 포인터도 가상 함수 표도 같이 사라지므로,
    내리지 않고 사라지면 다음 비우기가 죽은 코드로 뛴다(엔진 쪽 "Statics die on hot reload" 와 같은 함정).
    두고 가면 종료가 **이름으로** 경고한다 — 등록 시점에 이름을 복사해 두므로 그 진단은 죽은
    포인터를 건드리지 않는다.
- **Serialization/**: 직렬화 (BinarySerializer · JsonSerializer · XmlSerializer · Archive)
- **Module/**: LiveReloadManager · ModuleTypeRegistry · ReloadFileManager. DLL 핫스왑과 그에 따른
  TypeInfo 재결합을 담당합니다. 예전에는 `Utility/Module` 에 있었지만, 모든 로드된 Scene 의
  GameObjectManager 를 다시 묶는 **상위 서브시스템**이라 `Utility`(최하위 티어)가 아닙니다.
- **Utility/**: Format (KeyValueFile), Json, Xml, CommandStack, Debug — 진짜 최하위 헬퍼만 둡니다.
- **Task 시스템**: Core의 [Task/README.md](../Core/Task/README.md) (`TaskManager` / `TaskHandle`)

## 동작 방식
- **개발 모드(Dev)**: `SHARED` (DLL) 형태로 빌드되어 동적으로 로드됩니다.
- **배포 모드(Shipping)**: 성능 최적화를 위해 `App`에 `STATIC`으로 묶입니다.

## 핵심 주의사항
`Engine` 내부에 작성된 코드는 **`EditorModule`이나 `Games` / `GameFramework` 로직에 직접 의존하면 안 됩니다.**
엔진은 플랫폼이자 뼈대이므로, 게임별로 달라지는 구체적인 로직이나 에디터 전용 UI 코드가 이 폴더를 더럽히지 않도록 주의하세요.
에디터와 통신이 필요할 때는 RuntimeAPI·공통 인터페이스나 델리게이트를 통합니다.
