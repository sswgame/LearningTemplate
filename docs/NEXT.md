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
- **측정:** Tracy 에서 `GT.Frame` 자식 구간 합이 90% 이상인지(W-P0 계측 자동 부착이 들어왔다 — 모자라면 빠진 호출부에 구간), 유니티 배치 크기 12 · 24 · 48(Release 깨끗한 빌드), [DesignBacklog](DesignBacklog.md) S8 측정 표 7 행, 작업자 W25 가 남긴 GPU 비교(`gpu.timings` — `GPU.<프레임 패스>` · `RT.GPUScene.upload` · `RT.Light.uploadBytes` · `GT.GPUScene.build.viewTransparentOrder`, c05a3ee251 대 지금 — 이득이 노이즈 안이면 되돌린다).

### 4. Shooter3D 총 (사용자 지적)

- **소총 자세:** 권총 조준 클립으로 소총이 머리 옆에 들린다. 브랜치 `rifle-pose-wip`(5073ea9105)에 기반이 있다. 다음: Quaternius **Toon Shooter Game Kit**(묶음 페이지가 CC0 인지 먼저 확인)의 `Character_Soldier` 클립을 UBC 로 리타깃 → 몸 전체 사진 → 손 소켓 `Gun` 회전을 사진으로 맞춤 → 골든 다시.
- **걸을 때 총이 떨린다:** 원인 미확인. `-gv_shooterMotionTrace` 기록에 총 위치를 더해 손 · 카메라와 프레임별로 대조한다.

### 5. 작업자 · 설계 문서가 남긴 일

- **린트를 빌드 구조로(린트 표에서 지우기):** `editor-view-includes` 를 Panels View 부분의 `#error` 껍데기로. 먼저 린트가 못 보는 전이 include 둘을 끊는다 — `VFXPreviewPanel.cpp` → `Engine/VFX/VFXComponent.h`(View 가 미리보기 `VFXComponent*` 를 든다 — ViewModel 값으로), `PrefabEditorPanel.h` → `Viewport/EditorPreviewGizmo.h` → `EditorViewportGizmo.h` → `Workspace/EditorTransaction.h`. 껍데기는 티어 6 View 부분 중 `Panels` 만(같은 티어 `AssetActions` 는 씬 헤더를 쓴다 — 부분을 가른다), `GameFramework/.../MultiplayerInProcessTab` 은 확장 모듈 쪽 껍데기로. 닫힘 검사는 전이 include 를 다 따라간다(EditorUI 'MVVM' 절).
- **에디터 MVVM 측정(통합):**
  - Hierarchy: 큐브 8000 루트 씬에서 Hierarchy 창을 연 프레임 시간을 옮기기 전 커밋과 대조한다(모두 접힘 · 루트 하나 펼침 · 맨 아래로 스크롤). 나빠졌으면 원인을 `HierarchyViewModel::gather` 에서 찾는다.
  - 인스펙터 값 행: 프로퍼티 많은 대상(컴포넌트 여럿을 단 오브젝트 · 프로젝트 설정 · 펼친 큰 배열)에서 인스펙터 프레임 시간과 `Editor.InspectorFilledRows` · `Editor.InspectorSkippedRows` 를 값 행 전 커밋과 대조한다(맨 위 · 맨 아래로 스크롤 · 스크롤 중). 나빠졌으면 `PropertyGridModel::gather` 의 행 채우기(값 사본 · 기본값 비교 · 혼합)에서 찾는다.
- **렌더링:** Vulkan placed 텍스처(해제 큐에 메모리 참조 세기), TransientLeasePool 을 뷰 풀에 잇기, 비동기 컴퓨트 큐 패스 타임스탬프 · Tracy 프레임 패스 내보내기, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단 외형의 디퍼드(G버퍼 칸 부족 — 포워드 전용으로 남김), 몸 머티리얼이 피부 층 표면 덮어쓰기(`_skinLayerSurfaceMap`)를 읽기, 텍스처 전이 · VFX · 프로브 디스패치의 버퍼별 전이를 묶음 API 로.
- **표면 변형:** 지형(`terrain.hlsl`)은 머티리얼을 픽셀에서만 읽어(GL 은 구조버퍼를 두 단계에서 못 읽는다) 정점 변위를 받지 못한다 — 엔진 텍스처 칸 `SurfaceDeform`(`shaderslot::emulated::kEngineTextureNames` 13 번째 — `SW_ENGINE_TEXTURE_COUNT` · DX11 `t44` · GL 유닛 12 · binding.hlsli 의 칸 줄)과 패스 상수 `g_SurfaceDeformParams`(창)를 더해 지형 정점 셰이더가 읽게 한다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 그림자 패스도 같은 오프셋). 네 백엔드 셰이더 · 바인딩을 함께 바꾸므로 빌드 · 시나리오를 돌릴 수 있는 작업자가 한 묶음으로 한다.
- **폴더 자리(막는 구조부터):** 엔진 쪽 `Dev/` 예외(`Config/Rules/Lint/FolderLayout.json` `_devFolderAllowed` — ModuleManager/Dev · Host/Dev · App/Dev · GF GameData/Dev · Graphics/Material/Dev)를 DevTools 로 — ModuleManager 를 공유 라이브러리로(언리얼 HotReload 처럼), DevTools 에 리플렉션 단계를 더하면 핫 리로드 · 머티리얼 그래프가 옮겨 간다.

### 5-1. 구조 · 성능 설계(2026-11-10 설계안 — 순서대로, 폴더가 겹치지 않는 작업자 단위)

- **W-S4 `GameObjectManager.h` 허브 해체(포함 TU 433):** 구조 조각은 다 들어갔다 — 씬 부품(트랜스폼 계층 · 프리미티브 · 카메라 · 컴포넌트 레지스트리 · 태그 색인 · 틱 스케줄러 · 오브젝트 저장소 · 씬 범위와 서비스 목록)은 `GameObjectManager::State`(pimpl), 템플릿 몸체는 `GameObjectManager.inl`(부르는 파일 215 개만 포함), `createGameObject` 기본 이름은 오버로드. 헤더 include 30 → 24 → 16 이고 `GameObject.h` · `GameObjectStore.h` · `ISceneService.h`(→ `SubsystemScope` · `RegistrationList`) · `ComponentRegistry.h` · `ObjectIDAllocator.h` 가 닫힘에서 빠졌다. 남은 것: (c) 통합 빌드 — `.inl` 을 빠뜨린 TU 는 컴파일이 아니라 링크(정의 없는 `GameObjectManager::forEach…` · `resolveComponent<…>` 템플릿 심볼)에서 멈추므로 그 TU 에 `.inl` 을 더한다. 막는 구조로는 clang `-Werror=undefined-func-template` 을 Engine · GameFramework · Games 에 켜 컴파일에서 잡을 수 있는지 그 빌드에서 본다. 이어 `-ftime-trace` 집계로 재컴파일 TU · ms 를 전후 비교(통합 담당).
- **W-R1 `FramePassExecutor` 분할 → 프레임 아레나:** 3257줄을 그래프 짓기 · 패스 라이브러리 · 제출로 나눈다(성능 변화 0). 그 뒤 프레임 지역 `vector` 를 `LinearAllocator` 기반 `frame_vector` 로 옮긴다(RT 프레임 힙 할당 0).
- **W-R2 PSO 배치 색인(W-R1 뒤):** 배치를 만들 때 PSO 핸들을 한 번 풀어 둔다. `RT.PSO.ensureMaterial` 전후를 재고, 흔들림 안이면 되돌린다.
- **W-E1 패널 열림 훅:** `IEditorPanel::onPanelOpened` 에서만 비동기 준비를 시작하고, View 는 준비 전에 뼈대만 그린다(3절 "첫 패널 느린 프레임" 측정이 후보를 고른다).
- **W-G1 분할:** `InputMap`(데이터 · 평가기 · 상태 — Enhanced Input 자리), `PrefabAsset`(포맷 셋 + 스폰), `AbilityComponent`(어트리뷰트 · 이펙트 · 스펙 컨테이너).

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

0. **측정 기반(통합 담당):** `-workers=N` 인자 · Debug 카운터(`GT.Object.getComponentLookups` · `GT.Scene.tick.virtualTicks`)는 들어갔다. `load/*` 4 시나리오 × 워커 1 · 2 · 4 · 8 기준선을 S8 표에 적는다.
1. **일괄 틱 · 파도 · 레인 — 런타임은 들어갔다(`TickRegistry` · `SceneTickScheduler` · `TickBatchRegistry`, 테스트 `TickLanesTest`).** Batch 레인은 서브틱 · 여러 항목 오브젝트까지 파도(순서 키, 오브젝트 안 순번)로 일괄하고, Task 는 오브젝트마다 태스크, Main 은 게임 스레드다. 일괄 경로를 끈 레지스트리(`setBatchPathEnabled( false )`)가 동등성의 기준 길이다. 남은 것:
   - **생성기(5-4 작업자):** `REFLECT( TickMode = Batch|Task|Main )` 을 받아, `onTick` · `onSubTick` 을 재정의했거나 `TickMode` 를 단 타입마다 생성 TU 의 `namespace sw` 에 `SW_REGISTER_TICK_TYPE( 타입, 레인 )` 을 낸다(`TickBatchRegistry.h` 머리말이 계약). `TickBatchOf<타입>` 이 `onTick` · `onSubTick` 을 한정 이름으로 부르므로 보호 재정의는 `REFLECT_BODY` 가 친구로 연다. 손 등록은 `StressTestMoverComponent` 하나 — 생성기가 내면 지운다. 완료: Debug `virtualTicks` 가 0 에 가깝다.
   - **선행 조건 스테이지 일괄:** 스테이지 항목은 레인의 일괄 함수(비가상)로 돈다(`TickStage::_listItemFunction`). 남은 것은 같은 함수의 이웃 항목을 한 번에 넘기도록 레벨 안을 (타입, 서브틱 id) 로 묶는 것 — 스테이지 편집(`flushStageEdits`)이 항목 순서를 가정하므로 그 정렬 규칙과 함께 바꾼다.
   - **Task 의 선행 조건 간선:** 선행 조건을 건 항목은 지금 그룹 끝 스테이지(Kahn 레벨 장벽)로 간다. 간선만 기다리는 개별 태스크로 바꾸면 파도 · 레벨 장벽에 묶이지 않는다(언리얼 TickFunction 선행 조건 자리).
   - **판정 숫자(통합 담당):** 구조 지표 `GT.Scene.tick.waves`(장벽 수) · `batchElements` · `taskEntries` · `mainEntries` · `objectEntries` 를 부하 시나리오에서 읽어 S8 표에. 벽시계는 오브젝트 8000 이상에서만.
2. **잡 컨텍스트 형식 — 프레임 시스템은 들어갔다:** `ISceneMainFrameSystem::runMain` / `ISceneLaneFrameSystem::runLane( SceneJobContext& )`(매니저 읽기 전용 + 틱 뒤 큐), `SceneDataKindSet` 64 비트. 남은 것: (a) **컴포넌트 틱의 컨텍스트** — Batch 레인의 `onTick` 도 같은 형식(읽기 전용 남의 오브젝트 + 명령 버퍼)으로 받아 남의 컴포넌트 직접 쓰기(결함 후보 2 · `setCanEverTick` 비트필드)를 컴파일에서 막는다. 102 개 타입의 서명이 바뀌므로 생성기(5-4)의 `TickBatchOf` 호출과 함께 옮긴다 — 서명 · 옮기는 순서는 `TickBatchRegistry.h` 머리말에 적었다. (b) `SW_SCENE_SERVICE` 의 접근 선언 필수화 — 지금 접근 선언은 프레임 일(`SW_SCENE_SERVICE_WITH_FRAME_JOB`)만 읽으므로, 단계 본문의 접근을 쓰는 곳(서비스 병렬화)을 먼저 정해야 의미가 있다.
3. **물리 Export · 지연 이벤트 — 들어갔다:** 움직인 바디는 백엔드 일괄 읽기(`readBodyStates`, Jolt 무잠금)를 워커 구간으로 나눠 읽고 주인의 이전 · 지금 자세(이중 버퍼)에 넣는다. 끝 자세는 `ScenePhysics::queuePoseWrite` 로 모아 `applyTransformBatch` 한 번 + 펼친 뒤 부모 있는 것만 풀어 쓴다. 접촉 · 겹침 배달은 `FlushPostPhysics` 단계의 `ScenePhysicsEvents` 서비스다. 자세를 바디 칸 번호 SoA(`PhysicsPoseStore`)로 옮기는 것은 5 의 `ComponentDataStore` 와 함께 — 따로 하면 두 번 옮긴다.
4. **물리 훅 배치:** Jolt 조임(인라인 실행 폴백 삭제 · `WaitForJobs` 돕기)은 들어갔다. 남은 것: 물리 훅 4 종(`beginPhysicsFrame` · `prePhysicsStep` · `postPhysicsStep` · `endPhysicsFrame`, 재정의 타입 12)을 타입별 배치 함수로 — 재정의가 대개 보호 · 비공개라 `TickBatchOf` 처럼 한정 이름으로 부르려면 `REFLECT_BODY` 의 친구 열기가 필요해 5-4 생성기와 함께(`SW_REGISTER_PHYSICS_HOOKS( 타입 )`, 재정의 여부는 `&T::hook` 의 클래스 형식으로 컴파일에서 — 지금의 "기본 구현이 불리면 뺀다" 런타임 비트를 대신한다). 명령 저장소(강체마다 `SpinLock` + `PhysicsBodyCommand`)는 5 의 첫 사용자(강체 자세 · 명령)로 옮긴다.
5. **`ComponentDataStore<TPage, MaxPages>` — 들어갔다:** 트랜스폼 칸 저장소가 그 틀 위에 있고(페이지 계약 `initializeSlot` · `releaseSlot`), 첫 물리 사용자 `PhysicsPoseStore` 가 강체 3D · 2D 의 바디 핸들 · 앞뒤 스텝 자세 · 속도 · 명령(칸마다 스핀락)을 강체 칸 SoA 로 든다 — 스텝 뒤 일괄 읽기는 바디 칸 → 강체 칸 표로 칸에 바로 쓴다. 투사체(`ProjectileSimulation`)는 옮기지 않는다: 컴포넌트가 아니라 시뮬레이션이 든 연속 `vector<ProjectileState>` 라 칸 번호가 필요 없고(저장 · 롤백이 목록 순서), SoA 로 가르는 것은 배치 레이아웃 문제라 측정(`load/*`)이 이득을 보일 때만. 남은 것: 캐릭터 컨트롤러 3D · 2D 의 입력(`_commandLock` + 이동 · 점프 · 루트 모션 대기 값)을 같은 칸 모양으로 — 강체와 명령 집합이 달라 페이지가 따로다.
6. **엔진 공간 색인:** `SceneSpatialGrid` 서비스(월드 버전으로 증분, 이중 버퍼 읽기). GF `InteractionSpatialIndex` · `ZoneOccupantIndex` 는 읽기만. `TagIndex` 는 세대 스냅샷으로.
7. W-S4 허브 해체(5-1) — 구조 조각(State pimpl · 템플릿 .inl · GameObject.h 제거)은 들어갔고 통합 빌드 · 측정이 남았다(5-1 줄).

병렬 작업자: B(3 · 4, `Engine/Physics/**`), C(2), D(6, C 뒤), 5-4 작업자(1 의 생성기). 결함 후보 둘은 확인했다 — 중첩 `runParallel` 의 writerID 0 은 고쳤고(`TickThreadScope` 가 들어올 때의 상태로 되돌린다), `setCanEverTick` 비트필드는 틱 중 쓰기가 `deferIfStructureFrozen` 으로 미뤄져 지금은 경쟁이 없다(틱 밖에서 워커 레인이 남의 컴포넌트에 쓰면 다시 열린다 — 2 단계 형식이 막을 자리).

### 5-4. 리플렉션(2026-11-10 설계 — 규모: REFLECT 1,408 · PROPERTY 6,708 · ENUM 413, 생성 TU 가 전체 컴파일 CPU 의 20 %(Debug) · 17 %(Release))

조회 핫패스(`StaticType` · `castTo`, 1~2 ns)는 상용 수준이다. 비용과 결함은 등록 쪽과 생성 코드의 무게에 있다.

- **W-R0 계측:** `registerModuleTypes` · `registerPendingTypes` · `populateLookupCaches` 에 구간, `MemoryTag::Reflection` 전후 바이트, 등록당 캐시 비움 수 카운터. `.ninja_log` 로 생성 TU 비중을 내는 `Scripts/dev/BuildTimeReport.py`.
- **W-R1 런타임 핵심(결함 먼저):**
  - `registerClass` 가 등록 한 번마다 **모든 타입**의 상속 프로퍼티 목록을 비운다(`TypeRegistry.cpp:190`). 다음 조회가 부모 목록을 값으로 복사해 다시 짓는다(`TypeInfo.cpp:311`).
    - 등록 비용이 O(타입²) 이다.
    - 무관한 키트 하나만 로드돼도, 들고 있던 `const PropertyInfo*`(UI 바인딩 경로 · 시퀀서 트랙)가 매달린다.
    - 지연 `mutable` 캐시를 잠금 없이 읽는 것과 등록이 경합한다.
  - 고칠 모양: 상속 목록을 복사 대신 뷰(`span<const PropertyInfo*>`)로 둔다. 무효화는 자손만 하고, 캐시는 등록 때 즉시 지어 `mutable` 을 없앤다. 보관처는 (타입, 색인, 세대)로 든다. 소비자 없는 `_onPropertyBoundChanged` 는 지운다.
- **W-R3 생성기(5-3 선결):**
  - **(a) 정적 서술자 표:** 생성 코드를 constexpr 서술자 표로 바꾼다(언리얼 UHT `PropPointers` 자리). 해시를 미리 계산하고, 컨테이너 래퍼는 정적 객체로 두어 `shared_ptr` 를 없앤다. 목표는 등록 할당 타입당 3 이하, 생성 바이트 10.8 → 4 MB 미만이다.
  - **(b) 메타데이터 형식화:** `Units` 는 열거로 둔다(지금은 검증한 뒤 문자열로 실어 인스펙터가 `== "rad"` 로 다시 푼다). `Default` 는 그 프로퍼티 타입의 리터럴로 내서 오타가 컴파일 오류가 되게 한다. `EditCondition` 은 생성기가 미리 풀고, 없는 이름은 생성 오류로 막는다.
  - **(c) 타입 특성 수집기:** 재정의(`onTick` · `onSubTick` · `serializeState`)를 특성 비트로 모은다. `REFLECT( TickMode = … )` 를 받아 `TickBatchRegistry` 등록을 생성 코드에 낸다. 그러면 손 매크로 `SW_REGISTER_TICK_BATCH` 가 사라진다.
  - **(e) 조건부 필수 주석:** "부모(`_base` · 상속)가 없을 때만 `Required`" 같은 조건을 메타데이터로 적는다. `CultureInfo` 는 상속 덮어쓰기라 칸이 비어도 부모 값을 받으므로 지금의 무조건 `Required` 로는 `XMLFields` 로 옮기지 못했다(D1 손 파서 이전에서 남김). 생성기가 조건을 서술자에 싣고 필수 칸 판정(`SerializerUtil::isRequiredValueMissing`)이 합친 상태에서 본다.
  - **(d) 경고를 생성 오류로:** 클래스 템플릿 `REFLECT` · 멤버 함수 템플릿 `FUNCTION` 은 지금 경고만 남기고 건너뛴다. `#if` 아래 `PROPERTY` 가 조용히 빠지는 것도 막는다. 같은 모듈 안에 리프 이름이 겹치면 오류로 낸다.
- **W-R5 빌드 시간(W-R3 와 병렬 — 파일이 다르다):** 헤더당 `.gen.cpp` 대신 모듈 안 8~16 헤더 묶음 TU 로 짓는다(PCH 를 1,465 번 연다). `.gen.meta.cpp` 는 같은 TU 의 `#if` 절로, 의존은 헤더별로 귀속한다. 목표는 생성 비중 8 % 미만이다.
- **W-R4 프로퍼티 방문자 하나(W-R3 뒤):** 직렬화 9 루프 · 검증 · 문서 · 인스펙터 gather · 상태 해시 기본 구현이 `ReflectVisit` 하나를 쓴다. 지금 `foldStateHash` 는 손으로 써서 새 칸이 해시에서 빠질 수 있다. 소비자가 테스트뿐인 `Replicated` 메타데이터는 여기서 첫 소비자를 얻는다.
- **W-R6 밀집 타입 색인:** 5-3 의 5 단계(`ComponentDataStore<T>`)와 함께만.

### 5-5. 상용 대비 고도화(2026-11-10 진단 — 5-1 ~ 5-4 밖)

기능 폭은 대부분 찼다(패널 64 · 단위 5619 · 시나리오 390 · 골든 631). 빈 곳은 검증 체계의 구조와 출시 뒤를 위한 이관 · 운영이다. 즉시 병렬 가능: T1 · T2 · D1 · D2 · N2 · U1(5-1 ~ 5-4 폴더와 겹치지 않는다). T1 · T2 는 5-1 · 5-3 의 상태 해시 동등성 판정이 흔들리지 않게 하는 선결이다.

- **T1 골든 판정 · 기록(최우선):**
  - 기록 모드가 진 골든만 다시 쓴다(`AutomationJudge.cpp:474` — 허용치 안이면 돌아간다). 그래서 0.99 % 로 남은 골든이 다음 실행에 1.01 % 로 졌다(오늘의 bodyviews).
  - `GoldenVerdict { Pass, PassNearLimit, Fail }` 를 두고, 기록은 여유 폭(허용치의 절반) 밖이면 다시 쓴다. 보고에 `nearLimitGoldens[]` 를 남긴다.
  - 판정을 두 축으로 나눈다: 넓고 얕은 차이(평균 거리)와 좁고 깊은 차이(최대 연결 영역). 지금 "색 거리 12 이상 화소 ≤ 1 %" 한 식은 작은 마젠타 덩어리를 놓친다.
  - 골든 옆 `*.golden.json` 에 기록 커밋과 측정값을 둔다. 30 일간 골든 631 개가 1442 회 다시 쓰였다.
  - WebGPU 골든은 0 이다 — 체인 백엔드 집합을 한 곳에서 정의해 골든 폴더와 대조한다.
- **T2 자동화 격리 — 통합 검증만 남음:** 자동화 실행(`CommandLineManager::isAutomatedRun` — `-unattended` · `-scenario`)은 사용자 데이터 뿌리를 `Saved/Automation/UserData/` 아래로 가두고 밖이면 종료 11(`AutomationUserDataRoot`), 상태 자리 변수(`gv_editorStateDir` · `gv_userSettingsFile`)가 사람의 폴더여도 11, 백엔드별 부모가 자식마다 `<뿌리>/<백엔드>`, 두 번 기동 쌍은 묶음 뿌리(`RunStateDirPair.cmake`).
  - 남은 일: 빌드 · `AutomationUserDataRootTest`(리눅스 포함 — 대소문자 케이스) · `UnitTests*`(무인 실행이라 이제 격리된다) · 체인(`engine.isolation` 포함) · `Scenario.velmora.sequence.*` · `Scenario.editor.statedir.*` · `py -3 -m Scripts net-pair mmo` 를 돌린다.
  - `-unattended` 도 `-scenario` 도 없는 스크립트 실행(`LockstepPairRun.py` · `TelemetryLocalRun.py` — 표준 입력 명령으로 모는 보이는 실행)은 판정 밖이라 사람의 세이브 폴더를 쓴다. 세이브를 쓰면 스크립트가 `-unattended` 를 붙이거나 판정에 표준 입력 실행을 더한다.
- **T3 테스트 이력 · 격리 · 파이프라인(T1 뒤):**
  - 보고 JSON 을 sqlite 에 쌓고, 흔들림 지수와 p95 추세를 낸다. 격리 표 `Config/Rules/Test/Quarantine.json` 은 사유 · 만료 필수다.
  - 샤드를 시간 균형으로 나누고, 묶음 끝 셸 스크립트를 단계 · 산출물 · 재시도가 적힌 파이프라인 정의로 옮긴다.
  - `ScenarioCulprit.py` 는 로그 문구 대신 JSON 을 읽는다. 외부 CI 러너는 비용이 드는 연동이라 사용자 결정이다.
- **D1 데이터 필수 칸 린트:** 발 뼈 결함의 모양이다. `PROPERTY` 의 문자열 · `hashed_string` · `AssetID` 칸이 비어 있지 않은 C++ 기본값을 갖고 `Required` 가 아니면 오류로 낸다. 빠진 칸은 로드 오류 id(`Reflection.RequiredMissing`)로 드러낸다.
- **D2 포맷 · 세이브 이관 표:**
  - 지금 에셋(`AssetFormat.h:5`) · 세이브(`SaveGame.h:85`)는 판이 다르면 거절한다. 출시 뒤 첫 판 올림에서 사용자 데이터를 모두 잃는다.
  - `SW_FORMAT_UPGRADE( 종류, n, n+1, fn )` 순수 함수 사슬을 둔다. 빠진 단계는 오류이므로 폴백이 아니다. `Test/Fixtures/formats/<종류>/v<n>/` 샘플로 단위 테스트한다.
  - 표가 비어 있는 동안 동작은 같다(언리얼 `FCustomVersion` 자리).
- **N2 서버 배포 · 설정 검증:**
  - `Scripts/package/ServerImage.py` 를 둔다(Linux 서버 + 팩 + 설정 → tar + systemd unit, 컨테이너는 선택).
  - `ServerApp --check-config` 를 둔다(잘못된 설정은 종료 11).
  - `OpsHTTPEndpoint` 는 인증 없는 GET 이다. 바인드 주소를 루프백 · 사설로 형식에서 제한하고, 외부 바인드는 토큰 필수로 한다.
- **U1 햅틱 · UI 포커스 내비:** 진동 · 트리거 출력 API 가 없다(grep 0). `IInputFeedbackDevice` 와 `*.haptic.xml` 을 둔다. 패드 전용 메뉴 탐색(방향 포커스)을 확인하고 없으면 더한다.
- **A1 애니메이션 포즈 노드 등록식(평가 경로는 5-3 뒤):**
  - 포즈 노드가 `None · BlendPoses` 둘뿐이라(`AnimationGraphAsset.h:106`) 새 블렌드마다 enum · 로더 · 플레이어를 고친다. 리그 노드처럼 등록표로 바꾼다.
  - 그 위에 Mirror · Montage(섹션 · 분기)를 더한다.
- **R1 에셋 리다이렉터(5-4 뒤):** GUID 전환은 기각한다(경로 id 가 언리얼 SoftObjectPath 자리). `redirects.xml`(옛 → 새)를 `AssetPaths` 앞단에서 풀어, 세이브 · 서버에 적힌 옛 경로가 산다. 린트가 남은 참조를 보고한다.
- **N1 복제 스키마를 리플렉션에서(5-4 뒤):**
  - 엔티티 상태 바이트를 게임이 손으로 양자화한다. 그래서 칸 하나를 더하면 서버 · 클라 두 곳을 고쳐야 한다.
  - `PROPERTY( Replicated, Quantize = … )` 로 작성기를 생성하고, 스키마 해시를 와이어 버전에 합친다.
  - `kMaxEntityBytes` 초과는 첫 경고만 남기고 엔티티가 사라진다 — `registerEntity` 의 오류로 바꾼다.
- **E1 되돌리기를 형식으로:** 씬 변이 명령이 `EditorTransaction&` 를 요구하게 한다. 시나리오 `editor/undocoverage` 는 등록 명령 전수를 실행 → undo 한 뒤 씬 해시가 같은지 본다.
- **E2 런타임 디버거 한 자리(5-1 W-E1 뒤):** AI 섹션 · 시퀀스 디버거 · 애니 리와인드 · HUD 섹션을 `IDebugSource` 등록표 하나로 묶는다. 선택 오브젝트에 연동하는 패널을 둔다(언리얼 Gameplay Debugger 자리).
- **G1 장르 수직 슬라이스(T1 · T2 뒤):** 완결 루프가 없는 게임마다 `slice.scenario.xml` 을 둔다(시작 → 목표 → 결과 → 저장 · 복원 해시 동일).

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
