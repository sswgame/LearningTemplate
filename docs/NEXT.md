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
- **Linux GL 프래그먼트 샘플러 한도:** `forward_lit` 이 Mesa(d3d12 · llvmpipe 둘 다 단계당 32)에서 "Too many fragment shader texture samplers" 로 링크되지 않아 GL 교체 단계(editor flowboundaries · flowterrain · previewswap)가 UnhandledLog 로 진다. 머티리얼 유닛 0..31 위에 엔진 텍스처 유닛 32..49 를 얹어 한 프래그먼트 셰이더가 32 를 넘는다 — 장치 검사(`OpenGLRHIDevice` 의 `requiredTextureUnit`)가 머티리얼 칸만 세어 이 백엔드를 받아들인다. 셰이더가 실제로 쓰는 샘플러 수로 검사하거나 유닛 배치를 줄인다.
- dzn 기본 드라이버로 처음 돈 체인(editor · empty)에서 진 단계: netresourcedigest(접속 클라이언트 0), sequencercuts · sequencerkeys(`SequencerUnboundCount`) — 소프트웨어에서도 지는지 `--gpu software chain editor` 로 가른다. `.linux` 골든은 dzn 으로 다시 뜬다(통합 담당).

### 3. 통합 담당이 직접 (실행 · 측정이 필요)

- **에디터 첫 패널의 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초. `-gv_traceSlowTickMs` 트레이스로 그 프레임의 동기 일을 찾아 비동기로.
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력), waterplay(강체 속도 · 잠듦 감시 값으로 조건 대기).
- **측정:** Tracy 에서 `GT.Frame` 자식 구간 합이 90% 이상인지(W-P0 계측 자동 부착이 들어왔다 — 모자라면 빠진 호출부에 구간), 유니티 배치 크기 12 · 24 · 48(Release 깨끗한 빌드), [DesignBacklog](DesignBacklog.md) S8 측정 표 7 행, 작업자 W25 가 남긴 GPU 비교(`gpu.timings` — `GPU.<프레임 패스>` · `RT.GPUScene.upload` · `RT.Light.uploadBytes` · `GT.GPUScene.build.viewTransparentOrder`, c05a3ee251 대 지금 — 이득이 노이즈 안이면 되돌린다).

### 4. Shooter3D 총 (사용자 지적)

- **소총 자세:** 권총 조준 클립으로 소총이 머리 옆에 들린다. 브랜치 `rifle-pose-wip`(5073ea9105)에 기반이 있다. 다음: Quaternius **Toon Shooter Game Kit**(묶음 페이지가 CC0 인지 먼저 확인)의 `Character_Soldier` 클립을 UBC 로 리타깃 → 몸 전체 사진 → 손 소켓 `Gun` 회전을 사진으로 맞춤 → 골든 다시.
- **걸을 때 총이 떨린다:** 원인 미확인. `-gv_shooterMotionTrace` 기록에 총 위치를 더해 손 · 카메라와 프레임별로 대조한다.

### 5. 작업자 · 설계 문서가 남긴 일

- **`game-gpu-includes` 를 타깃 게이트 껍데기로(진행 중):** 메시 GPU 버퍼는 `MeshRenderData`, 머티리얼 바인딩 쌍은 `MaterialRenderBinding.h` 로 뗐고 `TextureCache.h` 는 RHI · `Texture2D.h` 를 보지 않는다 — 게임 코드가 RHI 에 닿는 파일 43(`Client/` · `Editor/` 밖 20), 직접 include 는 `Client/` · `Editor/` 포함 0. 남은 잎(include 닫힘으로 센 것):
  - `Material.h` · `MaterialInstance.h` 의 `RHIRenderResource` 상속(빌린 텍스처 반환 통보) → 메시처럼 멤버 프록시로.
  - `MaterialTypes.h` 의 `RHIRenderState.h`(`RHIRenderStateOverride` · `RHIBlendTargetState` · `RHIBlendMode` · `RHIOpaqueDrawBand` → `RHIEnums.h`) · `RHIHandles.h`(`RHIDescriptorIndex`) — 저작 값이라 언리얼 `EBlendMode` 처럼 엔진 쪽 형식으로 두고 렌더러가 RHI 로 옮긴다.
  - `VFXEffectDef.h` 의 `AssetRef<Texture2D>` 가 `Texture2D::kAssetType` 완전 형식을 요구해 `Texture2D.h`(RHI) 를 include — 텍스처 에셋 종류를 GPU 객체와 떼거나 생성자를 .cpp 로.
  - `Client/` 만 닿는 둘: `CanvasDrawList.h`(`RHIScissorRect` · `shaderslot::kCanvasTextureCount` → 주인 상수 + static_assert), `RenderExtension.h`(`Games/Empty/Client/Dev/EmptyRenderDemo` 가 렌더 확장을 직접 짓는다 — 게이트는 타깃 단위라 폴더로 빼지 못한다: 데모를 DevTools · 에디터 확장으로 옮기거나 `[allow]` 로).
  그 뒤 TargetLayers `[gate]` 의 gameframework · kit · game 에 `Engine/Graphics/RHI` · `Engine/Graphics/Shader` 를 더하고 린트를 지운다.
- **에디터 MVVM 측정(통합):**
  - Hierarchy: 큐브 8000 루트 씬에서 Hierarchy 창을 연 프레임 시간을 옮기기 전 커밋과 대조한다(모두 접힘 · 루트 하나 펼침 · 맨 아래로 스크롤). 나빠졌으면 원인을 `HierarchyViewModel::gather` 에서 찾는다.
  - 인스펙터 값 행: 프로퍼티 많은 대상(컴포넌트 여럿을 단 오브젝트 · 프로젝트 설정 · 펼친 큰 배열)에서 인스펙터 프레임 시간과 `Editor.InspectorFilledRows` · `Editor.InspectorSkippedRows` 를 값 행 전 커밋과 대조한다(맨 위 · 맨 아래로 스크롤 · 스크롤 중). 나빠졌으면 `PropertyGridModel::gather` 의 행 채우기(값 사본 · 기본값 비교 · 혼합)에서 찾는다.
- **렌더링:** Vulkan placed 텍스처(해제 큐에 메모리 참조 세기), TransientLeasePool 을 뷰 풀에 잇기, 비동기 컴퓨트 큐 패스 타임스탬프 · Tracy 프레임 패스 내보내기, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단 외형의 디퍼드(G버퍼 칸 부족 — 포워드 전용으로 남김), 몸 머티리얼이 피부 층 표면 덮어쓰기(`_skinLayerSurfaceMap`)를 읽기, 텍스처 전이 · VFX · 프로브 디스패치의 버퍼별 전이를 묶음 API 로.
- **표면 변형:** 지형(`terrain.hlsl`)은 머티리얼을 픽셀에서만 읽어(GL 은 구조버퍼를 두 단계에서 못 읽는다) 정점 변위를 받지 못한다 — 엔진 텍스처 칸 `SurfaceDeform`(`shaderslot::emulated::kEngineTextureNames` 13 번째 — `SW_ENGINE_TEXTURE_COUNT` · DX11 `t44` · GL 유닛 12 · binding.hlsli 의 칸 줄)과 패스 상수 `g_SurfaceDeformParams`(창)를 더해 지형 정점 셰이더가 읽게 한다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 그림자 패스도 같은 오프셋). 네 백엔드 셰이더 · 바인딩을 함께 바꾸므로 빌드 · 시나리오를 돌릴 수 있는 작업자가 한 묶음으로 한다.
- **폴더 자리(막는 구조부터):** 엔진 쪽 `Dev/` 예외(`Config/Rules/Lint/FolderLayout.json` `_devFolderAllowed` — ModuleManager/Dev · Host/Dev · App/Dev · GF GameData/Dev · Graphics/Material/Dev)를 DevTools 로 — ModuleManager 를 공유 라이브러리로(언리얼 HotReload 처럼), DevTools 에 리플렉션 단계를 더하면 핫 리로드 · 머티리얼 그래프가 옮겨 간다.

### 5-1. 구조 · 성능 설계(2026-11-10 설계안 — 순서대로, 폴더가 겹치지 않는 작업자 단위)

- **W-S4 `GameObjectManager.h` 허브 해체:** 포함 TU 428 · include 30 이다. 소유 타입을 pimpl 로, 템플릿 전달은 `.inl` 로. `-ftime-trace` 집계로 재컴파일 TU · ms 를 전후 비교한다.
- **W-R1 남은 것(통합):** 세 TU 분할 · `frame_vector`(프레임 아레나 `FramePassExecutor::_frameArena`)는 들어왔다 — 빌드 뒤 RT 프레임 힙 할당을 재서 남은 지역 목록을 옮긴다(`drawTextureLayerBakes` 의 `TextureCache::takeLayerBakes( vector& )` 는 API 가 `vector` 라 남았다).
- **W-R2 PSO 배치 색인(측정 먼저):** 계측 `RT.PSO.batchLookups`(묶음마다 · 병합 판정의 CB 차이마다 PSO 조회 수)이 들어왔다. 그 수와 `RT.PSO.ensureMaterial` · `RT.Draw.gpuBatches` 를 재고, 이득이 있으면 배치를 만들 때 PSO 핸들을 한 번 풀어 두고 흔들림 안이면 하지 않는다.
- **W-E1 남은 것:** 훅 `IEditorPanel::onPanelOpened`(첫 사용처 카탈로그 창)이 들어왔다 — 3절 "첫 패널 느린 프레임" 측정(`-gv_editorPanelTimes`)이 고른 패널의 동기 준비를 이 훅의 비동기 잡으로 옮긴다(`ModulesModel::ensureLoaded` 가 후보).

### 5-2. RHI 진단(2026-11-10 — 결함 먼저, 폴더가 겹치는 단위는 순서대로)

- **DX12 기록 경로 락:** 바인딩마다 `shared_mutex`(`D3D12RHICommandContext::resolveBufferAddress` · `resolveOfflineView`), 배리어마다 `_resourceStateMutex` + `_mapOffscreenTexture.find`. 병렬 기록 구간의 레지스트리 불변식(`setParallelRecording`)으로 락을 걷어 내고, 텍스처 상태를 `RHIHandleTable` 레코드로 옮긴다. 등록표는 이미 `RHIBindlessRegistry`(`_srvRegistry` · `_uavRegistry` + `_bindlessIndexAllocator`)라 락만 바꾸면 된다. `bench.json` p50 으로 전후를 잰다.
- **측정 뒤 조건부(계측은 넣었다):** 묶음 검증의 bench 로 `RT.Bind.descriptorUpdates ÷ RT.Bind.descriptorSetFlushes`(Vulkan 드로마다 슬롯 세트 갱신)와 `RT.Bind.bindGroupCreates ÷ bindGroupHits`(WebGPU 프레임 캐시)를 읽는다. 갱신이 드로 수에 붙으면 동적 오프셋 · 푸시 디스크립터로, 다시 짓기가 많으면 바인드 그룹 캐시를 프레임 펜스(`RHIFrameFenceRing`)로 은퇴시킨다.
- **측정 뒤에만:** 중립 커맨드 스트림 · RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더(`RHIHandleKind`/`RHIPipelineStateDesc._kind` + ABI 범프). Android 는 이미지마다 `VkDeviceMemory` 하나라 `maxMemoryAllocationCount`(4096)에 닿을 수 있다 → 서브할당(추정).

### 5-3. 병렬이 기본인 오브젝트 · 물리 모델(2026-11-10 설계 — 하이브리드: 언리얼 Mass + 액터 자리)

GameObject/Component API 는 저작 · 수명 · 직렬화 층으로 두고, 프레임마다 도는 일은 읽기 · 쓰기 종류를 선언한 잡으로, 핫 데이터는 종류별 SoA(`SceneTransformStorage` 일반화)로 옮긴다. 완전 ECS 는 기각했다(컴포넌트 타입 229 · `getComponent<>` 593 · 리플렉션 · 프리팹 전부 재작성). 단계마다 빌드 · 테스트가 통과해야 하고, 이득이 흔들림 안이면 되돌린다.

0. **측정 기반(통합 담당):** `-workers=N` 인자 · Debug 카운터(`GT.Object.getComponentLookups` · `GT.Scene.tick.virtualTicks`)는 들어갔다. `load/*` 4 시나리오 × 워커 1 · 2 · 4 · 8 기준선을 S8 표에 적는다.
1. **일괄 틱 · 파도 · 레인 — 런타임은 들어갔다(`TickRegistry` · `SceneTickScheduler` · `TickBatchRegistry`, 테스트 `TickLanesTest`).** Batch 레인은 서브틱 · 여러 항목 오브젝트까지 파도(순서 키, 오브젝트 안 순번)로 일괄하고, Task 는 오브젝트마다 태스크, Main 은 게임 스레드다. 일괄 경로를 끈 레지스트리(`setBatchPathEnabled( false )`)가 동등성의 기준 길이다. 남은 것:
   - **생성기(5-4 작업자):** `REFLECT( TickMode = Batch|Task|Main )` 을 받아, `onTick` · `onSubTick` 을 재정의했거나 `TickMode` 를 단 타입마다 생성 TU 의 `namespace sw` 에 `SW_REGISTER_TICK_TYPE( 타입, 레인 )` 을 낸다(`TickBatchRegistry.h` 머리말이 계약). `TickBatchOf<타입>` 이 `onTick` · `onSubTick` 을 한정 이름으로 부르므로 보호 재정의는 `REFLECT_BODY` 가 친구로 연다. 손 등록은 `StressTestMoverComponent` 하나 — 생성기가 내면 지운다. 완료: Debug `virtualTicks` 가 0 에 가깝다.
   - **선행 조건 스테이지 일괄:** 스테이지 항목은 레인의 일괄 함수(비가상)로 돈다(`TickStage::_listItemFunction`). 남은 것은 같은 함수의 이웃 항목을 한 번에 넘기도록 레벨 안을 (타입, 서브틱 id) 로 묶는 것 — 스테이지 편집(`flushStageEdits`)이 항목 순서를 가정하므로 그 정렬 규칙과 함께 바꾼다.
   - **Task 의 선행 조건 간선:** 선행 조건을 건 항목은 지금 그룹 끝 스테이지(Kahn 레벨 장벽)로 간다. 간선만 기다리는 개별 태스크로 바꾸면 파도 · 레벨 장벽에 묶이지 않는다(언리얼 TickFunction 선행 조건 자리).
   - **판정 숫자(통합 담당):** 구조 지표 `GT.Scene.tick.waves`(장벽 수) · `batchElements` · `taskEntries` · `mainEntries` · `objectEntries` 를 부하 시나리오에서 읽어 S8 표에. 벽시계는 오브젝트 8000 이상에서만.
2. **잡 컨텍스트 형식 — 프레임 시스템은 들어갔다:** `ISceneMainFrameSystem::runMain` / `ISceneLaneFrameSystem::runLane( SceneJobContext& )`(매니저 읽기 전용 + 틱 뒤 큐), `SceneDataKindSet` 64 비트. 남은 것: (a) **컴포넌트 틱의 컨텍스트** — Batch 레인의 `onTick` 도 같은 형식(읽기 전용 남의 오브젝트 + 명령 버퍼)으로 받아 남의 컴포넌트 직접 쓰기(결함 후보 2 · `setCanEverTick` 비트필드)를 컴파일에서 막는다. 102 개 타입의 서명이 바뀌므로 생성기(5-4)의 `TickBatchOf` 호출과 함께 옮긴다. (b) `SW_SCENE_SERVICE` 의 접근 선언 필수화 — 지금 접근 선언은 프레임 일(`SW_SCENE_SERVICE_WITH_FRAME_JOB`)만 읽으므로, 단계 본문의 접근을 쓰는 곳(서비스 병렬화)을 먼저 정해야 의미가 있다.
3. **물리 Export · 지연 이벤트:** `PhysicsPoseStore` 이중 버퍼를 병렬로 읽고 `applyTransformBatch` 로 일괄 쓴다(지금은 바디마다 백엔드 3 호출 + 세터). 충돌 배달은 `Physics` 단계 안 동기 가상 호출 대신 `FlushPostPhysics` 서비스로.
4. **물리 훅 배치:** Jolt 조임(인라인 실행 폴백 삭제 · `WaitForJobs` 돕기)은 들어갔다. 물리 훅 4 종을 배치 함수 + 명령 저장소로.
5. **`ComponentDataStore<T>`:** 트랜스폼 SoA 페이지를 템플릿으로 일반화한다. 첫 사용자는 강체 자세 · 명령, 투사체.
6. **엔진 공간 색인:** `SceneSpatialGrid` 서비스(월드 버전으로 증분, 이중 버퍼 읽기). GF `InteractionSpatialIndex` · `ZoneOccupantIndex` 는 읽기만. `TagIndex` 는 세대 스냅샷으로.
7. W-S4 허브 해체(5-1).

병렬 작업자: B(3 · 4, `Engine/Physics/**`), C(2), D(6, C 뒤), 5-4 작업자(1 의 생성기). 결함 후보 둘은 확인했다 — 중첩 `runParallel` 의 writerID 0 은 고쳤고(`TickThreadScope` 가 들어올 때의 상태로 되돌린다), `setCanEverTick` 비트필드는 틱 중 쓰기가 `deferIfStructureFrozen` 으로 미뤄져 지금은 경쟁이 없다(틱 밖에서 워커 레인이 남의 컴포넌트에 쓰면 다시 열린다 — 2 단계 형식이 막을 자리).

### 5-4. 리플렉션(2026-11-10 설계 — 규모: REFLECT 1,408 · PROPERTY 6,708 · ENUM 413, 생성 TU 가 전체 컴파일 CPU 의 20 %(Debug) · 17 %(Release))

조회 핫패스(`StaticType` · `castTo`, 1~2 ns)는 상용 수준이다. 비용과 결함은 등록 쪽과 생성 코드의 무게에 있다.

- **됨(작업자 브랜치, 통합 빌드 · 검증 전):** W-R0 계측(구간 · 모듈마다 시간 · Reflection 바이트 · 상속 캐시 비움 수 로그, `Scripts/dev/BuildTimeReport.py` — 지금 Debug 생성 TU 17.4 %), W-R1(바뀐 타입과 자손만 비우고 배치 끝에 한 번 다시 짓기, 상속 목록 = 선언 타입 원소의 포인터 뷰 `PropertyListView`, 등록이 짓고 조회는 읽기만, `_onPropertyBoundChanged` 삭제), W-R3 (c) 특성 비트 · `REFLECT( TickMode )` · 생성된 `SW_REGISTER_TICK_TYPE` 등록(StressTestMover 손 등록 삭제), (d) 경고 → 생성 오류(클래스 템플릿 · 멤버 함수 템플릿 · 없는 ENUM 센티널 · `#if` 아래 리플렉션 매크로), (b) `Units` 열거화 · `Default` 는 칸 타입 리터럴의 생성 대입(`_pApplyDefault`) · `EditCondition` 은 생성된 판정(`_pEditCondition`, 기반 없는 타입의 없는 이름은 생성 오류 — 런타임 파서 삭제), (e) `PROPERTY( RequiredAtRoot )` + `InheritParent`(판정 `isRequiredValueMissing( …, RequiredScope )` 한 곳) · `CultureInfo` 의 `<culture>` 속성을 REFLECT 스키마(`XMLFields`)로.
- **남은 일(순서대로):**
  - W-R1 나머지: 보관처를 (타입, 색인, 세대)로 — 지금 포인터는 선언 타입 자신이 다시 등록(핫 리로드) · 해제될 때만 매달린다. 판정 비트(POD · 세이브 · 검증)는 등록이 미리 구하지만 아직 `mutable` 지연 자리가 남았다(검증 판정이 칸 타입을 재귀로 묻는다).
  - W-R3 (a) 정적 서술자 표(constexpr, 해시 사전 계산, 컨테이너 래퍼 정적 객체 — 목표 등록 할당 타입당 3 이하, 생성 바이트 4 MB 미만). 지금 생성 모양: 프로퍼티마다 즉시 호출 람다가 `PropertyInfo` 를 짓고 `_metadata` 상자에 대입한다(`CodeGenerator::emitPropertyInfoEntry` · `emitPropertyMetadata`). (d) 나머지: 같은 모듈 리프 이름 겹침 — 파서가 증분이라 모듈 전체를 못 본다(모듈 목록 단계에서 보거나 런타임 `Reflection.TypeShortNameCollision` 을 같은 모듈이면 오류로). (e) 나머지: `CultureInfo` 의 부모 병합(부모 값 복사 → 적힌 칸 덮기)과 자식 요소(규칙 · 패턴 · 목록) · `pseudo` 는 손 읽기다 — 자식 요소를 리플렉션 컨테이너 칸으로 옮기려면 `<item>` 목록 · 범주 속성 규칙 묶음의 코덱이 필요하다. 구분 기호 · 패턴의 C++ 초기값은 표 없는 불변 문화권(`CultureInfo{}` 폴백 — `LocalizationManager` · `LocText`)이라 린트 예외 표에 두었다. 그 폴백을 없애면(문화권을 못 찾으면 오류) 초기값을 지우고 `RequiredAtRoot` 로 바꾼다.
  - (b) 확인 남음: 생성기를 실제 헤더에 돌린 결과(`_pApplyDefault` · `_pEditCondition` 줄)와 파서 사례 3 개(`DefaultOnContainer` · `EditConditionUnknownName` · `RequiredAtRootWithoutParent`)는 통합 빌드에서 본다. `Default` 를 단 문자열 칸이 `const char*` 대입을 받지 않는 타입(명시 생성자)이면 생성 코드가 컴파일되지 않는다 — 지금 `Default` 4 곳은 int · string 이다.
  - W-R5 빌드 시간: 헤더당 `.gen.cpp` 대신 모듈 안 8~16 헤더 묶음 TU(목표 생성 비중 8 % 미만 — `BuildTimeReport.py` 로 잰다).
  - W-R4 `ReflectVisit` 하나(직렬화 9 루프 · 검증 · 문서 · 인스펙터 gather · 상태 해시 기본 구현). `foldStateHash` 손 구현 대체는 해시 구성이 바뀌므로 상태 해시 동등성 판정(T1 · 5-3)과 같은 묶음에서.
  - W-R6 밀집 타입 색인: 5-3 의 5 단계(`ComponentDataStore<T>`)와 함께만.
- **5-3 과 맞춘 자리(병합함):** 생성기는 `onTick` · `onSubTick` 을 직접 재정의한 컴포넌트와 `REFLECT( TickMode = … )` 를 단 컴포넌트에 `SW_REGISTER_TICK_TYPE( 타입, 레인 )` 을 낸다(`CodeGenerator::emitTickBatchRegistration` 한 곳, 메타데이터가 없으면 Batch). `TickMode` 는 `TickRegistry.h` 하나, 재정의 비트는 `TypeMetadata::_bOverridesTick` · `_bOverridesSubTick` · `_bOverridesSerializeState`. `REFLECT_BODY` 가 `TickBatchOf` 를 친구로 연다.

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
  - 이력 · 격리 표는 들어왔다(`Scripts/dev/TestHistory.py` ingest · report · quarantine, `Config/Rules/Test/Quarantine.json` — 사유 · 만료 필수). 남은 일: 묶음 끝 스크립트가 보고 폴더를 `ingest` 하고 ctest 에 `quarantine --ctest-exclude` 를 `-E` 로 넘긴다(bundle-verify 스킬의 절차와 함께 고친다).
  - 샤드를 시간 균형으로 나누고, 묶음 끝 셸 스크립트를 단계 · 산출물 · 재시도가 적힌 파이프라인 정의로 옮긴다.
  - 외부 CI 러너는 비용이 드는 연동이라 사용자 결정이다. 범인 찾기(`ScenarioCulprit.py` · `LinuxScenarios.py culprit`)는 실행마다 보고 폴더의 JSON 으로 판정한다 — 처음 한 번 돌려 확인한다.
- **D1 데이터 필수 칸 린트:** 발 뼈 결함의 모양이다. `PROPERTY` 의 문자열 · `hashed_string` · `AssetID` 칸이 비어 있지 않은 C++ 기본값을 갖고 `Required` 가 아니면 오류로 낸다. 빠진 칸은 로드 오류 id(`Reflection.RequiredMissing`)로 드러낸다.
- **N2 서버 배포 — 통합 검증만 남음:** Linux 서버 패키지로 `py -3 -m Scripts server-image --package … --config … --game …`(묶기 전 `Server -check-config` 를 WSL 로 돈다) 한 번, 쿠버네티스 예시는 비밀 `ops-token` 을 더해야 뜬다(운영 포트 0.0.0.0 은 토큰 필수 — 스크레이퍼도 Bearer).
- **U1 햅틱 남은 것:** 출력 `IInputFeedbackDevice` · 데이터 `*.haptic.xml`(`HapticEffectDef`, 공유 캐시) · 재생 `HapticPlayback`(게임패드가 든다, XInput · evdev 백엔드 그대로)은 들어갔다 — 통합에서 `HapticEffectTest` 를 돌린다. 방아쇠 진동(DualSense · GameInput)과 터치 기기 진동(Android Vibrator)은 그 백엔드가 생길 때 같은 출력으로 건다. 패드 메뉴 방향 포커스는 이미 있다(`UINavigationSolver` · `UIInputRouter`).
- **A1 애니메이션 포즈 노드(5-3 뒤):** 등록표(`AnimationPoseNodeRegistry`)는 들어왔다. 그 위에 Mirror · Montage(섹션 · 분기)를 등록 한 줄씩 더한다(평가 경로가 5-3 에서 정해진 뒤).
- **R1 에셋 리다이렉터 — 됨(작업자 브랜치):** 도메인 뿌리마다 `redirects.xml`(옛 → 새), `AssetIO` 가 경로 교체보다 먼저 푼다(`AssetRedirect`), 사슬은 접고 순환 · 중복은 기동 오류, 린트 `asset-references` 가 남은 옛 참조를 보고한다. 남은 일: 에디터 옮기기 · 이름 바꾸기가 표에 줄을 더하게(`move-asset`).
- **N1 복제 스키마를 리플렉션에서(5-4 뒤):**
  - 엔티티 상태 바이트를 게임이 손으로 양자화한다. 그래서 칸 하나를 더하면 서버 · 클라 두 곳을 고쳐야 한다.
  - `PROPERTY( Replicated, Quantize = … )` 로 작성기를 생성하고, 스키마 해시를 와이어 버전에 합친다(남음 — W-R3 (a) 서술자 표 위에 얹는다).
  - 됨: 상한(`kMaxEntityBytes` · MMO `kMaxStateBytes`) 초과는 경고가 아니라 오류다(`ReplicationServer::setEntity` · `MMOReplicator::setEntity`, 첫 번째만 남기고 횟수를 센다).
- **E1 되돌리기 남은 것(통합에서):** 씬 명령 · 완료 함수(이름 확정 · 컴포넌트 더하기 · 프리셋 · Keep Simulation Changes)는 `EditorTransaction&` 를 받는다. 시나리오 `editor/undocoverage` 를 돌려 실패 줄의 명령을 고친다 — 컴포넌트 대상 명령(`SceneDuringPlay`)은 선택만으로는 꺼져 건너뛰므로 컴포넌트 대상을 주는 줄을 더한다. 굽기는 파생 데이터라 기록하지 않는다(결정, `EditorCommandEdit`).
- **G1 장르 수직 슬라이스 — 통합 실행만 남음:** 게임 12개에 `automation/slice.scenario.xml`(목표 단계는 그 게임 시나리오의 사본 + `game.save` → 두 번 `game.load` 뒤 `<RecordStateHash>` · `<ExpectStateHash>`)이 들어왔다. 체인에서 처음 돈다 — 진 게임은 목표 사본(원본 시나리오)과 복원이 되살리지 못한 오브젝트(`state.objects`)를 본다. velmora 는 `vale` 이 슬라이스다.

### 6. 리소스

- **Sprite Fusion 타일셋**(상세 페이지마다 `License: CC0`): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer(Kenney)는 Kenney 원본에서.
- **Sprite Fusion VFX 25 종**(MIT — 저작권 고지를 credits 에): 캔버스에 그리는 TypeScript 코드다 → 브라우저 MCP 로 재생해 스프라이트 시트로 굽는다.
- **PixelFX Piper**(무료 브라우저 효과 생성기): 이용 조건을 먼저 확인해 credits 에, 기본 효과를 시트로 내보내 층 쌓기 기법으로 효과를 만든다.

### 7. 그 뒤

- Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기).
- 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 보기. 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 보기.
- 남은 단위 테스트: `py -3 Scripts/dev/TestCoverage.py` 의 "없음" 폴더, 온라인 `AccountTicketConnect` 의 만료 · 재시도 길(성공 길은 `net.ticketconnect` 시나리오가 본다).
- 흔들리는 게임 골든: 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.
- **Linux**(큰 일이 모두 끝난 뒤 — 사용자 결정): `LinuxScenarios.py all`, `.linux` 골든(dzn 기준), 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심). lavapipe `blendEnable` + `R16G16_SNORM` 은 `VulkanRHIResourceFactory` 가 이제 막는다 — 재현해 확인.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스 규칙(2026-11-09):** 받아 오는 리소스는 무료 · 공개 라이선스(CC0 우선, MIT 처럼 재배포가 되는 것은 고지와 함께)만 — 묶음 페이지의 라이선스를 확인해 credits 에 URL · 날짜. 출처 하나에 고집하지 않고 뼈대가 달라도 리타깃한다. 에이전트가 리소스를 직접 그리지 않는다. PixelFX Piper(무료)는 쓴다. Agent Sprite Forge 는 도입하지 않는다(유료 생성 API 필요). Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌었다 — 새 묶음은 그 묶음 페이지가 CC0 일 때만. 원본 바이너리는 Git LFS, 받아 둔 팩은 `/AssetLibrary/`(무시)에 두고 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/` 로.
- 범용 스크립팅 언어는 도입하지 않는다(실제 요구가 확인될 때만 — [판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤, GC 는 권하지 않는다.
- 웹은 wasm32 다(유니티 · Godot 웹과 같다 — memory64 는 Safari 미지원 · 경계 검사 비용). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓰고 스스로 다시 짓는다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`(시연 모드는 테스트 자료). App 의 `Dev/` 는 남긴다(배포에 쓰지 않는 인터페이스를 두지 않는다).
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
