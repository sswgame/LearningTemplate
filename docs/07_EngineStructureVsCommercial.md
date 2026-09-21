# Engine 구조 — 상용 엔진과 대조한 결과 (2026-09-21)

> 목적: `Source/Engine` 의 뼈대를 언리얼 · Unity · Godot 의 자리와 견주어 **어디가 같고, 어디가 달랐고,
> 무엇을 고쳤고, 무엇은 일부러 두는지**를 한 장에 적는다. 기준은 하나다 — **의존 방향이 상용 엔진과
> 같은가.** 폴더 이름이나 클래스 수가 아니라, "누가 누구를 아는가" 다.
>
> 측정 도구: `py -3 Scripts/lint/report/RunEngineLayerGraph.py` (묶음·티어), 강제는
> `Scripts/lint/gate/CheckEngineLayers.py`. 관련: [Source/Engine/README.md](../Source/Engine/README.md),
> [ARCHITECTURE.md](../ARCHITECTURE.md).

---

## 1. 결과 한 줄

**Engine 코어의 강결합 묶음(일곱 폴더)이 사라졌다.** 폴더 간 include 그래프가 DAG 가 됐고, 그 순서가
상용 엔진의 층 순서와 같다. 엣지 다섯을 뗐고 전부 "위층 것을 아래층이 들고 있던" 모양이었다.

| | 2026-09-13 | 2026-09-21 |
|---|---|---|
| 강결합 묶음 | 7 폴더 (`Graphics` `Module` `Object` `Resource` `Scene` `Sequencer` `Window`) | **없음** |
| 티어 수 | 7 (묶음은 순서 없음) | 10 (전부 순서 있음) |
| 게이트가 강제하는 것 | 묶음 밖의 방향만 | 모든 방향 + `Graphics/Renderer` 분리 |

---

## 2. 층별 대조표

| 층 | 언리얼 | Unity / Godot | 이 저장소 | 판단 |
|---|---|---|---|---|
| 토대 | `Core` (컨테이너·문자열·파일·스레드·메모리) | Godot `core/` | `Source/Core` (STATIC, OBJECT 로 Engine 에 흡수) | **같다.** Core 는 Engine 을 모른다. 리플렉션 파서도 Core 만 링크한다. |
| 리플렉션·직렬화 | `CoreUObject` (UClass · UProperty · 아카이브) | Godot `ClassDB` · Unity C# 리플렉션 | `Engine/Reflection` + `Engine/Serialization` (코드젠 `.gen.cpp`) | **같다.** 인코딩은 Serialization 이 갖고 선언은 Reflection 에 남는다(2026-09-13). |
| 설정 | `GConfig` · `UDeveloperSettings` | `ProjectSettings` | `Engine/Config` (EngineConfig · GameConfig · EngineData) | **같다.** 리플렉션 위, 코어 아래. |
| 에셋 | `AssetRegistry` · `FStreamableManager` · 패키지 | `Resources` · `ResourceLoader` | `Engine/Resource` (AssetDatabase · 팩 · `IAssetCache` 등록부 · 스트리밍 큐) | **같다.** 종류를 늘리는 자리가 인터페이스 하나다. |
| 디바이스 | `RHI` — `void*` 창 핸들로 `FRHIViewport` 를 만든다. 창 시스템(Slate)은 RHI 위 | Godot `RenderingDevice` | `Graphics/RHI` (4 백엔드) | **고쳤다.** RHI 가 `IWindow::getActiveWindow()` 전역을 읽어 창 시스템 아래가 아니라 옆에 있었다 → `Common/IRenderSurface` 를 `IWindow` 가 구현하고 `EngineLoop` 이 넘긴다. |
| GPU 에셋 | `Engine` 의 `UMaterialInterface` · `UStaticMesh` — 컴포넌트가 든다 | Unity `Material` · `Mesh` | `Graphics/Material` · `Mesh` · `Texture` · `Shader` | **같다.** 컴포넌트가 머티리얼·메시를 드는 것은 상용 엔진의 모양이다 — 이 엣지는 결함이 아니다. |
| 렌더러 | `Renderer` — `Engine` 을 보고(프록시·씬) 그린다. `Engine` 은 `RendererInterface` 만 안다 | Unity SRP · Godot `RenderingServer` | `Graphics/Renderer` (FrameRenderer · RenderGraph · GpuScene · RenderThread) | **고쳤다.** (1) `IRHIDevice` 가 렌더 패스 *에셋* 캐시(`RenderPassManager`)를 소유해 RHI 가 Renderer 를 include 했다 → `FrameRenderer` 소유. (2) 셰이더 컴파일 폴더가 "무엇을 구울지" 를 알려고 렌더러를 include 했다 → 정책은 `Renderer/Bake/ShaderBakeDriver`. |
| 월드 | `UWorld` → `AActor` → `UActorComponent`. 액터는 `GWorld` 전역으로 월드를 찾는다 | Godot `SceneTree` → `Node` | `Scene` → `Object`(GameObject · Component) | **고쳤다.** 핸들 해석이 활성 씬을 `SceneManager` 에게 물어 Object 가 Scene 을 include 했다 → 슬롯은 Object 가 갖고 Scene 이 채운다(`GameObjectManager::setActiveManager`). |
| 월드 ↔ 렌더러 | `UWorld` 는 `FScene`(렌더 씬 인터페이스)만 안다. 렌더러 본체를 모른다 | Godot 노드는 `RenderingServer` 에 RID 로만 말한다 | `SceneManager` | **고쳤다.** 에디터 툴바 하나 때문에 `FrameRenderer*` 를 들고 있었다 → 렌더러는 호스트가 내주는 선택 서비스(`EngineServiceList.xxx`). |
| 기능 모듈 | `LevelSequence` · `MovieScene` 은 `Engine` 위의 모듈 — 액터를 알고, 액터는 모른다 | Godot `AnimationPlayer` 는 `scene/` 안의 노드 | `Sequencer` | **고쳤다.** `SequencePlayerComponent` 가 `Object/` 에 있어 Object ↔ Sequencer 가 순환했다 → `Sequencer/` 로. |
| 서브시스템 수명 | `FEngineLoop` + `UEngineSubsystem`(자동 수집) | `PlayerLoop` | `EngineLoop` + `EngineServiceList.xxx`(X-macro 등록표, `owned` 열로 생성·바인딩 생성) | **같은 자리, 다른 수단.** 초기화·종료 **순서**는 손으로 남겼다(백로그 1-0e). 언리얼도 순서는 `Initialize` 안에서 `Collection.InitializeDependency` 로 손으로 적는다. |
| 창·입력 | `ApplicationCore` (창) · `InputCore` — RHI 위 | Godot `DisplayServer` | `Window` · `Input` | **같다(고친 뒤).** Window 는 Resource(스플래시 그림) 위, Input 은 Window 위. RHI 는 이제 둘 다 모른다. |
| 병렬 | `ParallelFor` · `TaskGraph` | Unity Jobs | `Core/Task` + `engine::runParallel` | **같다.** 병렬 시스템의 모양이 하나다(트랜스폼 계층이 첫 예). |

---

## 3. 이번에 고친 것 — 다섯 엣지, 그리고 그 뒤에 있던 생각

각 항목의 형식: **무엇이 거꾸로였나 → 상용 엔진의 자리 → 고친 방법.**

1. **Object → Scene.** `GameObjectManager::resolveOwningManager` 의 폴백이 `engine::getSceneManager().getActiveScene()`
   을 불렀다(나머지 세 파일은 쓰지도 않는 include). → 액터 층은 "현재 월드" 슬롯만 갖고, 월드 관리자가 채운다
   (`GWorld` · `SceneTree::get_singleton()`). → `GameObjectManager::setActiveManager/getActiveManager`(static 슬롯),
   `SceneManager::activateScene` 이 `_pActiveScene` 대입 다섯 자리를 하나로 모아 슬롯을 같이 바꾼다. 매니저 소멸자가
   자기 슬롯을 비우므로 죽은 포인터는 남지 않는다.
2. **RHI → Renderer.** `IRHIDevice::initialize` 가 `RenderPassManager` 를 만들고 `getRenderPassManager()` 로 내줬다.
   쓰는 곳은 `FrameRenderer` 둘뿐이었다. → 디바이스 추상은 렌더 패스 *에셋* 을 모른다. → `FrameRenderer` 가 소유하고
   `initialize/shutdown` 에서 만들고 비운다(패스 자원·그래프가 놓은 **뒤에** 비운다 — 그쪽이 캐시의 포인터를 든다).
3. **Graphics → Window.** `RHI::initialize` · `recreateDevice` · `IRHIDevice::initialize` · `FrameRenderer::ensureTransientResources`
   가 `IWindow::getActiveWindow()` 를 읽었다. → RHI 는 `void*` 핸들과 크기만 받는다(`FRHIViewport`). → `Common/IRenderSurface`
   (핸들 · 디스플레이 · 크기 · 재생성 훅 다섯)를 `IWindow` 가 구현, `RHI::initialize( IRenderSurface* )`,
   `IRHIDevice::setRenderSurface`. 렌더러의 첨부 크기는 창이 아니라 **디바이스의 백버퍼 크기**
   (`IRHIDevice::resize` 가 적어 두고 `resizeInternal` 이 백엔드로 내려간다 — `initialize/initializeInternal` 과 같은 모양).
4. **Scene → Renderer.** `SceneManager::setFrameRenderer/getFrameRenderer` — 소비자는 에디터 뷰포트 툴바 하나.
   → 월드는 그리는 쪽을 모른다. → `EngineServiceList.xxx` 에 `SW_ENGINE_SERVICE_OPT( _pFrameRenderer … 0, 0 )`.
   호스트(`EngineLoop`)가 꽂고, 에디터는 `editor::getService<FrameRenderer>()` 로 받는다. 테스트 하네스에는 없다(선택 행).
5. **Object ↔ Sequencer, Shader → Renderer.** `SequencePlayerComponent` 두 파일을 `Sequencer/` 로. 베이크의
   "무엇을 구울지"(`collectAllRecipes`, 파이프라인 XML · `FrameRendererUtil::getPassDefine`)와 "전부 굽기"
   (`bakeAllShaders`)를 `Renderer/Bake/ShaderBakeDriver` 로. `Shader/Compile/ShaderBaker` 에는 한 장 굽기 · 이름 짓기 ·
   최신 판정만 남는다. → 언리얼의 ShaderCore(한 장 컴파일) 와 Renderer/Engine(무엇을 컴파일할지) 의 선.

그리고 **게이트가 그 선을 지킨다.** `CheckEngineLayers.py` 의 티어 표를 DAG 로 바꾸고, `Graphics/Renderer` 를 최상위
폴더보다 잘게 보게 했다(RHI·Shader 가 Renderer 를 include 하면 실패). 되돌아오는 두 엣지(Object → Scene,
RHI → Renderer)는 자가 검사 조각으로 못박았다(`selfTestCases`).

---

## 4. 같아서 두는 것 — 다시 제안하지 말 것

- **컴포넌트가 머티리얼·메시를 든다 (Object → Graphics 저층).** 언리얼의 `UStaticMeshComponent` 가 `UStaticMesh` ·
  `UMaterialInterface` 를 드는 것과 같다. Godot 식 "노드는 RID 만 안다" 로 바꾸면 모든 컴포넌트에 해석 표가 생기고,
  `shared_ptr` 로 이미 풀어 둔 렌더 패킷 수명 문제가 되살아난다(백로그 1-0 "Mesh · Material 을 ObjectHandle 로").
- **렌더러가 컴포넌트를 읽는다 (Graphics/Renderer → Object · Scene).** `GpuSceneBuilder` 가 `PrimitiveRegistry` ·
  `LightRegistry` 를 훑는 것은 언리얼 `FScene` 이 프리미티브 프록시를 훑는 것과 같은 방향이다. 영속 렌더 씬(`FScene`
  모델)은 2026-09 에 재 보고 기각했다(커밋 `c6777ae3`).
- **`EngineLoop` 이 크다.** `FEngineLoop::Init` 도 그렇다. 서비스 생성·바인딩은 이미 표에서 생성되고, 남은 것은 순서다 —
  순서를 선언으로 옮길 값이 있는지는 백로그 1-0e 의 판단 그대로다.
- **`Scene` 이 기본 머티리얼을 든다.** 언리얼은 `UMaterial::GetDefaultMaterial` 이 엔진 에셋이라 월드가 모른다. 여기는
  씬이 인스턴스화할 때 머티리얼 없는 메시에 채워 준다 — 결과는 같고, 엣지는 Scene → Graphics 저층(허용 방향)이다.
- **`Dialogue` · `Sequencer` · `Localization` 이 Engine 안에 있다.** 언리얼은 모듈/플러그인이지만 전부 Engine 위의
  런타임 모듈이다. 이 저장소는 물리 분할을 하지 않으므로(README "후속: 물리 분할 — 설계만") 폴더 티어로 같은 선을 긋는다.
- **물리 모듈 분할(EngineRHI / EngineReflection).** 이번 결과가 그 준비다 — 그래프가 DAG 라 어디를 잘라도 순환이 없다.
  자르는 날은 별도 PR.

---

## 5. 다시 재는 법

```powershell
py -3 Scripts/lint/report/RunEngineLayerGraph.py          # 묶음 없음 · 티어 표 (게이트와 다르면 표가 낡은 것)
py -3 Scripts/lint/report/RunEngineLayerGraph.py --edges  # 묶음이 생겼을 때 파일 단위로
py -3 Scripts/lint/gate/CheckEngineLayers.py              # 방향 위반 = 실패
```

묶음이 하나라도 찍히면 **위층 것을 아래층이 들고 있는 것**이다. 이번 다섯 엣지의 공통 처방은 셋 중 하나였다 —
슬롯을 아래로 내리고 위가 채운다(1) · 소유를 위로 올린다(2, 5) · 인터페이스를 토대에 두고 위가 구현한다(3) ·
서비스 표로 내준다(4).
