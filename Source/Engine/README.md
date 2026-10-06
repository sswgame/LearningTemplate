# Engine (핵심 엔진 모듈)

오브젝트(Object), 그래픽스 렌더링(RHI), 리플렉션(Reflection), 씬(Scene) 관리 등 **진짜 게임 엔진의 코어 로직**이 모여있는 곳입니다.

Foundation(로그/파일/문자열 등)은 `Source/Core`의 `Core_objects`에서 한 번만 컴파일되며, Engine이 동일 OBJECT를 링크해 Dev에서 `Engine.dll`로 export합니다.

## 내부 티어 (허용 의존 방향)

물리적으로 SHARED를 쪼개지 않은 상태이므로, 폴더 간 include 방향으로만 순환을 가둡니다.
아래 표는 **include 그래프를 위상 정렬해 얻은 것**입니다. 손으로 고른 순서가 아니므로, 코드가
바뀌면 표도 다시 계산해야 합니다(`Scripts/lint/report/RunEngineLayerGraph.py`).

| 티어 | 폴더 | 뜻 |
|---|---|---|
| 0 | `Common` · `Compression` · `Network` · `Observability` | 토대. Engine 의 어느 것도 참조하지 않는다. 외부 라이브러리로 Core 의 창구를 구현하는 자리(압축 코덱 · 네트워크 보안)와 Core 만 보는 서버 운영 관측. |
| 1 | `Reflection` · `Utility` | 리플렉션과, 토대 위의 잎 헬퍼. |
| 2 | `Animation` · `Localization` · `Serialization` | 리플렉션 위에 올라가는 직렬화와 에셋형 잎. |
| 3 | `Audio` · `Config` · `Dialogue` · `Physics` | 설정 — 리플렉션·직렬화로 읽힌다. 물리(설정 표 · 물리 에셋 · 셰이프 서술자가 리플렉션 데이터)와 오디오(믹서 그래프 · 이벤트 · 음악 데이터)도 여기다. |
| 4 | `Resource` · `Spatial` · `Navigation` | 에셋 데이터베이스·팩·캐시 등록부. 위의 모두가 읽는다. 공간 분할은 물리의 `AABB` 위에 선다. 내비메시(Recast 백엔드 · 베이크 입력 · 설정 표)는 물리의 셰이프 서술자를 읽어 베이크하고, 씬의 내비게이션 · 컴포넌트(6)가 쓴다. |
| 5 | `Graphics`(Renderer 제외) · `Window` · `Text` | RHI · 셰이더 · 머티리얼 · 메시 · 텍스처 — **디바이스와 GPU 에셋**. 창은 표면(`Common/IRenderSurface`)으로만 RHI 에 보인다. 글자(`Text` — 글꼴 · 글리프 · SDF 아틀라스(CPU 바이트) · 셰이핑 · 줄 바꿈)는 글꼴 파일(Resource)을 읽고 GPU 를 모른다 — 아틀라스 업로드는 렌더러가 한다. |
| 6 | `Input` · `Object` | 컴포넌트 모델. 컴포넌트가 머티리얼·메시(5)를 든다 — 언리얼의 `UStaticMeshComponent` 가 `UMaterialInterface` 를 드는 것과 같은 자리. |
| 7 | `Scene` · `Sequencer` · `Character` · `UserSettings` · `Environment` · `DevTools` | 월드(씬·씬 매니저)와, 오브젝트 위에서 도는 기능 모듈(시퀀서 · 캐릭터 외형의 소켓 · 피팅 · 소켓 부착 컴포넌트 · 지형 · 식생 · 물). **월드는 액터를 알고 액터는 월드를 모른다.** 플레이어 옵션(`UserSettings`)은 입력 · 오디오 · 언어 · 창 값을 넣는 자리라 그 위다. 개발 도구(`DevTools` — 게임 창 개발 콘솔의 판단 · 엔진 개발 명령 · 로컬라이제이션 수집 명령)는 씬(7) · 입력(6) · 창 · 디버그 그리기(5) 위에 선다. |
| 8 | `Graphics/Renderer` · `Module` · `Telemetry` · `Destruction` · `Automation` · `UI` | **그리는 쪽**(FrameRenderer · RenderGraph · GpuScene · RenderThread · Cook)과 핫리로드. 씬·컴포넌트를 읽어 그린다 — 언리얼의 Renderer 가 Engine 을 보는 방향. 텔레메트리 · 크래시 보고는 동의를 사용자 설정(7)에서 읽는다. 파괴(`Destruction`)는 캐릭터 형상의 자르기 도구(7) 위에 서는 기능 모듈이라 여기다(렌더러는 모른다). 런타임 UI(`UI`)는 입력(6) · 글자(5) · 사용자 설정(7)을 쓰고, 렌더러와는 서로 include 하지 않는다 — 사이의 값은 그리기 목록뿐이다(언리얼 Slate ↔ SlateRHIRenderer). |
| 9 | `EngineLoop` 등 루트 파일 | 전부를 엮는 자리. |

강결합 묶음은 없습니다 — 이 표는 DAG 이고 `CheckEngineLayers` 가 그대로 강제합니다.
다시 계산하려면 `py -3 Scripts/lint/report/RunEngineLayerGraph.py` — 게이트와 같은 규칙(prelude·배선 예외,
`Graphics/Renderer` 분리)으로 묶음과 티어를 찍습니다. `Graphics` 만 폴더보다 잘게 봅니다: `Graphics/Renderer`
는 위(8), 나머지 `Graphics` 는 아래(5) — 상용 엔진의 RHI/RenderCore ↔ Renderer 사이의 선과 같습니다.
상용 엔진과 어디가 같고 어디가 다른지는 아래 [상용 엔진과의 대조](#상용-엔진과의-대조).

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
- `Games/` · `GameFramework/` → `Engine/Common/EngineServices.h` 금지 — 게임 쪽은 `GameFramework/Base/Framework/GameService.h` 의 `game::` 만 씁니다.

## 주요 시스템 디렉터리 구조
- **Object/**: GameObject · Component · Prefab. 틱/구조 동결·사용법은 [Object/README.md](Object/README.md)
- **Scene/**: Scene · SceneManager · SceneDocument · SceneCooker · ObjectUndoUtil ([Scene/README.md](Scene/README.md))
- **Character/**: 캐릭터 외형의 형상 쪽 — `Fit/`(중립 형상 · 체형 · 장비 피팅 `FitSolver` · 병합 · 자르기 · 표면 상태) · `Socket/`(소켓 에셋 · 해석된 소켓 표 ·
  `SocketBindingComponent`) · `Hit/`(맞힘 · 래그돌 · 절단 런타임) · `Pose/`(레퍼런스 포즈 덮어쓰기 · 후처리 리그) · `AnimNotify/`(알림 디스패치), 루트는 공용 데이터 읽기
  ([Character/README.md](Character/README.md)). 모션 워핑 · 이동 보정 컴포넌트는 `Object/Animation/`
- **Destruction/**: 파괴 가능 메시 — 보로노이 파쇄(`.fracture`) · 묶음 계층 · 연결 그래프 · 구조 지지 · 피해 · 조각 컴포넌트 ([Destruction/README.md](Destruction/README.md))
- **Environment/**: 지형(높이장 · 청크 LOD · 스플랫 레이어) · 식생(규칙 배치 · GPU 인스턴스 · 바람) · 물(거스트너 · 수면 질의) · 2D/3D 공용 배치 규칙 ([Environment/README.md](Environment/README.md))
- **Navigation/**: 3D 내비메시 — `INavMesh` · `INavCrowd` 인터페이스 뒤의 Recast & Detour(타일 베이크 · 경로 · 레이캐스트 · 군중), 설정 표, 쿠킹본(`.navmesh`). 씬 쪽(`SceneNavigation` · 에이전트 · 표면 · 장애물 컴포넌트)은 Object 에 있다 ([Navigation/README.md](Navigation/README.md))
- **Spatial/**: 2D/3D 공간 분할 가속 구조체(BVHTree3D · SpatialHashGrid2D · SpatialQuadTree · SpatialOctree) ([Spatial/README.md](Spatial/README.md))
- **Reflection/**: 매크로 · TypeRegistry · Builtins. [Reflection/README.md](Reflection/README.md) · 생성기 [ReflectionParser](../../Tools/ReflectionParser/README.md)
- **Graphics/**: RHI · Material · Shader · FrameRenderer. [Graphics/README.md](Graphics/README.md)
- **Input/**: InputManager · InputMap · 장치(Keyboard/Mouse/Gamepad) 추상화. [Input/README.md](Input/README.md)
- **Localization/**: 문자열 표(원문 · 문화권 번역 · 낡은 번역 판정) · 문화권 데이터 · ICU 메시지 포맷(복수형 · 고르기 · 숫자 · 날짜) · 의사 로컬라이제이션 · `SW_LOCTEXT`. [Localization/README.md](Localization/README.md)
- **Text/**: 런타임 글자 — 글꼴 래스터라이저 계약(`IFontRasterizer`, 구현은 `Text/FreeType/` 하나 — FreeType 헤더는 거기서만 include 한다,
  `CheckThirdPartyIsolation.py`) · 단일 채널 SDF 글리프 · 글꼴 서비스(`FontSystem` — 카탈로그 · 시스템 글꼴 · 문화권 대체 사슬). GPU 를 모르는 티어 5 다. [Text/README.md](Text/README.md)
- **UI/**: 런타임(게임) UI — 유지형 위젯 트리(`Widget` · `PanelWidget` · `WidgetTree`, 무효화 이유를 나눠 알린다) · 서비스 `UiSystem`. 티어 8. [UI/README.md](UI/README.md)
- **DevTools/**: 개발 도구 — 게임 창 콘솔의 판단(`DevConsoleController`) · 엔진 개발 명령(`EngineDevCommands.cpp`) · 로컬라이제이션 수집 · 가져오기 ·
  내보내기 명령의 본문(`LocalizationTools`, `EngineLoop` 이 명령줄로 부른다). 씬 · 오브젝트 · 대화 에셋을 함께 보므로 티어 7 이다.
- **UserSettings/**: 플레이어 옵션 메뉴의 백엔드 — 데이터 스키마 · 품질 프리셋 · 사용자 파일 · 적용/되돌리기/확인 카운트다운 · 메뉴 바인딩 API. [UserSettings/README.md](UserSettings/README.md)
- **Automation/**: 자동화 시나리오(`-scenario=<파일>` — 프레임별 가상 입력 · 탐침 단언 · 스크린샷 지표, 결과는 종료 코드)와 탐침 · 단계 등록표. [Automation/README.md](Automation/README.md)
- **Telemetry/**: 텔레메트리(동의 · 스키마 · 표본 · 묶음 · JSON lines 스풀 · 회전 · 올리기 · 장면별 프레임 시간 요약)와 크래시 보고(다음 실행의 묶음 ·
  동의 local/ask/send · 보고 프로세스 · multipart 업로드). 바깥으로는 `IHttpClient` 창구로만 나가고 기본 창구는 보내지 않는다. [Telemetry/README.md](Telemetry/README.md)
- **Observability/**: 서버 운영 관측 — 지표 등록부(`MetricRegistry` — 카운터 · 게이지 · 히스토그램, 라벨은 등록 때 고정, Prometheus 텍스트 0.0.4),
  상태 확인(`ServiceHealthRegistry` — 틱 박동 · 검사 · 비우는 중), 운영 HTTP 끝점(`OpsHttpEndpoint` — GET `/metrics` · `/healthz` · `/readyz`, 기본 바인드 127.0.0.1). 늘 켜진
  서버 누계라 `FrameProfiler`(개발 프레임 구간) · `Telemetry`(동의 받은 클라이언트 사건)와 다른 자리다. 전용 서버 실행 파일(`Source/Server`)이 하나 들고 넘긴다.
- **Resource/**: AssetDatabase · AssetManager · ResourceUtil · ResourcePackManager (VFS .pack) · AssetStreamingQueue · AssetLoadProfiler
  (팩 리더는 위치 지정 읽기라 여러 스레드가 잠금 없이 읽고 — 매니저는 리더를 찾는 동안만 잠근다 — `readFileAsync` 로 `AsyncFileIo` 에 구간 읽기를 걸어 해제 · CRC 를 태스크 워커에서 한다.
  스트리밍 큐의 바이트 요청은 `ResourceUtil::readBinaryResourceAsync` 로 간다)
  - **스트리밍 큐(`AssetStreamingQueue`)는 호스트 전용 서비스(`HostOnly`)다** — `EngineLoop` 의 기동 단계가 만들고 내리며 게임 모듈에는 보이지 않는다.
    완료 콜백은 엔진 루프가 메인 스레드에서 프레임마다 내보낸다(`update()`). 값으로 받으려면 `requestAssetFuture`.
  - **에셋 로드 시간은 `AssetLoadScope` 로 잰다**(`AssetLoadProfiler` — 종류별 수 · 바이트 · IO / 해석 / GPU 올리기 시간 · 가장 긴 로드 · 비동기(워커 스레드)
    · 실패, 가장 느린 16 개, 프레임 프로파일러 구간 `Asset.<종류>.<단계>` · `Asset.Bytes`). 텍스처 · 메시 · 머티리얼 · 프리팹 · 씬 로더가 연다 — 새 로더도
    `AssetLoadScope scope( "Kind", path )` → `beginPhase` → `setBytes` → `setSucceeded`(안 부르면 실패). `-gv_assetLoadProfile=0` 은 끄고,
    `-gv_assetLoadReport=1` 이면 엔진 종료 때 표를 남긴다(언리얼 Insights LoadTimeProfiler · 유니티 Loading 마커의 자리)
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
    포인터를 건드리지 않는다. GameFramework 의 데이터 표 캐시(`GameDataCache` — 상호작용 · 원소 규칙 표)는 게임 서비스가 묶이고 풀릴 때 이 짝을 부른다.
  - **"같은 키면 같은 객체, 마지막 사용자가 놓으면 사라짐" 표는 `WeakInternTable` 하나다.** 경로로 읽는 에셋(`SharedAssetTable` — 스켈레톤 · 클립 · 리그 ·
    스프라이트 클립 · 캐릭터 데이터)도, 코드로 짓는 값(`WeakInternCache` — 내장 도형 `MeshUtil::acquirePrimitive` · 9-슬라이스 메시 · 스프라이트 텍스처 인스턴스)도
    이것 위에 선다. 함수 정적 표를 등록부 밖에 따로 두지 않는다 — `WeakInternCache` 는 `IAssetCache` 라 `AssetManager` 가 생성자에서 내장 캐시로 올리고(목록
    `_listBuiltInAssetCache` 하나), 종료의 비우기가 그 표의 약한 칸까지 지운다.
  - **티어 때문에 Resource 에 사는 것 셋**: `SpriteClipCache` · `AnimationAssetCache`(스켈레톤 · 애니메이션 클립)는 `IAssetCache` 를 구현하므로 Animation(티어 2)이 아니라 Resource(티어 4)에,
    `LocalizationReloadCache`(로컬라이제이션 파일의 핫 리로드 창구 — 글은 `LocalizationManager` 가 갖고 `clear()` 는 아무것도 지우지 않는다)도 같은 이유로 여기에,
    `PackCompressionUtil` 은 팩 타입(`ResourcePackTypes.h`)을 쓰므로 Compression(티어 0)이 아니라 Resource 에 둔다.
- **Serialization/**: 직렬화 (BinarySerializer · JsonSerializer · XmlSerializer · Archive · 바뀐 값만 델타로 쓰는 ObjectDiffSerializer)
- **Module/**: 모듈 DLL 쪽 계약만 둔다 — `ModuleTypeRegistry`(로드한 모듈의 타입 · 전역 변수 등록과 정리) · `ModuleHandleProvider`(지연 로드 훅이
  섀도 복사본을 묻는 창구) · `DelayLoadNotifyHook.cpp`(모듈 DLL 마다 컴파일되는 지연 로드 훅) · `EngineAbiStamp`(핫 리로드의 엔진 ABI 도장).
  감시 · 섀도 복사 · 다시 로드(`LiveReloadManager`)는 App 의 `App/Module`, 에셋 파일 감시(`FileWatchDispatcher`)는 에디터의
  `Editor/Common/Workspace` 에 있다. 모듈 이미지 수명 계약의 Core 쪽(`IModuleUnloadListener`)은 `Core/Module` 이다.
- **Module/** 의 `ModuleCatalog` 는 모듈 매니페스트(`<모듈>.module.json`)를 읽고 켜짐 · 플랫폼 · 구성 · 의존 · 버전 · 순환을 보고 적재 순서를 정한다(App 이 쓴다 — CMake 와 같은 규칙).
- **Utility/**: `KeyValueFile`, Json, Xml(데이터 XML 의 "모르는 이름" 검사는 `XmlNameCheck` 하나 — 판정 · 문구 `<원소> has unknown attribute 'x'` 가 같고, 데이터 오류면 Error · 읽기를 잇는 로더면 Warning 을 고른다), CommandStack, `DebugOverlayState`(게임이 쓰고 에디터 HUD 가 읽는 디버그 값), `GameTimeScale`(게임 시간 배율 `gv_timeScale` — 호스트의 프레임 시간이
  곱한다), `GameAutoplay`(게임의 자동 플레이 스위치 계약 — `SW_GAME_AUTOPLAY`, `Source/Games/README.md`), Console(개발 콘솔 — 아래), TileMap(타일셋 · 규칙 타일 해석 · 충돌 사각형 병합 · 외곽선 · 이동 비용) — 진짜 최하위
  헬퍼만 둡니다.
  `Profiling/` 은 엔진 프로파일러다 — 프로세스 안의 표(`FrameProfiler` · `FrameProfileSession`)와 두 번째 출력(외부 타임라인 뷰어 — Tracy): `SW_PROFILE_SCOPE` 한 줄이 `FrameProfiler` 표와 Tracy 구간에 함께
  남는다(`Utility/Profiling/README.md`). Tracy 헤더는 `Profiling/Tracy/` 에서만 include 한다(`CheckThirdPartyIsolation.py`).
  `Profiling/MemoryBudgetMonitor` 는 메모리 태그 예산(`Config/Engine/MemoryBudget.json`, 모르는 태그 · 키는 오류) · 프레임 끝 예산 검사 · `-gv_memoryReport` 표 ·
  FrameProfiler 카운터를 맡습니다(`EngineLoop::endFrame`, 표는 `-gv_profileFrames` 보고와 같은 함수).
- **개발 콘솔 · 개발 명령(Shipping 에는 없다 — `SW_DEV_COMMANDS_ENABLED`)**: `Utility/Console/DevCommandRegistry` 가 명령 등록부(Engine 하나, 모듈을 내리면
  그 모듈의 명령이 빠진다)이고, 명령은 자기 .cpp 에 `SW_DEV_COMMAND( 변수, "이름", "사용법", "설명", &본문 )` 한 줄로 등록합니다 — 본문은
  `#if SW_DEV_COMMANDS_ENABLED` 안에 둡니다. 게임 · 키트의 치트(무적 · 아이템 주기 …)도 그렇게 그 게임 · 키트에 둡니다. `Utility/Console/DevConsole`
  은 한 줄 해석(`help` · `get`/`set` · 명령 · `gv_이름 [값]`) · 자동완성 · 기록이고, 에디터 Output Log 입력 줄과 게임 창 콘솔이 같이 씁니다.
  게임 창 콘솔은 셋으로 나뉩니다 — 판단 `DevTools/DevConsoleController`(티어 7), 그리기 `Window/DevConsoleWindow.h` 의 `IDevConsoleWindow`
  (`Window/Windows/Win32DevConsoleWindow` · `Window/Linux/X11DevConsoleWindow`, 입력을 모른다), 입력은 `InputManager` 하나입니다.
  여는 키 · 편집 키는 셸 InputMap(`Resource/engine/input/default.input.xml`)의 액션이라 데이터로 바꾸고 패드로도 씁니다
  (`DevConsoleToggle` 은 늘 켜진 `Debug` 레이어, 닫기 · 실행 · 완성 · 기록 ↑↓ · 지우기는 열려 있는 동안만 켜는 `DevConsole` 레이어). 글자는 액션이 아니라
  글자 입력(`InputManager::setTextInputCallback( …, InputKeyboardFocus::DevConsole )`)이고, 여닫는 액션이 발화한 프레임의 글자는 버립니다.
  열려 있는 동안 콘솔이 `InputManager` 키보드 포커스를 쥐어 게임 쪽 키 조회와 통합 InputMap 의 키보드 바인딩이 "안 눌림" 입니다(셸 맵만 포커스를 무시합니다).
  패드는 포커스 밖이라 콘솔이 열린 동안에도 게임이 받습니다. 엔진 명령은
  `DevTools/EngineDevCommands.cpp`(`timescale` · `teleport` · `debugdraw.category`). Shipping 실행 파일에 등록부가 없는지는
  `DevCommandShippingTest`(AppTest)가 바이너리를 훑어 봅니다.
- **루트 파일 — 기동 · 종료**(이 밖의 파일은 루트에 두지 않는다 — `CheckEngineRootFiles` 허용 목록. 폴더 크기 · 파일 하나짜리 폴더는 `Scripts/lint/report/RunFolderFileCount.py`):
  - `EngineInitStepList.xxx`: 기동 단계의 등록표(X-macro). 줄 순서가 초기화 순서이고, 줄마다 단계 이름 · 그 초기화에 거는 메모리 태그 · 도는 호스트(대상 — `All` · `Client` · `Server`, 전용 서버는 `Client` 줄을 건너뛴다) ·
    먼저 서야 하는 단계 `{ A, B }` 를 적습니다. 의존이 자기보다 아래 줄이거나 오타면 컴파일 오류이고(`EngineInitSequence.cpp` 의 static_assert),
    의존만으로 위상 정렬한 순서가 줄 순서와 같아야 합니다(`EngineInitSequenceTest.TableIsWrittenInStartupOrder`).
    `ModuleTypes` 단계(호스트가 타입 공급자 — GF · 킷 · 게임 모듈 — 를 올려 등록을 끝냄)가 서야 씬을 읽고 쿠킹합니다(`Headless` 가 그 뒤).
  - `EngineInitSequence`: 표를 읽어 초기화(`initializeAll`) → 초기화한 단계만 역순 종료(`shutdownAll`) → **모든 단계**를 역순 해제(`destroyAll`).
    단계 본문은 호스트가 줄마다 구조체 하나 `<단계>StartupStep`(`initialize` · `shutdown` · `destroy`, `EngineInitStepDefaults` 상속)로 줍니다 —
    `EngineLoop.cpp` 와 시험 하네스(`Test/TestFramework/main.cpp`)가 같은 표를 씁니다. 구조체가 빠지면 컴파일 오류입니다.
  - `EngineBootstrap`: 표 **앞**의 고정 부트스트랩(이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 교착 감지기 · 메모리 프로파일러 · 명령줄 · 전역 변수)과
    표 **뒤**의 끝 정리. 서비스 표를 언제 끊는지 · 로거를 언제 내리는지는 여기 한 곳이 정합니다. 두 호스트가 함께 씁니다.
  - `EngineServiceCollection`: 호스트가 소유하는 서비스 저장소(`RuntimeAPI/Service/EngineServiceList.xxx` 의 `EngineCreated` 줄에서 생성, `destroyAll` 은 목록의 역순).
  - `EngineLoop`(메인 루프 · 단계 구조체) · `EngineMinimal.h`(prelude)
- **메모리 태그**(`Core/Memory/MemoryTag.h`, UE LLM 식): 할당을 용도로 나눕니다. 거는 자리는 하위 시스템의 **진입점**뿐입니다 — 기동 단계(표의 태그 칸),
  에셋 종류별 로드 · 씬 로드 · 렌더러 · 렌더 스레드 · 모듈 호출에서 `SW_MEMORY_SCOPE( Tag )`. 태스크와 엔진이 띄우는 스레드는 띄운 쪽의 태그를 잇습니다.
  태그를 읽는 것은 Debug 의 `MemoryProfiler` 뿐이라 다른 구성에서 비용은 0 입니다. 보고는 `-gv_profileFrames` 프로파일 보고 · 에디터 프로파일러 패널.
- **Task 시스템**: Core의 [Task/README.md](../Core/Task/README.md) (`TaskManager` / `TaskHandle`)

## 상용 엔진과의 대조

기준은 하나 — **의존 방향이 상용 엔진과 같은가**(폴더 이름 · 클래스 수가 아니라 "누가 누구를 아는가")입니다.
묶음(순환)이 생기면 위층 것을 아래층이 들고 있는 것이고, 처방은 위 "지금 자리" 의 모양 중 하나입니다 — 푸는 쪽이 든 객체로 풀기 ·
소유를 위로 올리기 · 인터페이스를 토대에 두고 위가 구현하기 · 서비스 표로 내주기.

| 층 | 언리얼 | Unity / Godot | 이 저장소 | 판단 |
|---|---|---|---|---|
| 토대 | `Core` (컨테이너·문자열·파일·스레드·메모리) | Godot `core/` | `Source/Core` (STATIC, OBJECT 로 Engine 에 흡수) | **같다.** Core 는 Engine 을 모른다. 리플렉션 파서도 Core 만 링크한다. |
| 리플렉션·직렬화 | `CoreUObject` (UClass · UProperty · 아카이브) | Godot `ClassDB` · Unity C# 리플렉션 | `Engine/Reflection` + `Engine/Serialization` (코드젠 `.gen.cpp`) | **같다.** 인코딩은 Serialization 이 갖고 선언은 Reflection 에 남는다. |
| 설정 | `GConfig` · `UDeveloperSettings` | `ProjectSettings` | `Engine/Config` (EngineConfig · GameConfig · EngineDefaultAssets) | **같다.** 리플렉션 위, 코어 아래. |
| 에셋 | `AssetRegistry` · `FStreamableManager` · 패키지 | `Resources` · `ResourceLoader` | `Engine/Resource` (AssetDatabase · 팩 · `IAssetCache` 등록부 · 스트리밍 큐) | **같다.** 종류를 늘리는 자리가 인터페이스 하나다. |
| 디바이스 | `RHI` — `void*` 창 핸들로 `FRHIViewport` 를 만든다. 창 시스템(Slate)은 RHI 위 | Godot `RenderingDevice` | `Graphics/RHI` (4 백엔드) | **같다.** RHI 는 창을 모른다 — `Common/IRenderSurface` 를 `IWindow` 가 구현하고 `EngineLoop` 이 넘긴다. |
| GPU 에셋 | `Engine` 의 `UMaterialInterface` · `UStaticMesh` — 컴포넌트가 든다 | Unity `Material` · `Mesh` | `Graphics/Material` · `Mesh` · `Texture` · `Shader` | **같다.** 컴포넌트가 머티리얼·메시를 드는 것은 상용 엔진의 모양이다 — 이 엣지는 결함이 아니다. |
| 렌더러 | `Renderer` — `Engine` 을 보고(프록시·씬) 그린다. `Engine` 은 `RendererInterface` 만 안다 | Unity SRP · Godot `RenderingServer` | `Graphics/Renderer` (FrameRenderer · RenderGraph · GpuScene · RenderThread) | **같다.** (1) 렌더 패스 *에셋* 캐시(`RenderPipelineAssetCache`)는 `FrameRenderer` 가 소유한다 — RHI 는 Renderer 를 모른다. (2) "무엇을 쿠킹할지" 의 정책은 `Renderer/Cook/ShaderCookDriver` 에 있고 `Shader/Compile` 은 렌더러를 모른다. |
| 월드 | `UWorld` → `AActor` → `UActorComponent`. 액터는 `GWorld` 전역으로 월드를 찾는다 | Godot `SceneTree` → `Node` | `Scene` → `Object`(GameObject · Component) | **더 좁다.** Object 는 Scene 을 모른다. 활성 월드 전역(`GWorld`)도 없다 — 핸들은 그것을 푸는 쪽이 든 `GameObjectManager` 가 푼다(`resolveGameObject` · `resolveComponent`). |
| 월드 ↔ 렌더러 | `UWorld` 는 `FScene`(렌더 씬 인터페이스)만 안다. 렌더러 본체를 모른다 | Godot 노드는 `RenderingServer` 에 RID 로만 말한다 | `SceneManager` | **같다.** 씬은 렌더러를 모른다 — 렌더러는 호스트가 내주는 선택 서비스(`EngineServiceList.xxx`)다. |
| 기능 모듈 | `LevelSequence` · `MovieScene` 은 `Engine` 위의 모듈 — 액터를 알고, 액터는 모른다 | Godot `AnimationPlayer` 는 `scene/` 안의 노드 | `Sequencer` | **같다.** `SequencePlayerComponent` 는 `Sequencer/` 에 있다 — Sequencer 가 Object 를 알고, Object 는 Sequencer 를 모른다. |
| 서브시스템 수명 | `FEngineLoop` + `UEngineSubsystem`(자동 수집) | `PlayerLoop` | `EngineLoop` + `EngineServiceList.xxx`(X-macro 등록표, 낱말 칸 `Required`/`Optional` · `GameVisible`/`HostOnly` · `EngineCreated`/`HostCreated` 로 생성·바인딩 생성) + `EngineInitStepList.xxx`(기동 단계와 의존) | **같은 모양.** 언리얼은 `USubsystem::Initialize` 안에서 `FSubsystemCollectionBase::InitializeDependency` 로 먼저 설 서브시스템을 적고 컬렉션이 그 순서로 초기화 · 역순으로 `Deinitialize` 한다. 여기도 단계마다 의존을 표에 식별자 목록으로 적고(오타 · 아래 줄 의존은 컴파일 오류) `EngineInitSequence` 가 그 순서로 초기화 · 역순 종료한다. 호스트(`EngineLoop` · 시험 하네스, 공통 부트스트랩은 `EngineBootstrap`)는 단계마다 구조체 하나(`initialize` · `shutdown` · `destroy`)로 본문만 준다. 객체 해제도 모든 종료 뒤에 같은 역순으로 모든 단계를 돈다 — `AssetManager::shutdown` 은 Resource 단계의 해제에 있어, 에셋을 드는 뒤 단계들의 소멸자가 먼저 돈다. |
| 창·입력 | `ApplicationCore` (창) · `InputCore` — RHI 위 | Godot `DisplayServer` | `Window` · `Input` | **같다.** Window 는 Resource(스플래시 그림) 위, Input 은 Window 위. RHI 는 둘 다 모른다. |
| 병렬 | `ParallelFor` · `TaskGraph` | Unity Jobs | `Core/Task` + `engine::runParallel` | **같다.** 병렬 시스템의 모양이 하나다(트랜스폼 계층이 첫 예). |

### 같아서 두는 것 — 다시 제안하지 말 것

- **컴포넌트가 머티리얼·메시를 든다 (Object → Graphics 저층).** 언리얼의 `UStaticMeshComponent` 가 `UStaticMesh` ·
  `UMaterialInterface` 를 드는 것과 같다. Godot 식 "노드는 RID 만 안다" 로 바꾸면 모든 컴포넌트에 해석 표가 생기고,
  `shared_ptr` 로 풀어 둔 렌더 패킷 수명 문제가 되살아난다.
- **렌더러가 컴포넌트를 읽는다 (Graphics/Renderer → Object · Scene).** `GpuSceneBuilder` 가 `PrimitiveRegistry` ·
  `ComponentRegistry`(빛) 를 훑는 것은 언리얼 `FScene` 이 프리미티브 프록시를 훑는 것과 같은 방향이다. 영속 렌더 씬(`FScene`
  모델)은 두지 않는다 — 빌더가 내용이 그대로면 빌드를 건너뛰고, 움직임만 있으면 제자리 갱신하고, 바뀐 구간만 올리므로
  메시 종류가 8 → 1024 로 늘어도 빌드 비용이 평평하다(`-gv_benchMeshVariants` 로 잰다). 얻을 것이 남아 있지 않다.
- **`EngineLoop` 이 크다.** `FEngineLoop::Init` 도 그렇다. 서비스 생성·바인딩은 표(`EngineServiceList.xxx`)에서, 초기화 · 종료 순서는
  기동 단계 표(`EngineInitStepList.xxx`)의 의존 칸에서 나온다. 남은 크기는 단계 본문이고, 본문은 단계마다 하는 일이 정말 달라 나누지 않는다.
  표는 기동 순서대로 적고, 의존 칸만으로 위상 정렬(동점은 이름 순)한 결과가 줄 순서와 같아야 한다 — 의존을 빼먹으면
  `EngineInitSequenceTest.TableIsWrittenInStartupOrder` 가 진다.
- **`Scene` 이 기본 머티리얼을 든다.** 언리얼은 `UMaterial::GetDefaultMaterial` 이 엔진 에셋이라 월드가 모른다. 여기는
  씬이 인스턴스화할 때 머티리얼 없는 메시에 채워 준다 — 결과는 같고, 엣지는 Scene → Graphics 저층(허용 방향)이다.
- **`Dialogue` · `Sequencer` · `Localization` 이 Engine 안에 있다.** 언리얼은 모듈/플러그인이지만 전부 Engine 위의
  런타임 모듈이다. 이 저장소는 물리 분할을 하지 않으므로 폴더 티어로 같은 선을 긋는다.
- **물리 모듈 분할(EngineRHI / EngineReflection)은 하지 않는다.** 그래프가 DAG 라 어디를 잘라도 순환이 없으므로,
  자를 때는 티어 경계를 그대로 링크 단위로 바꾸면 된다.
