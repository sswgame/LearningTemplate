# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 본다. 끝난 일은 git 기록에, 지킬 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다 — 여기에 되풀이하지 않는다. 끝나면 줄을 지운다.

## 일하는 법

절차는 스킬이 정본이다: 묶음 끝 검증 · push `bundle-verify`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계 · 시간 기록은 `Test/README.md`. 사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset] [--backend] [--stages a,b] [체인…]`.
병렬 작업자 워크트리는 main 에서 fast-forward 한 뒤 시작한다(옛 기준에서 시작한 갈래는 통합 때 수십 파일이 충돌했다).

## 상태 (2026-11-10)

- 작업자 다섯(누수 창 · None 이름 · GL 분기 린트 · 결과 형식 0 · Multiplayer MVVM)과 Scripts 정리(린트 표 한 장 `Scripts/lint/Rules.py`, 규칙 데이터 `Config/Rules/`)를 main 에 합쳤다. Debug 빌드 경고 0, App 단위 3645/3645.
- Android(Android-Shipping)는 대상 전체가 컴파일 · 링크되고 Vulkan 셰이더 쿠킹이 지난다. 웹(Web-Shipping)은 App.wasm 까지 지어진다.

## 남은 일

### 1. 묶음 검증

- 98fa5929ce 의 `VerifyBundle.sh` 가 돌고 있다 — 결과를 분류하고 Windows 통과 sha 를 push 한다.
- 흔들리는 단위 테스트 `AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`: 전체 실행 한 번에 "base 'testproject/base.audiograph.xml' could not be read" 로 졌다. 같은 씨앗 · 단독 1800 회로는 재현되지 않았다 — 다시 지면 그 로그의 앞 케이스와 스크래치 도메인 마운트 상태를 본다.
- 통합 때 확인하지 못한 동작: 셋이 시나리오에서 본다 — Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯(실장치), DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), 배포 서버(shipping-check — 서버가 `Window/Linux` · `Input/Linux` 를 짓지 않는다).

### 2. 플랫폼 ([PlatformTargets](PlatformTargets.md) "남은 일")

- **Android:** `CookAssets`(호스트 빌드로 굽기) 통과 → `py -3 -m Scripts android-apk --game <게임>` → 실기기에서 기동 · 회전(pre-rotation 방향) · APK 팩 읽기 · 터치. JNI 클립보드(`AndroidClipboard.cpp` 는 지금 `isAvailable` false).
- **웹:** 브라우저에 띄우기(팩 패키징 · COOP/COEP 로컬 서버 · 브라우저 MCP 로 기동 · WebGPU · 입력 · IDBFS), WebSocket 전송(클라이언트)과 서버 WebSocket 수락.
- dzn SPIR-V→DXIL 충돌.

### 3. 통합 담당이 직접 (실행 · 측정이 필요)

- **에디터 첫 패널의 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초. `-gv_traceSlowTickMs` 트레이스로 그 프레임의 동기 일을 찾아 비동기로.
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력), waterplay(강체 속도 · 잠듦 감시 값으로 조건 대기).
- **측정:** 유니티 배치 크기 12 · 24 · 48(Release 깨끗한 빌드), [DesignBacklog](DesignBacklog.md) S8 측정 표 7 행, 작업자 W25 가 남긴 GPU 비교(`gpu.timings` — `GPU.<프레임 패스>` · `RT.GPUScene.upload` · `RT.Light.uploadBytes` · `GT.GPUScene.build.viewTransparentOrder`, c05a3ee251 대 지금 — 이득이 노이즈 안이면 되돌린다).

### 4. Shooter3D 총 (사용자 지적)

- **소총 자세:** 권총 조준 클립으로 소총이 머리 옆에 들린다. 브랜치 `rifle-pose-wip`(5073ea9105)에 기반이 있다. 다음: Quaternius **Toon Shooter Game Kit**(묶음 페이지가 CC0 인지 먼저 확인)의 `Character_Soldier` 클립을 UBC 로 리타깃 → 몸 전체 사진 → 손 소켓 `Gun` 회전을 사진으로 맞춤 → 골든 다시.
- **걸을 때 총이 떨린다:** 원인 미확인. `-gv_shooterMotionTrace` 기록에 총 위치를 더해 손 · 카메라와 프레임별로 대조한다.

### 5. 작업자 · 설계 문서가 남긴 일

- **린트를 빌드 구조로(린트 표에서 지우기):** `editor-view-includes`(MVVM 이 끝나면 View 부분의 껍데기로).
- **에디터 MVVM 남은 일:** 모든 패널 View 를 옮겼다. 남은 것은 인스펙터 그리드의 값 행(3단계 — `gather` 가 프로퍼티마다 값 행을 내고 위젯이 멤버 주소에 바로 쓰지 않게, EditorUI 'Property grid edit model' 절). 그 뒤 `editor-view-includes` 를 View 부분 `#error` 껍데기로.
- **렌더링:** Vulkan placed 텍스처(해제 큐에 메모리 참조 세기), TransientLeasePool 을 뷰 풀에 잇기, 비동기 컴퓨트 큐 패스 타임스탬프 · Tracy 프레임 패스 내보내기, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단 외형의 디퍼드(G버퍼 칸 부족 — 포워드 전용으로 남김), 몸 머티리얼이 피부 층 표면 덮어쓰기(`_skinLayerSurfaceMap`)를 읽기, 텍스처 전이 · VFX · 프로브 디스패치의 버퍼별 전이를 묶음 API 로.
- **표면 변형:** 지형(`terrain.hlsl`)은 머티리얼을 픽셀에서만 읽어(GL 은 구조버퍼를 두 단계에서 못 읽는다) 정점 변위를 받지 못한다 — 엔진 텍스처 칸 `SurfaceDeform`(`shaderslot::emulated::kEngineTextureNames` 13 번째 — `SW_ENGINE_TEXTURE_COUNT` · DX11 `t44` · GL 유닛 12 · binding.hlsli 의 칸 줄)과 패스 상수 `g_SurfaceDeformParams`(창)를 더해 지형 정점 셰이더가 읽게 한다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 그림자 패스도 같은 오프셋). 네 백엔드 셰이더 · 바인딩을 함께 바꾸므로 빌드 · 시나리오를 돌릴 수 있는 작업자가 한 묶음으로 한다.
- **폴더 자리(막는 구조부터):** 엔진 쪽 `Dev/` 예외(`Config/Rules/Lint/FolderLayout.json` `_devFolderAllowed` — ModuleManager/Dev · Host/Dev · App/Dev · GF GameData/Dev · Graphics/Material/Dev)를 DevTools 로 — ModuleManager 를 공유 라이브러리로(언리얼 HotReload 처럼), DevTools 에 리플렉션 단계를 더하면 핫 리로드 · 머티리얼 그래프가 옮겨 간다.

### 5-1. 구조 · 성능 설계(2026-11-10 설계안 — 순서대로, 폴더가 겹치지 않는 작업자 단위)

- **W-P0 계측 자동 부착(선결):** `SceneTickScheduler` 가 프레임 시스템 · 씬 서비스 · 틱 배치마다 등록 이름으로 `SW_PROFILE_SCOPE` 를 건다. 오디오 · 물리 드라이버 호출부(`Engine.cpp`)에도 존 하나씩. 지금 존은 Physics 1 · Audio 0 · Scene 2 · Asset 1 이다. 완료: Tracy 에서 `GT.Frame` 자식 합 90% 이상.
- **W-S1 Engine 6층 하위 티어:** 바닥 기능(Physics · Visual · Audio · UserSettings · Net · TileMap · Navigation)과 합성 기능(Animation · VFX · Environment · Destruction · Character · UI …)으로 가른다. 층 안 상호 include 5쌍(Animation↔Character · Physics · Visual, TileMap↔Visual, Telemetry↔UserSettings)을 끊는다.
- **W-S2 `GameFrameworkLayers.toml`:** Runtime 에 Foundation 0 · World 1 · Character 2 · Gameplay/Online 3 의 층 부분과 `#error` 껍데기를 둔다. 역방향 include 24 를 끊고, 생성기 `Scripts/generate/EngineLayers.py` 를 매개변수화한다.
- **W-S3 등록 사슬 통일 · `state-shape` 형식화:** `s_pHead` 손 사슬(Audio · RHI · Module 2 · UI 접근성 · UserSettings)을 `ModuleEntryRegistrar`/`RegistrationList` 로 옮기고 `MutableStaticExceptions` 의 그 줄을 지운다. `Archive` 를 내부 헤더로 숨기고 린트 행을 지운다.
- **W-S4 `GameObjectManager.h` 허브 해체(W-P0 뒤):** 포함 TU 428 · include 30 이다. 소유 타입을 pimpl 로, 템플릿 전달은 `.inl` 로. `-ftime-trace` 집계로 재컴파일 TU · ms 를 전후 비교한다.
- **W-R1 `FramePassExecutor` 분할 → 프레임 아레나:** 3257줄을 그래프 짓기 · 패스 라이브러리 · 제출로 나눈다(성능 변화 0). 그 뒤 프레임 지역 `vector` 를 `LinearAllocator` 기반 `frame_vector` 로 옮긴다(RT 프레임 힙 할당 0).
- **W-R2 PSO 배치 색인(W-R1 뒤):** 배치를 만들 때 PSO 핸들을 한 번 풀어 둔다. `RT.PSO.ensureMaterial` 전후를 재고, 흔들림 안이면 되돌린다.
- **W-E1 패널 열림 훅:** `IEditorPanel::onPanelOpened` 에서만 비동기 준비를 시작하고, View 는 준비 전에 뼈대만 그린다(3절 "첫 패널 느린 프레임" 측정이 후보를 고른다).
- **W-G1 분할(W-S2 뒤):** `InputMap`(데이터 · 평가기 · 상태 — Enhanced Input 자리), `PrefabAsset`(포맷 셋 + 스폰), `AbilityComponent`(어트리뷰트 · 이펙트 · 스펙 컨테이너).

### 5-2. RHI 진단(2026-11-10 — 결함 먼저, 폴더가 겹치는 단위는 순서대로)

- **D1 WebGPU 백프레셔 + 공통 `RHIFrameFenceRing`(High):** 네이티브 Dawn 은 프레임 인 플라이트 상한이 없다. `WebGPURHIDevice::beginFrameInternal` 이 끝나지 않은 링을 두고 새 링을 만들어, 상수 링 · 프레임 펜스가 GPU 가 뒤처진 만큼 자란다(`IRHIDevice.h:111` 계약 위반). 네이티브는 `wgpuInstanceWaitAny` 로 가장 오래된 프레임을 기다린다. 다섯 백엔드의 프레임 펜스 링(Vulkan 손 링 · DX11/GL `retireFrameOnGPU` · WebGPU deque)을 `Backend/RHIFrameFenceRing` 하나로 모아 상한을 강제한다.
- **D2/D3 해제 키 한 길:** Vulkan `_frameFenceCounter`(원자 아님, 렌더 스레드가 올림)를 전송 완료 콜백(`VulkanRHIResourceFactory.cpp:1115`, 폴링 스레드)이 해제 키로 읽는다 — 데이터 경쟁이다. GL · DX11 의 CPU 프레임 지연 `enqueueRelease` 는 `kGPUReleaseFrameLatency == kMaxFrameCountInFlight` 의 우연한 일치에 기댄다 → `enqueueRelease` 와 그 상수를 지우고 GPU 펜스 길 하나로. GL · WebGPU `_frameSerial` 도 같은 점검.
- **C1 `RHIBindlessRegistry`:** 백엔드 다섯이 같은 등록표(번호 발급 · 생존 · 해제 대기 · 락)를 다시 쓴다. 공통으로 올리고 백엔드는 디스크립터 쓰기 훅만. GL · WebGPU `isBindlessIndexLive` 의 무락 읽기와 Vulkan 해제의 전 텍스처 선형 탐색도 사라진다.
- **DX12 기록 경로 락:** 바인딩마다 `shared_mutex`(`D3D12RHICommandContext.cpp:125,151`), 배리어마다 `_resourceStateMutex` + `_mapOffscreenTexture.find`. 병렬 기록 구간의 레지스트리 불변식(`setParallelRecording`)으로 락을 걷어 내고, 텍스처 상태를 `RHIHandleTable` 레코드로 옮긴다. `bench.json` p50 으로 전후를 잰다.
- **C2 전송 조립 템플릿:** 업로드 · 되읽기 · 구간 갱신의 조립 순서를 `Transfer/RHITextureTransferDriver` 로, DX12/Vulkan 구조 버퍼 스테이징 성장 정책을 공유한다.
- **C3/C4 타임스탬프 링 · 스왑체인 세트:** `RHITimestampRing` · `RHISwapChainSet`. Vulkan 보조 창 재생성의 프레임 중간 `vkDeviceWaitIdle`(`VulkanRHIDevice.cpp:1634`)은 다음 프레임으로 미룬다.
- **측정 뒤 조건부:** Vulkan 드로마다 `vkUpdateDescriptorSets` 인지 `RT.Bind.descriptorUpdates` 로 먼저 센다. WebGPU 바인드 그룹 캐시를 프레임마다 비우지 말고 펜스로 은퇴시킨다.
- **작은 정리:** `IRHIDevice::getShaderTargetFormat()` 으로 Renderer 의 `getBackendType()` 6곳을 없앤다. `IRHIResourceFactory::create*` 에 `[[nodiscard]]`, `transitionBuffer` 단건은 `[[deprecated]]`, Vulkan 프레임마다 세마포어 `vector` 할당을 없앤다.
- **측정 뒤에만:** 중립 커맨드 스트림 · RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더(`RHIHandleKind`/`RHIPipelineStateDesc._kind` + ABI 범프). Android 는 이미지마다 `VkDeviceMemory` 하나라 `maxMemoryAllocationCount`(4096)에 닿을 수 있다 → 서브할당(추정).

### 5-3. 병렬이 기본인 오브젝트 · 물리 모델(2026-11-10 설계 — 하이브리드: 언리얼 Mass + 액터 자리)

GameObject/Component API 는 저작 · 수명 · 직렬화 층으로 두고, 프레임마다 도는 일은 읽기 · 쓰기 종류를 선언한 잡으로, 핫 데이터는 종류별 SoA(`SceneTransformStorage` 일반화)로 옮긴다. 완전 ECS 는 기각했다(컴포넌트 타입 229 · `getComponent<>` 593 · 리플렉션 · 프리팹 전부 재작성). 단계마다 빌드 · 테스트가 통과해야 하고, 이득이 흔들림 안이면 되돌린다.

0. **측정 기반(W-P0 뒤):** `-workers=N` 인자, Debug 카운터(`getComponentLookups` · `virtualTicks`), `load/*` 4 시나리오 × 워커 1 · 2 · 4 · 8 기준선을 S8 표에.
1. **일괄 틱이 기본:** 리플렉션 생성기가 `onTick` 을 재정의한 타입(102 개)에 `TickBatchOf` 등록을 자동으로 낸다. 지금 수동 등록은 `StressTestMover` 하나뿐이다. 완료: `batchElements/objectEntries ≥ 0.9`.
   - **서브틱 · 선행 조건은 그대로 돌아야 한다(사용자 요구).** 지금 규칙(`TickRegistry::findBatchFunction`, `TickRegistry.cpp:814`)을 유지한다.
     - 일괄 경로는 그 그룹의 틱 항목이 주 틱 하나뿐이고 선행 조건이 없는 오브젝트만 탄다.
     - 서브틱(`registerSubTick`)이 있거나, 항목이 둘 이상이거나, 선행 조건이 걸리면 오브젝트 칸 경로나 선행 조건 스테이지로 간다.
     - 오브젝트 안 순서(그룹 · 단계 · 우선순위 · 붙은 순서), "한 오브젝트는 한 스레드", 선행 조건 스테이지가 앞 경로의 쓰기를 먼저 적용하는 것(`_bApplyBefore`)이 일괄 경로에도 그대로다.
   - **틱 중 바뀌는 상태:**
     - 서브틱 등록 · 해제 · 선행 조건 · 활성 변경은 지금처럼 틱 뒤로 미루고, 다음 `refresh` 가 일괄 소속을 다시 정한다.
     - 실행 여부(원자 마스크 · `_bRunnable`)는 일괄 함수도 항목마다 본다.
   - **1b. 서브틱도 일괄(사용자 요구 — 서브틱을 쓴다고 느린 길로 빠지지 않게).** 서브틱 · 다중 항목 오브젝트가 일괄에서 빠지는 까닭은 "오브젝트 안 순서를 한 스레드가 지킨다" 하나뿐이다. 이 순서를 스레드 대신 **파도**로 지킨다.
     - 파도 키: (단계, 우선순위, 같은 키끼리의 오브젝트 안 순번). 대부분 순번 0 이라 파도는 몇 개다.
     - 파도 안에서는 (정확한 타입, 서브틱 id) 별 일괄 함수를 병렬로 돌린다. 파도 사이에는 장벽을 둔다.
     - 한 오브젝트의 두 항목은 다른 파도에만 들므로 오브젝트 안 순서와 형제 컴포넌트의 무락 접근이 그대로다.
     - 선행 조건 스테이지는 레벨마다 같은 방식으로 일괄한다.
     - 일괄 함수는 `onSubTick( id, dt )` 를 한정 이름으로 부른다(비가상).
     - 유니티 DOTS(시스템 순서 = 장벽, 시스템 안 = 청크 병렬)와 같은 자리. 언리얼은 TickFunction 마다 태스크다.
     - 장벽 비용이 있으므로 0 단계 기준선과 비교해 흔들림 밖일 때만 채택한다. 동등성 테스트에 이 경로도 넣는다.
   - **테스트:** 같은 씬을 일괄 경로 켬 · 끔으로 N 프레임 돌려 상태 해시(`TestSceneStateHash`)가 같다. 서브틱 · 선행 조건 · 틱 중 서브틱 등록 · 해제를 섞은 케이스(`TickedPathMover` 테스트, 래그돌 `TickPhase::Late` 순서)가 두 경로에서 같다. 2 단계(잡 컨텍스트)도 이 동등성 테스트를 그대로 통과해야 한다.
2. **잡 컨텍스트 형식:** `_bLaneSafe` 를 서명 분리(`runLane(SceneJobContext&)` / `runMain`)로 바꾸고, `SW_SCENE_SERVICE` 의 접근 선언을 필수로, `SceneDataKind` 를 64 비트로. 컨텍스트는 명령 버퍼만 노출한다(틱 안 구조 변경을 형식으로 막는다).
3. **물리 Export · 지연 이벤트:** `PhysicsPoseStore` 이중 버퍼를 병렬로 읽고 `applyTransformBatch` 로 일괄 쓴다(지금은 바디마다 백엔드 3 호출 + 세터). 충돌 배달은 `Physics` 단계 안 동기 가상 호출 대신 `FlushPostPhysics` 서비스로.
4. **Jolt 조임 · 물리 훅 배치:** `JoltJobSystem.cpp:103` 의 인라인 실행 폴백을 지우고, `WaitForJobs` 가 `yield` 대신 돕기로 기다린다. 물리 훅 4 종을 배치 함수 + 명령 저장소로.
5. **`ComponentDataStore<T>`:** 트랜스폼 SoA 페이지를 템플릿으로 일반화한다. 첫 사용자는 강체 자세 · 명령, 투사체.
6. **엔진 공간 색인:** `SceneSpatialGrid` 서비스(월드 버전으로 증분, 이중 버퍼 읽기). GF `InteractionSpatialIndex` · `ZoneOccupantIndex` 는 읽기만. `TagIndex` 는 세대 스냅샷으로.
7. W-S4 허브 해체(5-1) → 8. `StructuralChangeBuffer` 를 스레드별 버퍼 + (writerID, 순번) 병합으로(틱 뒤 큐가 뮤텍스 하나라 투사체 수천에서 줄 선다).

병렬 작업자: A(0 · 1, Task · 생성기 · TickBatch), B(3 · 4, `Engine/Physics/**` — `-1` 기본값 브랜치 병합 뒤), C(2, A 뒤), D(6, C 뒤). 확인할 결함 후보: 틱 본문 안 중첩 `runParallel` 이 돕기 중 TLS 틱 상태를 비워 writerID 가 0 이 되는 것(`SceneTickScheduler.cpp:83`), 남의 오브젝트에 `setCanEverTick` 을 쓰는 비트필드 경쟁(`Component.h:620`).

### 6. 리소스

- **Sprite Fusion 타일셋**(상세 페이지마다 `License: CC0`): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer(Kenney)는 Kenney 원본에서.
- **Sprite Fusion VFX 25 종**(MIT — 저작권 고지를 credits 에): 캔버스에 그리는 TypeScript 코드다 → 브라우저 MCP 로 재생해 스프라이트 시트로 굽는다.
- **PixelFX Piper**(무료 브라우저 효과 생성기): 이용 조건을 먼저 확인해 credits 에, 기본 효과를 시트로 내보내 층 쌓기 기법으로 효과를 만든다.

### 7. 그 뒤

- Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기).
- 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 보기. 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 보기.
- 남은 단위 테스트: `py -3 Scripts/dev/TestCoverage.py` 의 "없음" 폴더, 온라인 `AccountTicketConnect` 의 만료 · 재시도 길(성공 길은 `net.ticketconnect` 시나리오가 본다).
- 흔들리는 게임 골든: 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.
- **Linux**(큰 일이 모두 끝난 뒤 — 사용자 결정): `LinuxScenarios.py all`, `.linux` 골든, 드라이버를 고르는 칸(dzn · Mesa d3d12 GPU), 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심). lavapipe `blendEnable` + `R16G16_SNORM` 은 `VulkanRHIResourceFactory` 가 이제 막는다 — 재현해 확인.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스 규칙(2026-11-09):** 받아 오는 리소스는 무료 · 공개 라이선스(CC0 우선, MIT 처럼 재배포가 되는 것은 고지와 함께)만 — 묶음 페이지의 라이선스를 확인해 credits 에 URL · 날짜. 출처 하나에 고집하지 않고 뼈대가 달라도 리타깃한다. 에이전트가 리소스를 직접 그리지 않는다. PixelFX Piper(무료)는 쓴다. Agent Sprite Forge 는 도입하지 않는다(유료 생성 API 필요). Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌었다 — 새 묶음은 그 묶음 페이지가 CC0 일 때만. 원본 바이너리는 Git LFS, 받아 둔 팩은 `/AssetLibrary/`(무시)에 두고 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/` 로.
- 범용 스크립팅 언어는 도입하지 않는다(실제 요구가 확인될 때만 — [판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤, GC 는 권하지 않는다.
- 웹은 wasm32 다(유니티 · Godot 웹과 같다 — memory64 는 Safari 미지원 · 경계 검사 비용). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓰고 스스로 다시 짓는다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`(시연 모드는 테스트 자료). App 의 `Dev/` 는 남긴다(배포에 쓰지 않는 인터페이스를 두지 않는다).
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
