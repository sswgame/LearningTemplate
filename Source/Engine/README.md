# Engine — 엔진 본체와 폴더 계층

## 이것은 무엇이고 왜 있나

`Source/Engine` 은 게임 엔진의 본체입니다. 리플렉션, 에셋, 렌더링, 게임 오브젝트, 씬, 입력, 물리, 오디오, UI가 모두 여기 있습니다.
게임 모듈과 에디터는 이 위에 올라가고, 엔진은 그 둘을 모릅니다.

빌드 단위로는 하나입니다. Dev 구성에서는 `Engine.dll` 하나, Shipping 구성에서는 실행 파일 하나에 정적으로 링크됩니다.
로그, 파일, 문자열, 태스크 같은 토대 코드는 [Core](../Core/README.md)에 있습니다. Core는 `Core_objects` 라는 OBJECT 라이브러리로 한 번만 컴파일되고, Engine이 그 결과를 링크해 함께 내보냅니다.

링크 단위가 하나이면 폴더끼리 아무 방향으로나 include할 수 있어서, 금방 서로가 서로를 아는 덩어리가 됩니다.
그래서 이 저장소는 폴더를 **티어**로 나누고, 아래 티어가 위 티어를 include하지 못하게 게이트로 막습니다.
언리얼이 Core, CoreUObject, Engine, Renderer를 별도 모듈로 나눠 얻는 의존 방향을, 여기서는 폴더 규칙으로 얻습니다.

이 문서는 그 티어 규칙과 엔진 전체의 기동과 종료 순서를 설명합니다. 각 폴더의 자세한 내용은 폴더마다의 README에 있습니다.

## 머릿속 그림

| 티어 | 폴더 |
|---|---|
| 0 | `Common`, `Compression`, `Observability` |
| 1 | `Reflection`, `Console` |
| 2 | `Utility`, `Serialization` |
| 3 | `Profiling`, `TileMap`, `Config`, `Physics` |
| 4 | `Resource`, `Spatial`, `Navigation` |
| 5 | `Animation`, `Localization` |
| 6 | `Audio`, `Dialogue`, `Text` |
| 7 | `Graphics` |
| 8 | `Window`, `Object` |
| 9 | `Input`, `Scene`, `Sequencer`, `Character`, `Environment` |
| 10 | `UserSettings`, `Module`, `Destruction` |
| 11 | `Telemetry`, `Renderer`, `UI`, `Automation` |
| 12 | `EngineLoop` 등 루트 파일 |

표의 숫자는 include 그래프에서 계산한 값입니다(`py -3 Scripts/dev/MoveEngineFolders.py --sync-tier` 가 게이트 표와 이 표를 함께 고칩니다). 폴더마다의 뜻은 아래 목록에 있습니다.

**아래 티어는 위 티어를 include하지 않습니다.** 같은 티어끼리는 include할 수 있습니다. `CheckEngineLayers.py` 게이트가 이 규칙을 확인하고, 위반은 경고가 아니라 실패입니다.

같은 게이트는 최상위 폴더 방향도 봅니다. Core 는 아무것도 모르고, App 은 Engine 과 RuntimeAPI 만 압니다. Editor 는 게임, 키트, 호스트를 모르고, Server 는 Editor, GameFramework, Games 를 모릅니다. Engine, GameFramework, Games 는 호스트(App, Server)를 모르고, 게임은 다른 게임을 include 하지 않습니다.
표에 없는 새 최상위 폴더도 실패합니다. 같은 게이트가 Engine에서 `Editor/`, `GameFramework/`, `Games/` 를 include하는 것도 막습니다.

폴더마다 알아 둘 점은 다음과 같습니다. 티어 숫자는 의존이 바뀌면 함께 바뀌므로 여기서는 폴더 이름으로 적습니다.

- **토대.** Engine의 어느 것도 참조하지 않습니다. `Compression` 은 외부 라이브러리(lz4, zstd)로 Core의 인터페이스를 구현합니다. Core가 그 라이브러리에 종속되지 않게 하려고 여기 둡니다.
  네트워크 보안(OpenSSL)은 온라인을 쓰는 게임만 필요하므로 Engine 이 아니라 `GameFramework/Base/Online/Security` 에 있습니다 — Engine.dll 은 libssl · libcrypto 를 모릅니다.
  `Observability` 는 Core만 보는 서버 운영 관측입니다. 엔진의 HTTP(운영 끝점 `OpsHttpEndpoint`, 텔레메트리 · 크래시 보고가 쓰는 `IHttpClient`)도 여기 한 곳에 있습니다.
- **설정과 물리.** 물리의 설정 테이블, 물리 에셋, 셰이프 서술자가 리플렉션 데이터라서 물리가 설정과 같은 자리에 있습니다.
- **에셋과 공간.** `Resource` 는 에셋 데이터베이스, 팩, 캐시 레지스트리(`IAssetCache`)입니다. 공간 분할(`Spatial`)은 물리의 `AABB` 를 씁니다.
  내비메시(`Navigation`)는 물리의 셰이프 서술자를 읽어 베이크하고, 씬 쪽 내비게이션 컴포넌트가 그것을 씁니다.
- **기능 데이터.** `Animation` 과 `Localization` 은 자기 에셋 캐시(`AnimationAssetCache`, `SpriteClipCache`, `LocalizationReloadCache`)가 `IAssetCache` 를 구현하므로 `Resource` 위에 있습니다.
  오디오는 립싱크 가져오기가 애니메이션 표정 트랙을 쓰므로, 대화와 글자는 로컬라이즈된 글을 쓰므로 그 위에 있습니다.
- **디바이스.** RHI, 셰이더, 머티리얼, 메시, 텍스처가 있는 계층입니다. 창(`Window`)은 RHI 가 정한 `Graphics/RHI/IRenderSurface` 인터페이스를 구현하므로 이 계층 위에 있습니다. 언리얼에서 창 시스템(Slate)이 RHI 위에 있는 것과 같습니다.
  글자(`Text`)는 글꼴 파일을 읽어 CPU 메모리에 SDF 아틀라스를 만들 뿐 GPU를 모릅니다. 아틀라스 업로드는 렌더러가 합니다.
- **컴포넌트 모델.** 컴포넌트가 머티리얼과 메시를 보관합니다. 언리얼의 `UStaticMeshComponent` 가 `UMaterialInterface` 를 보관하는 것과 같습니다.
- **월드와 기능 모듈.** 월드(씬, 씬 매니저)와 오브젝트 위에서 도는 기능 모듈입니다. **월드는 액터를 알고 액터는 월드를 모릅니다.**
  플레이어 옵션(`UserSettings`)은 입력, 오디오, 언어, 창에 값을 넣으므로 그 위에 있습니다.
- **그리는 쪽과 상위 기능.** 그리는 쪽(FrameRenderer, RenderGraph, GpuScene, RenderThread, 셰이더 쿠킹)과 핫 리로드입니다. 언리얼의 Renderer가 Engine을 보는 방향과 같습니다.
  텔레메트리와 크래시 보고는 사용자 설정에서 동의를 읽습니다. 파괴(`Destruction`)는 캐릭터 형상의 자르기 도구를 씁니다.
  런타임 UI는 입력, 글자, 사용자 설정을 쓰지만 렌더러와는 서로 include하지 않습니다. 둘 사이에 오가는 값은 그리기 목록뿐입니다(언리얼의 Slate와 SlateRHIRenderer 관계).

`Renderer` 는 그리는 쪽 티어이고 `Graphics` 는 디바이스 티어입니다. 상용 엔진의 RHI, RenderCore와 Renderer 사이의 경계와 같습니다.

티어가 아닌 파일이 두 종류 있고, 게이트도 이 둘을 예외로 둡니다.

- **prelude와 경로 헬퍼**(`EngineMinimal.h`, `Resource/ResourceUtil.h`)는 어느 티어에서든 씁니다. 타입 별칭과 전방 선언을 모은 헤더, 리소스 경로를 해석하는 헬퍼이기 때문입니다. `Common/Common.h` 는 티어 0 이라 예외가 필요 없습니다.
- **배선 파일**(`Reflection/ReflectGenerated.h`, `Resource/AssetManager.cpp`)은 노출하는 모든 서브시스템을 알아야 하는 곳입니다.
- 두 목록은 `CheckEngineLayersGate.mapExemption` 이 정본입니다. 더는 위 티어를 include 하지 않는 줄은 낡은 예외로 실패합니다.

## 따라 해 보기 — 새 코드가 들어갈 폴더 고르기

애니메이션 이벤트를 처리하는 새 클래스를 만든다고 해 보겠습니다. 이 클래스는 캐릭터의 소켓 정보를 읽어야 합니다. 어느 폴더에 두어야 할까요?

### 1단계 — 지금의 의존 그래프 보기

```powershell
py -3 Scripts/lint/report/RunEngineLayerGraph.py
```

보고서는 폴더마다 계산한 티어와 그 폴더가 include하는 폴더를 보여 줍니다. 몇 줄만 옮기면 다음과 같습니다.

```text
   5  Animation            -> Common, Reflection, Resource, Serialization
   9  Character            -> Animation, Audio, Common, Graphics, Object, Physics, Reflection, Resource, Serialization, Spatial
```

계산한 티어가 게이트의 테이블과 다르면 그 줄 끝에 `<- 게이트 표는 N` 이 붙습니다. 같은 티어끼리의 include는 허용되므로 계산 값이 테이블보다 높게 나올 수 있습니다.
폴더끼리 서로 include하는 순환이 있으면 보고서 맨 앞에 따로 나옵니다. 지금은 `Automation` 과 `UI` 가 서로 include하는 순환 하나가 보고됩니다. 둘이 같은 티어라 게이트는 통과합니다.

### 2단계 — 티어 정하기

새 클래스가 include할 가장 높은 폴더가 `Character`(티어 9)이므로, 이 클래스는 티어 9 이상의 폴더에 있어야 합니다.
`Animation`(티어 5)에 두면 위 티어를 include하게 되어 게이트가 실패합니다. 실제로 애니메이션 알림 디스패치는 그래서 `Character/AnimNotify/` 에 있습니다.

### 3단계 — 게이트로 확인하기

```powershell
py -3 Scripts/lint/gate/CheckEngineLayers.py
```

통과하면 `[CheckEngineLayers] OK (… files scanned in parallel)` 이 나옵니다. 이 게이트는 커밋 훅과 CI에서도 돕니다.

티어를 거스르는 include가 필요해 보이면 대개 소유가 거꾸로 된 것입니다. 이럴 때 쓰는 해법은 네 가지입니다.

1. 찾는 쪽이 이미 가진 객체로 찾습니다. 오브젝트 핸들은 핸들을 만든 매니저가 찾으므로 Object가 활성 씬(`SceneManager`)을 묻지 않습니다.
2. 소유를 위로 올립니다. 렌더 패스 에셋 캐시(`RenderPipelineAssetCache`)는 `IRHIDevice` 가 아니라 `FrameRenderer` 가 소유합니다.
3. 인터페이스를 아래 티어에 두고 위에서 구현합니다. RHI와 렌더러는 창을 RHI 쪽 인터페이스 `Graphics/RHI/IRenderSurface` 로만 보고, 위 티어의 `IWindow` 가 구현하며 `EngineLoop` 이 넘깁니다.
   그래서 렌더러의 첨부 크기는 창이 아니라 디바이스의 백버퍼 크기(`IRHIDevice::getBackBufferWidth`)입니다.
4. 서비스 테이블로 내줍니다. 렌더러는 호스트가 내주는 선택 서비스(`_pFrameRenderer`)이고, Scene은 `FrameRenderer*` 를 보관하지 않습니다.

같은 원칙으로 정해진 위치가 몇 가지 더 있습니다. 기능 모듈은 오브젝트 위에 있고(`SequencePlayerComponent` 는 `Sequencer/`), 정책은 메커니즘 위에 있습니다.
셰이더 쿠킹에서 무엇을 쿠킹할지는 `Renderer/Cook/ShaderCookDriver` 가 정하고, 컴파일 방법은 `Graphics/Shader` 에 있습니다.
리플렉션 타입의 인코딩(`ReflectAny`, `Rpc`)은 Serialization에 있습니다. Core 기능만 쓰는 값 타입(`Core/String/TagID.h`, `Core/Container/ComponentHandle.h`)은 Core에 둡니다.
설정은 `RHITypes.h` 대신 `Config/RHIBackendType.h` 만 봅니다.

## 작동 원리

### 기동과 종료

엔진의 기동 순서는 `EngineInitStepList.xxx` 한 파일에 적혀 있습니다. X-macro 목록이고, 줄마다 단계 이름, 그 단계에서 쓸 메모리 태그, 실행할 호스트, 먼저 초기화되어야 하는 단계 목록을 적습니다.
호스트 값은 `All`, `Client`, `Server` 이고, 전용 서버는 `Client` 줄을 건너뜁니다. 언리얼의 `USubsystem` 이 `InitializeDependency` 로 의존을 선언하는 것과 같은 방식입니다.

의존은 `{ A, B }` 같은 식별자 목록입니다. 오타가 있거나 자기보다 아래 줄에 의존하면 컴파일 오류가 납니다(`EngineInitSequence.cpp` 의 `static_assert`).
의존만으로 위상 정렬한 순서(동점은 이름 순)는 줄 순서와 같아야 합니다. 의존을 빠뜨리면 `EngineInitSequenceTest.TableIsWrittenInStartupOrder` 가 실패합니다.

`EngineInitSequence` 가 이 목록을 읽고 세 번 돕니다.

1. `initializeAll` 이 위에서부터 초기화합니다.
2. `shutdownAll` 이 초기화에 성공한 단계만 역순으로 종료합니다.
3. `destroyAll` 이 **모든 단계**를 역순으로 해제합니다. 실패했거나 건너뛰었거나 도달하지 못한 단계도 포함하므로, 해제 본문은 null을 안전하게 다뤄야 합니다.

단계의 본문은 호스트가 단계마다 구조체 하나(`<단계>StartupStep`)로 줍니다. 구조체는 `EngineInitStepDefaults` 를 상속하고 `initialize`, `shutdown`, `destroy` 를 가집니다.
호스트는 `EngineLoop.cpp` 와 테스트 하네스(`Test/TestFramework/main.cpp`) 두 곳이고, 같은 목록을 씁니다. 구조체가 빠지면 `EngineInitStepTable` 이 컴파일 오류를 냅니다.

씬은 `ModuleTypes` 단계가 끝난 뒤에만 읽고 쿠킹합니다. 이 단계에서 호스트가 GameFramework, 키트, 게임 모듈을 로드해 타입 등록을 끝냅니다. `Headless` 단계는 그 뒤에 있습니다.

목록 **앞**의 고정 부트스트랩과 목록 **뒤**의 마무리는 `EngineBootstrap` 이 합니다. 이름 풀, 로거, 크래시 핸들러, 리소스 루트, 교착 감지기, 메모리 프로파일러, 명령줄, 전역 변수가 앞쪽입니다.
서비스 테이블을 언제 끊고 로거를 언제 종료할지는 이 한 곳이 정합니다. 로거 스레드는 메모리 프로파일러보다 먼저 시작하고, 로거 객체는 맨 마지막에 해제합니다. 해제 중의 진단도 로그에 남기기 위해서입니다.

### 엔진 서비스

엔진 서비스는 `Source/RuntimeAPI/Service/EngineServiceList.xxx` 의 줄 하나로 정의합니다. 줄마다 타입, getter 이름, 그리고 낱말로 된 세 열이 있습니다.

- `Required` 또는 `Optional` 은 바인딩 검사에 넣을지입니다.
- `GameVisible` 또는 `HostOnly` 는 게임 모듈에 보일지입니다. `HostOnly` 서비스를 게임이 요청하면 `nullptr` 를 받습니다.
- `EngineCreated` 또는 `HostCreated` 는 누가 만들지입니다. `EngineCreated` 는 `EngineServiceCollection::createAll()` 이 만들고 `bindInto()` 가 연결합니다.

목록이 RuntimeAPI에 있는 이유는 서비스 id가 호스트와 모듈 사이의 계약이기 때문입니다. 모듈은 `getService<T>()` 로 타입에서 id를 찾습니다.
`destroyAll` 은 목록의 역순으로 해제하고, 그 순서는 `EngineServiceCollection::makeDestroyOrder()` 로 테스트합니다.

### 메모리 태그

메모리 태그(`Core/Memory/MemoryTag.h`)는 할당을 용도별로 나눕니다. 언리얼의 LLM과 같은 방식입니다.
태그를 거는 곳은 하위 시스템의 **진입점**뿐입니다. 기동 단계(목록의 태그 열), 에셋 종류별 로드, 씬 로드, 렌더러, 렌더 스레드, 모듈 호출에서 `SW_MEMORY_SCOPE( Tag )` 를 씁니다.
태스크와 엔진이 띄우는 스레드는 띄운 쪽의 태그를 이어받습니다. 태그를 읽는 것은 Debug의 `MemoryProfiler` 뿐이라 다른 구성에서는 비용이 없습니다.
결과는 `-gv_profileFrames` 프로파일 보고와 에디터 프로파일러 패널에서 봅니다.

`Profiling/MemoryBudgetMonitor` 는 메모리 태그 예산(`Config/Engine/MemoryBudget.json`)을 프레임 끝에 검사합니다. 모르는 태그나 키는 오류입니다.
`-gv_memoryReport` 표와 FrameProfiler 카운터도 이 클래스가 만듭니다(`EngineLoop::endFrame`).

### 에셋과 리소스

`Resource/` 에는 에셋 데이터베이스(`AssetDatabase`), 에셋 매니저(`AssetManager`), 팩 파일 가상 파일 시스템(`ResourcePackManager`), 스트리밍 큐(`AssetStreamingQueue`)가 있습니다.

팩 리더는 위치를 지정해 읽으므로 여러 스레드가 잠금 없이 읽을 수 있습니다. 매니저는 리더를 찾는 동안만 잠급니다.
`readFileAsync` 는 `AsyncFileIo` 에 구간 읽기를 걸고, 압축 해제와 CRC 검사는 태스크 워커에서 합니다. 스트리밍 큐의 바이트 요청은 `ResourceUtil::readBinaryResourceAsync` 로 갑니다.

스트리밍 큐는 호스트 전용 서비스(`HostOnly`)라 게임 모듈에는 보이지 않습니다. `EngineLoop` 의 기동 단계가 만들고 종료합니다.
완료 콜백은 엔진 루프가 메인 스레드에서 프레임마다 내보냅니다(`update()`). 결과를 값으로 받으려면 `requestAssetFuture` 를 씁니다.

에셋 로드 시간은 `AssetLoadScope` 로 측정합니다(`AssetLoadProfiler`). 종류별 개수와 바이트, IO와 해석과 GPU 업로드 시간, 가장 느린 로드 16개를 기록합니다.
프레임 프로파일러에는 `Asset.<종류>.<단계>` 와 `Asset.Bytes` 구간으로 남습니다. `-gv_assetLoadProfile=0` 은 측정을 끄고, `-gv_assetLoadReport=1` 은 종료할 때 표를 남깁니다.
언리얼 Insights의 LoadTimeProfiler, 유니티의 Loading 마커에 해당합니다.

"같은 키면 같은 객체이고, 마지막 사용자가 놓으면 사라지는" 캐시는 `WeakInternTable` 하나로 만듭니다.
경로로 읽는 에셋은 `SharedAssetTable` 로 보관합니다. 스켈레톤, 클립, 리그, 스프라이트 클립, 캐릭터 데이터가 여기에 속합니다.
코드로 만드는 값은 `WeakInternCache` 로 보관합니다. 내장 도형, 9-슬라이스 메시, 스프라이트 텍스처 인스턴스가 여기에 속합니다. 둘 다 `WeakInternTable` 위에 있습니다.
`WeakInternCache` 는 `IAssetCache` 이므로 `AssetManager` 가 생성자에서 내장 캐시(`_listBuiltInAssetCache`)로 등록하고, 종료할 때 약한 참조 항목까지 지웁니다.

기능 에셋 캐시는 자기 기능 폴더에 있고 `Resource` 의 `IAssetCache` 를 구현합니다. 스켈레톤 · 클립 · 리그는 `Animation/AnimationAssetCache`, 스프라이트 클립은 `Animation/Sprite/SpriteClipCache` 입니다.
`Localization/LocalizationReloadCache` 는 로컬라이제이션 파일의 핫 리로드 진입점입니다. 글 자체는 `LocalizationManager` 가 가지고, 이 캐시의 `clear()` 는 아무것도 지우지 않습니다.
`AssetManager.cpp` 는 이 캐시들을 소유하는 배선 파일이라 티어 예외입니다. `PackCompressionUtil` 은 팩 타입(`ResourcePackTypes.h`)을 쓰므로 Compression(티어 0)이 아니라 여기 있습니다.

### 개발 콘솔과 개발 명령

개발 콘솔과 개발 명령은 Shipping에 없습니다(`SW_DEV_COMMANDS_ENABLED`). 명령 레지스트리는 `Console/DevCommandRegistry` 하나이고, 모듈을 언로드하면 그 모듈의 명령이 빠집니다.
명령은 자기 `.cpp` 에 `SW_DEV_COMMAND( 변수, "이름", "사용법", "설명", &본문 )` 한 줄로 등록하고, 본문은 `#if SW_DEV_COMMANDS_ENABLED` 안에 둡니다.
게임과 키트의 치트(무적, 아이템 주기)도 같은 방식으로 그 게임과 키트에 둡니다. 엔진 명령도 소유 코드 옆에 있습니다 — `timescale` 은 `Utility/GameTimeScale.cpp`, `autoplay` 는 `Utility/GameAutoplay.cpp`, `debugdraw.category` 는 `Graphics/Debug/DebugDrawQueue.cpp`, 활성 씬을 찾는 `teleport` · `tag.add` · `anim.rewind*` 는 `Scene/SceneDevCommands.cpp`(컴포넌트 모델은 씬을 모른다).
Shipping 실행 파일에 레지스트리가 없는지는 `DevCommandShippingTest`(AppTest)가 바이너리를 검사해 확인합니다.

`Console/DevConsole` 은 한 줄을 해석하고(`help`, `get`, `set`, 명령, `gv_이름 [값]`), 자동 완성과 기록을 맡습니다. 에디터 Output Log의 입력 줄과 게임 창 콘솔이 함께 씁니다.
게임 창 콘솔은 세 부분으로 나뉩니다. 판단은 `Input/DevConsoleController`, 그리기는 `Window/DevConsoleWindow.h` 의 `IDevConsoleWindow`, 입력은 `InputManager` 입니다.

콘솔을 여는 키와 편집 키는 셸 입력 맵(`Resource/engine/input/default.input.xml`)의 액션이라 데이터로 바꿀 수 있고 패드로도 쓸 수 있습니다.
`DevConsoleToggle` 은 늘 켜진 `Debug` 레이어에 있고, 닫기와 실행, 완성, 기록 이동, 지우기는 콘솔이 열려 있는 동안만 켜지는 `DevConsole` 레이어에 있습니다.
글자는 액션이 아니라 글자 입력(`InputManager::setTextInputCallback( …, InputKeyboardFocus::DevConsole )`)으로 받고, 콘솔을 여닫은 프레임의 글자는 버립니다.
열려 있는 동안 콘솔이 키보드 포커스를 가지므로 게임의 키보드 입력은 "안 눌림"이 됩니다. 패드는 포커스와 관계가 없어 게임이 계속 받습니다.

### 서버 운영 관측

`Observability/` 는 전용 서버(`Source/Server`)가 쓰는 운영 관측입니다. 지표 레지스트리(`MetricRegistry`)는 카운터, 게이지, 히스토그램을 Prometheus 텍스트 형식 0.0.4로 냅니다. 라벨은 등록할 때 고정합니다.
상태 확인(`ServiceHealthRegistry`)은 틱 박동, 검사, 드레인 상태를 봅니다. 운영 HTTP 엔드포인트(`OpsHttpEndpoint`)는 GET `/metrics`, `/healthz`, `/readyz` 를 내고, 기본 바인드 주소는 127.0.0.1입니다.
늘 켜져 있는 서버 누계라는 점에서, 개발용 프레임 구간인 `FrameProfiler` 나 동의를 받은 클라이언트 사건인 `Telemetry` 와 다릅니다.

### Utility, Console, Profiling, TileMap, Module

`Utility/` 에는 진짜 최하위 헬퍼만 둡니다. 키-값 파일, `CommandStack`, 게임이 쓰고 에디터 HUD가 읽는 디버그 값(`DebugOverlayState`)이 여기 있습니다.
XML · JSON 문서(`XmlDocument`, `JsonDocument`, `ConfigKeyDoc`)는 `Serialization/Xml` · `Serialization/Json` 에 있습니다. 데이터 XML의 "모르는 이름" 검사는 `XmlNameCheck` 하나가 합니다. 문구는 `<원소> has unknown attribute 'x'` 로 같고, 데이터 오류면 Error, 읽기를 계속하는 로더면 Warning을 고릅니다.
게임 시간 배율(`GameTimeScale`, `gv_timeScale`)은 호스트가 프레임 시간에 곱합니다. 게임의 자동 플레이 스위치 계약(`GameAutoplay`, `SW_GAME_AUTOPLAY`)도 여기 있습니다.
`Profiling/` 은 엔진 프로파일러입니다. `SW_PROFILE_SCOPE` 한 줄이 `FrameProfiler` 표와 Tracy 구간에 함께 남습니다([Profiling](Profiling/README.md)).
`Console/` 은 개발 명령 레지스트리와 콘솔 해석기, `TileMap/` 은 2D 타일맵 데이터(타일셋 에셋, 맵 XML, 격자 도우미)입니다. 셋 다 쓰는 곳이 여러 티어에 걸쳐 바닥 가까이 둡니다.
배치 규칙의 타일 표면(`Environment/Placement/PlacementTileSurface`)은 타일 값을 받아 쓰는 어댑터라 `TileMap` 과 겹치지 않습니다.

`Module/` 에는 모듈 DLL 쪽 계약만 둡니다.

- `ModuleTypeRegistry` 는 로드한 모듈의 타입과 전역 변수를 등록하고 정리합니다.
- `ModuleHandleProvider` 는 지연 로드 훅이 섀도 복사본을 묻는 진입점입니다. 훅 자체(`DelayLoadNotifyHook.cpp`)는 모듈 DLL마다 컴파일됩니다.
- `EngineAbiStamp` 는 핫 리로드의 엔진 ABI 스탬프입니다.
- `ModuleCatalog` 는 모듈 매니페스트(`<모듈>.module.json`)를 읽고 로드 순서를 정합니다. CMake와 같은 규칙으로 켜짐, 플랫폼, 구성, 의존, 버전, 순환을 봅니다.

파일 감시와 섀도 복사, 다시 로드는 호스트 라이브러리 `ModuleHost` 가 하고, 에셋 파일 감시는 에디터가 합니다([Module](Module/README.md)).

## 확장하는 법

### 새 최상위 폴더 만들기

1. 폴더가 include할 폴더 중 가장 높은 티어를 확인합니다. 그 티어 이상이 이 폴더의 티어입니다.
2. `Scripts/lint/gate/CheckEngineLayers.py` 의 `_kEngineTier` 에 한 줄을 더합니다. 표에 없는 폴더는 게이트가 실패시킵니다.
3. 이 문서의 티어 표와 `docs/02_DocumentMap.md` 에 README 줄을 더합니다.
4. 엔진 루트에는 새 파일을 두지 않습니다. 허용 목록은 `CheckEngineRootFiles` 가 가지고 있습니다.

폴더를 옮길 때는 옮길 파일의 include를 먼저 티어 표와 비교합니다. 옮긴 헤더의 예전 `.gen.cpp` 는 생성 폴더에서 지웁니다.
폴더 크기와 파일 하나짜리 폴더는 `py -3 Scripts/lint/report/RunFolderFileCount.py` 로 봅니다. 테스트는 소스 폴더 구조를 따릅니다([Test](../../Test/README.md)).

### 새 기동 단계 넣기

1. `EngineInitStepList.xxx` 에 기동 순서대로 줄 하나를 넣고, 먼저 초기화되어야 하는 단계를 의존 목록에 적습니다.
2. 두 호스트(`EngineLoop.cpp`, `Test/TestFramework/main.cpp`)에 `<단계>StartupStep` 구조체를 더합니다. 기본 구현은 아무것도 하지 않습니다.
3. `initialize` 본문은 다시 실행될 수 있어야 합니다. 백엔드 교체가 `shutdownDependentsOf( RHI )` 와 `restartStoppedSteps()` 로 같은 본문을 다시 부르기 때문입니다.
   객체가 이미 있으면 다시 쓰고, 디바이스 설정은 매번 다시 적용합니다.

### 새 엔진 서비스 넣기

1. `EngineServiceList.xxx` 에 줄 하나를 더합니다. 엔진이 만드는 서비스(`EngineCreated`)는 이것으로 끝이고 두 호스트가 함께 따라옵니다.
2. 클래스 정의는 `.cpp` 에 두고, 위치는 `Source/Engine/` 안의 알맞은 티어 폴더입니다. `Common` 에 두면 `CheckEngineLayers` 가 막습니다.
3. `HostCreated` 이면 호스트가 직접 만들어 연결해야 하고, `CheckEngineServiceBinding.py` 가 실제로 채우는지 확인합니다.

테스트에서 서비스를 바꿔 끼우는 진입점은 `test::rebindEngineServices` 하나입니다.

### 새 에셋 종류 넣기

경로를 키로 무언가를 보관하는 캐시는 `IAssetCache` 를 구현하고 `AssetManager::registerAssetCache` 로 등록합니다.
그러면 종료와 비우기, 진단이 레지스트리를 훑어 그 캐시까지 처리합니다. 종료 경로에 캐시 이름을 따로 적지 않습니다.
핫 리로드가 디바이스를 **인자로** 받는 것도 이 계약입니다. 캐시가 마지막으로 본 디바이스를 보관하면 백엔드를 바꾼 뒤 해제된 포인터가 됩니다.

게임, 에디터, 키트 모듈도 자기 에셋 종류를 등록할 수 있습니다. `AssetManager` 는 게임 모듈에도 보이는 서비스(`GameVisible`)이고, `IAssetCache.h` 는 모듈이 include해도 됩니다.

<!-- snippet: 모듈의 registerAssetCache / unregisterAssetCache 짝 — 5b U7 에서 GameDataCache 구간과 대조 -->
```cpp
// 모듈 초기화에서
getService<AssetManager>()->registerAssetCache( &_myCache );
// 모듈 종료에서 — 반드시
getService<AssetManager>()->unregisterAssetCache( &_myCache );
```

레지스트리는 포인터만 보관합니다. 해제하지 않은 채 모듈 DLL이 언로드되면 포인터와 가상 함수 테이블이 함께 사라지고, 다음 비우기가 해제된 코드로 점프합니다.
해제를 잊으면 종료할 때 **이름으로** 경고합니다. 등록할 때 이름을 복사해 두므로 이 진단은 해제된 포인터를 건드리지 않습니다.
GameFramework의 데이터 테이블 캐시(`GameDataCache`)는 게임 서비스가 바인딩되고 풀릴 때 이 짝을 부릅니다.

## 상용 엔진과의 대조

비교 기준은 폴더 이름이나 클래스 수가 아니라 **누가 누구를 아는가**입니다. 순환이 생기면 위 티어의 것을 아래 티어가 보관하고 있다는 뜻이고, 처방은 "따라 해 보기"의 네 가지 해법 중 하나입니다.

| 층 | 언리얼 | 이 저장소 | 판단 |
|---|---|---|---|
| 토대 | `Core` | `Source/Core` | 같다 |
| 리플렉션, 직렬화 | `CoreUObject` | `Reflection`, `Serialization` | 같다 |
| 설정 | `GConfig`, `UDeveloperSettings` | `Config` | 같다 |
| 에셋 | `AssetRegistry`, `FStreamableManager` | `Resource` | 같다 |
| 디바이스 | `RHI` | `Graphics/RHI` | 같다 |
| GPU 에셋 | `UMaterialInterface`, `UStaticMesh` | `Graphics/Material`, `Mesh`, `Texture` | 같다 |
| 렌더러 | `Renderer` | `Renderer` | 같다 |
| 월드 | `UWorld`, `AActor` | `Scene`, `Object` | 더 좁다 |
| 월드와 렌더러 | `FScene` 인터페이스 | `SceneManager` | 같다 |
| 기능 모듈 | `LevelSequence` | `Sequencer` | 같다 |
| 서브시스템 수명 | `FEngineLoop`, `USubsystem` | `EngineLoop`, 서비스와 기동 목록 | 같다 |
| 창, 입력 | `ApplicationCore`, `InputCore` | `Window`, `Input` | 같다 |
| 병렬 | `ParallelFor`, `TaskGraph` | `Core/Task`, `engine::runParallel` | 같다 |

판단이 "더 좁다"인 곳은 월드입니다. 언리얼의 액터는 `GWorld` 전역으로 월드를 찾지만, 여기서는 Object가 Scene을 모르고 활성 월드 전역도 없습니다.
핸들은 그 핸들을 보관한 쪽의 `GameObjectManager` 가 찾습니다(`resolveGameObject`, `resolveComponent`).

디바이스 층에서 언리얼은 `void*` 창 핸들로 `FRHIViewport` 를 만들고, 창 시스템(Slate)은 RHI 위에 있습니다. 여기도 RHI는 창을 모르고 `IRenderSurface` 만 봅니다.
렌더러 층에서 언리얼의 Engine은 `RendererInterface` 만 압니다. 여기도 렌더 패스 에셋 캐시는 `FrameRenderer` 가 소유하고, `Shader/Compile` 은 렌더러를 모릅니다.
창과 입력은 Window가 Resource(스플래시 이미지) 위에, Input이 Window 위에 있고 RHI는 둘 다 모릅니다.

### 같아서 두는 것

아래 항목은 상용 엔진과 같은 구조라 일부러 두는 것입니다. 바꾸자는 제안을 다시 하지 않습니다.

**컴포넌트가 머티리얼과 메시를 보관합니다.** Object가 Graphics의 아래 티어를 보는 방향이고, 언리얼의 `UStaticMeshComponent` 와 같습니다.
Godot처럼 "노드는 RID만 안다"로 바꾸면 모든 컴포넌트에 해석 테이블이 생기고, `shared_ptr` 로 풀어 둔 렌더 패킷 수명 문제가 다시 생깁니다.

**렌더러가 컴포넌트를 읽습니다.** `GpuSceneBuilder` 가 `PrimitiveRegistry` 와 `ComponentRegistry` 를 훑는 것은 언리얼 `FScene` 이 프리미티브 프록시를 훑는 것과 같은 방향입니다.
영속 렌더 씬(`FScene` 모델)은 두지 않습니다. 빌더가 내용이 같으면 빌드를 건너뛰고, 움직임만 있으면 제자리에서 갱신하고, 바뀐 구간만 업로드하기 때문입니다.
그래서 메시 종류가 8개에서 1024개로 늘어도 빌드 비용이 거의 같습니다(`-gv_benchMeshVariants` 로 측정합니다).

**`EngineLoop` 이 큽니다.** 언리얼의 `FEngineLoop::Init` 도 그렇습니다. 서비스 생성과 바인딩은 서비스 목록에서, 기동 순서는 기동 단계 목록에서 나옵니다.
남은 크기는 단계 본문이고, 단계마다 하는 일이 정말 달라서 더 나누지 않습니다.

**`Scene` 이 기본 머티리얼을 보관합니다.** 언리얼은 `UMaterial::GetDefaultMaterial` 이 엔진 에셋이라 월드가 모릅니다.
여기서는 씬이 인스턴스화할 때 머티리얼이 없는 메시에 채워 줍니다. 결과는 같고, 의존은 Scene에서 Graphics 아래 티어로 가는 허용 방향입니다.

**`Dialogue`, `Sequencer`, `Localization` 이 Engine 안에 있습니다.** 언리얼에서는 모듈이나 플러그인이지만 모두 Engine 위의 런타임 모듈입니다. 이 저장소는 링크 단위를 나누지 않으므로 폴더 티어로 같은 경계를 긋습니다.

**링크 단위를 나누지 않습니다**(EngineRHI, EngineReflection 같은 분할). 그래프가 DAG이므로 언제든 나눌 수 있고, 나눌 때는 티어 경계를 그대로 링크 경계로 바꾸면 됩니다.

## 함정과 주의

**폴더를 옮기기 전에 옮길 파일의 include를 티어 표와 비교하세요.** 계획한 위치가 위 티어를 include하는 파일을 받을 수 없는 경우가 많습니다.
2026년 10월의 폴더 정리에서 `Animation/`, `Console/`, `Localization/` 으로 옮기려던 파일이 그래서 `Character/PoseModifier/`, `Character/AnimNotify/` 와 한때의 `DevTools/` 로 갔습니다(`DevTools/` 는 뒤에 소유 코드 옆으로 나눠 없앴다).

**`destroy` 본문은 null을 안전하게 다뤄야 합니다.** `destroyAll` 은 초기화에 실패했거나 건너뛴 단계까지 모든 단계를 역순으로 돕니다.

**`initialize` 본문은 다시 실행될 수 있어야 합니다.** 백엔드 교체가 같은 본문을 다시 부릅니다. 디바이스 설정을 처음 한 번만 적용하면 교체 뒤에 예전 디바이스의 값이 남습니다.
`setMergeBatchesAcrossMaterials` 가 그렇게 예전 값으로 남은 적이 있습니다.

**기동 순서의 몇 가지는 진단 때문에 정해져 있습니다.** `ResourceUtil::initialize()` 는 로거 뒤에 와야 진단이 남습니다. 그다음이 설정, 그다음이 `AssetManager::initialize()` 와 `mountContent` 입니다.
종료할 때는 `_rhi->shutdown()` 이 `AssetManager::shutdown` 보다 먼저입니다. 오디오는 TaskManager보다 먼저 종료하고, `_voiceMutex` 로 `_bInitialized` 를 먼저 내립니다.
테스트 호스트도 앱과 같은 순서(리플렉션 등록, 설정, AssetManager)를 지킵니다.

**프로세스 정적 캐시는 엔진 종료 단계가 비웁니다.** `ShaderReflectionLibrary` 의 매니페스트 같은 캐시를 비우지 않으면 기동 뒤에 채운 양이 종료 누수 검사에 남습니다. 백엔드 교체 뒤에는 약 1.1MB였습니다.
진단하려면 MemoryProfiler 세부 추적을 켜고 `destroyAll` 뒤에 `getTopCallStacks( LiveBytes )` 를 봅니다. 교체 전 백엔드의 매니페스트는 종료까지 남습니다. 최대 4개라 그대로 둡니다.

**정적 컨테이너는 `clear()` 가 아니라 빈 객체를 대입해서 비웁니다.** 이름 풀, 트랜스폼 페이지, 경로 캐시 같은 프로세스 정적 저장소는 `EngineBootstrap::shutdown` 이 놓습니다.
`clear()` 는 버킷과 밀집 배열, 용량을 남기므로 타입을 적은 빈 객체를 대입합니다. `= {}` 로 쓰면 initializer_list 대입이 골라져 용량이 남습니다.
이름 풀 블록은 넣는 쪽 태그가 아니라 `EngineMisc` 로 셉니다. 종료 보고가 0인지는 `AppSmokeTest.ShutdownReturnsEveryTagToTheBaseline` 이 확인합니다.

**모듈 인스턴스를 해제할 때는 타입을 정리한 뒤에 서비스를 끊습니다.** 에디터와 게임 모두 같은 순서입니다(`ModuleHostInternal::destroyInstance`).

**핫 리로드를 넘어 살아야 하는 상태는 Engine이나 App에 둡니다.** 모듈 안의 정적 변수는 DLL이 교체되면 사라집니다. 그래서 `CommandStack` 은 `EngineLoop` 이 소유합니다.

**엔진 창은 `WindowResizeEvent` 를 발행하지 않습니다.** 크기 변경은 델리게이트로 알립니다.
`Win32Window` 의 `WM_SIZE` 처리 중에는 OS가 같은 스레드에서 창 프로시저를 다시 부를 수 있습니다. 그래서 `_bResizing` 이 중첩된 `onResize` 를 막고, 리사이즈 경로를 고칠 때도 이 가드를 지나게 둡니다.
다른 스레드(렌더 스레드)와의 경합은 `RenderThread::waitIdle()` 이 막습니다.

**서비스 테스트는 줄에 적은 `visibility` 값 자체의 오류를 잡지 못합니다.** `EngineServiceTest` 의 기댓값도 같은 X-macro에서 만들기 때문입니다. 줄의 `visibility` 값은 사람이 검토합니다.

**`Games/` 와 `GameFramework/` 는 `Engine/Common/EngineServices.h` 를 include하지 않습니다.** 게임 쪽은 `GameFramework/Base/Foundation/Framework/GameService.h` 의 `game::getService<T>()` 만 씁니다.

## 더 볼 곳

- [ARCHITECTURE](../../ARCHITECTURE.md) — 타깃 그래프와 C-ABI 경계
- [Core](../Core/README.md) — 토대 라이브러리와 [태스크 시스템](../Core/Task/README.md)
- [03 핫 리로드와 C-ABI](../../docs/03_LiveReload_and_ABI.md)

폴더마다의 README는 다음과 같습니다.

| 폴더 | 내용 |
|---|---|
| [Object](Object/README.md) | 게임 오브젝트, 컴포넌트, 프리팹 |
| [Scene](Scene/README.md) | 씬 파일, 로드, 전환, 쿠킹 |
| [Reflection](Reflection/README.md) | 리플렉션 매크로와 타입 레지스트리 |
| [Serialization](Serialization/README.md) | 바이너리, JSON, XML 직렬화와 델타 직렬화(`ObjectDiffSerializer`) |
| [Graphics](Graphics/README.md) | RHI, 셰이더, 머티리얼, 메시, 텍스처 |
| [Renderer](Renderer/README.md) | 파이프라인, 렌더 그래프, GPUScene, 프레임 실행 |
| [Input](Input/README.md) | 입력 장치와 액션 맵 |
| [UI](UI/README.md) | 런타임 UI |
| [Text](Text/README.md) | 글꼴과 글자 렌더링 데이터 |
| [Animation](Animation/README.md) | 스켈레톤과 애니메이션 |
| [Character](Character/README.md) | 캐릭터 형상, 소켓, 피격 |
| [Physics](Physics/README.md) | 물리 |
| [Navigation](Navigation/README.md) | 내비메시와 군중 |
| [Spatial](Spatial/README.md) | 공간 분할 |
| [Environment](Environment/README.md) | 지형, 식생, 물 |
| [Destruction](Destruction/README.md) | 파괴 가능 메시 |
| [Audio](Audio/README.md) | 오디오 |
| [Localization](Localization/README.md) | 문자열 테이블과 문화권 |
| [UserSettings](UserSettings/README.md) | 플레이어 옵션 |
| [Automation](Automation/README.md) | 자동화 시나리오 |
| [Telemetry](Telemetry/README.md) | 텔레메트리와 크래시 보고 |
| [Module](Module/README.md) | 모듈 DLL 계약 |
| [Profiling](Profiling/README.md) | 프로파일러 |
