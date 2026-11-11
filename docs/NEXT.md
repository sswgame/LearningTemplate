# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 본다. 끝난 일은 git 기록에, 지킬 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다 — 여기에 되풀이하지 않는다. 끝나면 줄을 지운다.

## 일하는 법

절차는 스킬이 정본이다: 묶음 끝 검증 · push `bundle-verify`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계 · 시간 기록은 `Test/README.md`. 사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset] [--backend] [--stages a,b] [체인…]`.
병렬 작업자 워크트리는 main 에서 fast-forward 한 뒤 시작한다(옛 기준에서 시작한 갈래는 통합 때 수십 파일이 충돌했다).

## 상태 (2026-11-11 09시)

- main = 091bd5b3e9. 이 커밋의 묶음 검증(VerifyBundle `--show`)이 돌던 중에 세션을 넘겼다 — Debug 빌드 · 단위 · ctest 51/51 통과, Release 빌드 경고 0, Release ctest 진행 중이었다. 출력은 그 세션 스크래치패드의 `verify26.out`, 로그는 `build/<프리셋>/Testing/Logs/verify.*`. 끝 줄이 없으면 `--from release` 로 다시 돈다.
- origin/main 이 09:03:45 에 091bd5b3e9 로 push 돼 있다(`git reflog show origin/main`) — 검증 스크립트는 push 하지 않으니 누가 했는지 확인하지 못했다. 검증이 지면 고친 커밋을 다시 push 한다.
- 통합 브랜치 `integ2`(워크트리 `.claude/worktrees/integ2`, ea4913f69c — main 보다 46 커밋 앞, 빌드 한 번도 안 함): 에셋 정체성 1단계(AssetGUID · .meta · asset-ids 도구 · 팩 GUID 색인), 5-3 틱 const 닫기, W-R3 (a) 코덱 키 검사, A1 포즈 노드 평가 · 미러 표 · 에디터 노드 더하기, R1 리다이렉트 표 팩 담기, 온라인 · 원장 · 신고 단위 테스트들. 1 절 첫 줄.
- Android(Android-Shipping)는 대상 전체가 컴파일 · 링크되고 Vulkan 셰이더 쿠킹이 지난다. 웹(Web-Shipping)은 App.wasm 까지 지어진다.

## 남은 일

### 1. 묶음 검증

- **integ2 통합:** main(091bd5b3e9 검증 통과 뒤)을 integ2 에 합친다 → 워크트리에서 Debug 전체 프리셋 빌드(App 만 지으면 게임 모듈 헤더 불일치로 기동 실패) → 컴파일 오류는 `.inl` include · `extern template` 로(`-Werror=undefined-func-template` 를 끄지 않는다) → 단위 · 영향 시나리오 → main 에 합쳐 VerifyBundle 한 번 → push. 에셋 GUID 2 · 3 단계(GUID 로 참조 해석 · 경로 참조 이전)는 그 뒤 — 설계는 [DesignBacklog](DesignBacklog.md) 에셋 정체성 절. 작업자 커밋에 GameObjectManager.inl 머리말 문장 · `EditorTransaction` 을 연 채 CommandStack 을 읽는 자리가 남았다.
- **이번 세션이 남긴 것:** 런타임 파생 오브젝트는 `GameObject::setTransient(true)` 로 상태(세이브 · 상태 해시)에서 뺀다 — 외형 부품만 걸었다. 다른 런타임 생성자(VFX · 투사체 풀 · 군중 프록시 등)도 상태에서 다시 세우는 파생이면 같은 표시를 건다(`slice` 게임에서 ExpectStateHash 가 이제 다른 오브젝트 이름을 말한다).
- 흔들리는 단위 테스트 `AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`: 전체 실행 한 번에 "base 'testproject/base.audiograph.xml' could not be read" 로 졌다. 같은 씨앗 · 단독 1800 회로는 재현되지 않았다 — 다시 지면 그 로그의 앞 케이스와 스크래치 도메인 마운트 상태를 본다.
- 5-4 리플렉션 작업자 둘(서술자 표 · 물리 훅 · 판정 비트 · PropertyHandle · 묶음 TU · 메서드 · 이벤트 표)은 빌드하지 않았다 — 앞 작업자의 프로퍼티 표 · 물리 훅 생성 코드는 워크트리 부분 Debug 빌드(4384 중 약 3900 단계, 실패 0 — Engine · GameFramework 의 모든 `.gen.cpp` 포함)에서 컴파일됐고, 그 뒤 것은 짓지 않았다. 통합 빌드에서 볼 것: 묶음 TU(`ReflectionBundle.N.cpp` — 헤더끼리 한 TU 에서 부딪히는지, Windows 매크로 오염)와 `BuildTimeReport.py` 생성 비중(전 30.7 %, 목표 8 % 미만), 메서드 · 이벤트 표(`kMethods` · `kEvents` — 상태 없는 람다의 상수 초기화, `ReflectDefaultCtorOf`), 생성 `.gen.cpp` 합계(전 7.86 MB), 판정 비트를 구하지 않은 사본에 묻는 단언(`_bDerivedFlagsBuilt`), 단위 `PhysicsHookRegistryTest.*` · `ReflectionTypeRegistryTest.PropertyHandleGoesStaleWhenItsDeclaringTypeChanges` · `ReflectionTypeInfoTest.MethodTableBuildsMethodList` · `ReflectionPropertyRoleTest.DerivedFlagsAreBuiltByRegistrationNotByFirstQuery` · `ReflectionTypeInfoTest.PropertyDescTableSharesDefaultMetadata`, 모듈 목록 단계의 리프 이름 겹침 오류(`ReflectionPipeline.LeafNameCollision`).
- 통합 때 확인하지 못한 동작: Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯(실장치), DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(이제 능력 참 — 검증 레이어 경고 없이 트랜지언트 첨부가 힙에 놓이는지 `RT.Frame.transientPlacements`, lavapipe · Android 포함).
- RHI 5-2 묶음(Vulkan 기록 경로 락 제거 · 텍스처 전이 묶음 ABI v54 · 컴퓨트 큐 타임스탬프 ABI v55 · 렌더 상태 이름)은 빌드하지 않고 넘겼다: Debug · Release 컴파일, RHI 단위 테스트(`RHITrackedStateTest` 포함), lightculling · shadow_* · postprocess_* 시나리오 dx12 · vk(Vulkan 트랜지언트 풀 · placed 텍스처가 처음 실제로 돈다), DX12 정보 로그 `D3D12.ComputeTimestampClock` 의 어긋남 µs 와 Vulkan 비동기 컴퓨트 프레임 패스의 `GPU.<패스>` 값 · Tracy GPU 타임라인 겹침을 본다.
- 통합 검증에서 함께 본다: 게이트 껍데기(`<빌드>/gates`) 생성 · 게임 타깃 · 에디터 확장(`editorextension` — CharacterEditor 의 RHI include 를 끊었다)의 RHI · Shader 게이트, 5-4 생성기 출력(`_pApplyDefault` · `_pEditCondition` 줄, 파서 사례 `DefaultOnContainer` · `EditConditionUnknownName` · `RequiredAtRootWithoutParent`), `HapticEffectTest`, 시나리오 `editor/undocoverage`(실패 줄의 명령을 고친다 — 컴포넌트 대상 명령은 대상을 주는 줄을 더한다), G1 `slice` 12 게임(진 게임은 복원이 되살리지 못한 `state.objects` 를 본다), T2 격리(`AutomationUserDataRootTest` · `engine.isolation` · `Scenario.editor.statedir.*` · `net-pair mmo`), N2 `server-image` 한 번. 지형 표면 변형(엔진 칸 `SurfaceDeform` · `g_SurfaceDeformParams`, 빌드 · 셰이더 쿠킹을 돌리지 않은 갈래): 네 백엔드 `game.empty.terraindeform` 골든 `terraindeform_on` 첫 기록 · 눈 확인, `surfacedeform` · 지형 쓰는 골든(envshowcase · waterplay · surfacelibrary — 지형이 깊이 프리패스에서 빠지고 그림자를 머티리얼 셰이더로 그린다)이 그대로인지.

### 2. 플랫폼 ([PlatformTargets](PlatformTargets.md) "남은 일")

- **Android:** `CookAssets`(호스트 빌드로 굽기) 통과 → `py -3 -m Scripts android-apk --game <게임>` → 실기기에서 기동 · 회전(pre-rotation 방향) · APK 팩 읽기 · 터치. JNI 클립보드(`AndroidClipboard.cpp` 는 지금 `isAvailable` false).
- **웹:** 통합에서 Web-Shipping 빌드(쿠킹이 단계 텍스처 16 계약을 처음 건다 — 넘는 WGSL 셰이더가 나오면 역할 칸으로 줄인다) → `py -3 -m Scripts web-check --game Empty`(실패는 `<묶음>/check/` 로그 · 화면). Core 단위 테스트 `WebSocketProtocolTest` · `WebSocketAcceptTransportTest`. 남은 것: 입력 · 클립보드 판정, `wss://`, 게임 세션(UDP)의 브라우저 길 — [PlatformTargets](PlatformTargets.md) "남은 일".
- **Linux:** `.linux` 골든을 dzn 으로 다시 뜬다 — 저장소에 커밋된 `.linux` 골든이 0 이다(`py -3 Scripts/dev/ScenarioCulprit.py --golden-coverage`, 데스크톱 WebGPU 도 0)(shooter3d · empty.boat · wildwest mount …), themepark 체인 멈춤, dzn 에서 진 단계 netresourcedigest · sequencercuts · sequencerkeys(`--gpu software chain editor` 로 가른다), 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심), lavapipe `blendEnable` + `R16G16_SNORM` 재현 확인.

### 3. 통합 담당이 직접 (실행 · 측정이 필요)

- **에디터 첫 패널의 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초. `-gv_editorPanelTimes` · `-gv_traceSlowTickMs` 로 그 패널의 동기 준비를 찾아 `IEditorPanel::onPanelOpened` 의 비동기 잡으로(`ModulesModel::ensureLoaded` 가 후보).
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력), waterplay(강체 속도 · 잠듦 감시 값으로 조건 대기).
- **측정(S8 표):** Tracy `GT.Frame` 자식 구간 합 90% 이상, 유니티 배치 크기 12 · 24 · 48, `load/*` 4 시나리오 × 워커 1 · 2 · 4 · 8 기준선과 구조 지표(`GT.Scene.tick.waves` · `batchElements` · `taskEntries` · `mainEntries` · `objectEntries` · `stageLists`), W25 GPU 비교(c05a3ee251 대 지금 — 노이즈 안이면 되돌린다), W-S4 `-ftime-trace` 재컴파일 TU · ms 전후, W-R1 RT 프레임 힙 할당, W-R2 `RT.PSO.batchLookups` · `ensureMaterial` · `RT.Draw.gpuBatches`(이득이 있으면 배치 때 PSO 핸들을 풀어 둔다), 5-2 `RT.Bind.*` 비율, DX12 기록 경로 락 제거(bindless 읽기 표 · 레코드 안 원자 상태) 전후 `bench.json` p50, 에디터 MVVM(Hierarchy 큐브 8000 · 인스펙터 값 행 — 옮기기 전 커밋 대비), 골든 여유(`ScenarioCulprit --golden-margins`).

### 4. Shooter3D 총 (사용자 지적)

- **소총 자세 클립(선택):** 지금은 리그 IK 견착(`ubc_player_body.rig.xml` TwoHandHold). Toon Shooter Game Kit 구글 드라이브는 2026-11-11 에도 "Quota exceeded". 대체 CC0 출처: poly.pizza "SWAT"(Quaternius, CC0 1.0, `https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb` — `Idle_Gun` · `Idle_Gun_Pointing` · `Idle_Gun_Shoot` · `Gun_Shoot` · `Run_Shoot`, 손가락 뼈 있음, 뼈 이름 Hips · Chest · UpperArm.L …). 판단: 상용(Lyra · ALS)은 쥔 자세 클립 + 왼손 IK 라 들이는 쪽이 길게 낫다 — `models_raw/quaternius` 에 넣고 UBC 로 리타깃해 상체 층(Aim)으로 쓰고, IK 견착은 손 자리 보정으로 남긴다. 실행으로 사진을 봐야 해서 이번에 하지 않았다.

### 5. 작업자 · 설계 문서가 남긴 일

- **렌더링:** TransientLeasePool 을 뷰 풀에 잇기, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단 외형의 디퍼드(G버퍼 칸 부족 — 포워드 전용으로 남김), 몸 머티리얼이 피부 층 표면 덮어쓰기(`_skinLayerSurfaceMap`)를 읽기, VFX · 프로브 디스패치의 버퍼별 전이를 묶음 API 로. DX11 postprocess_ssr 이 체인 안에서만 골든과 16.1% 다르다(단독은 0% — 앞 단계 상태, 원래 있던 결함; `ScenarioCulprit.py empty postprocess_ssr --backend dx11` 로 범인 단계를 찾고 두 사진의 차이 자리를 본다 — 가설은 에뮬 엔진 칸이 이번 패스에 걸지 않은 역할의 옛 텍스처를 그대로 들고 있는 것, 아직 확인 안 됨).
- **폴더 자리(남은 둘, `Config/Rules/Layers/DevFolders.json` — App/Dev 는 사용자 결정으로 남는다):** 핫 리로드 · 호스트 개발 계층은 DevTools 로 옮겼다(ModuleManager 공유 라이브러리 · `ILiveReloadManager`, 662582fd9c — 빌드 확인은 통합에서). 남은 길은 정했다.
  - Graphics/Material/Dev → `DevTools/Material/`: DevTools 에 `sw_addReflectionStep(DevTools MODULE …)`, 셰이더 쿠커(`ShaderCookDriver` 의 `MaterialGraphGeneratedPaths::refreshAll` 한 줄)는 엔진 창구(쿠킹 전 셰이더 소스 생성기 — 정적 등록, 개발 쿠킹인데 없으면 멈춘다)로 뒤집고, `SW_API` → `SW_DEVTOOLS_API`. 막힘: 데이터 상속(`_base`) 매크로가 등록 모듈 "Engine" 을 단언한다(`DataInheritance.h` — 팩 쿠킹이 모듈 등록을 못 본다) — DevTools 는 호스트 라이브러리라 "Engine" 으로 걷히므로(`registerHostLibraryTypes`) 그 단언을 "호스트 라이브러리" 로 넓힌다. 빌드하며 옮긴다.
  - GF GameData/Dev → GameFramework 층의 개발 전용 공유 모듈 `GameFrameworkDev`(언리얼 플러그인 모듈 Type `DeveloperTool` 자리): 매니페스트에 개발 전용 표시를 두고 배포 CMake 는 그 매니페스트를 짓지 않으며, 개발 호스트는 GameFramework 다음에 `loadSharedModule` 로 올린다. DevTools 는 GameSettings 를 볼 수 없어(게이트) 옮기지 않는다.
- **RHI(5-2):** DX12 기록 경로 락 — 바인딩마다 `shared_mutex`(`resolveBufferAddress` · `resolveOfflineView`), 배리어마다 `_resourceStateMutex` + `_mapOffscreenTexture.find` 를 병렬 기록 구간의 레지스트리 불변식(`setParallelRecording`)으로 걷어 내고 텍스처 상태를 `RHIHandleTable` 레코드로(`bench.json` p50 전후). 측정 뒤에만: Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, 중립 커맨드 스트림 · RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, Android `VkDeviceMemory` 서브할당.

### 5-3. 병렬 오브젝트 · 물리 모델 남은 것

GameObject/Component API 는 저작 · 수명 · 직렬화 층, 프레임 일은 접근을 선언한 잡, 핫 데이터는 종류별 SoA(완전 ECS 는 기각). 단계마다 빌드 · 테스트가 통과해야 하고, 이득이 흔들림 안이면 되돌린다.

- **컴포넌트 틱의 컨텍스트 남은 틈:** 주 틱은 `onTick( TickContext& )` 로 옮겼다. 틱 본문 45 곳이 아직 `getOwner()->getManager()`(쓰기 가능)를 꺼낸다 — 읽기는 `context.getManager()`(const), 쓰기는 `context.deferPostTick` 로 옮긴 뒤 틱에서 쓰기 가능한 매니저 길을 닫는다(빌드 고리가 필요). `onSubTick` 도 같은 컨텍스트로.
- **서비스 갱신 병렬화:** 같은 단계 서비스 갱신을 접근이 겹치지 않는 것끼리 워커로 — 본문의 게임 스레드 전용 API 를 서명으로 먼저 가린다.
- **SoA 다음 사용자:** 캐릭터 컨트롤러 3D · 2D 입력(`_commandLock` + 이동 · 점프 · 루트 모션 대기)을 `ComponentDataStore` 페이지로, 군중(`NavAgentCrowdSync`)의 에이전트별 `setWorldPosition` 을 `queuePoseWrite` · `applyTransformBatch` 일괄 쓰기로.
- **공간 질의:** 근접 기믹 · 감지 · 넷 관심 격자를 `SceneSpatialGrid` 층으로 옮길지 측정 뒤.

### 5-4. 리플렉션 남은 것

- W-R3 (a) 남은 것: 고정 배치형 코덱 본문(`fixedWrite` · `fixedRead` · `fixedEquals`)이 아직 타입마다 코드다 — 필드 서술자(오프셋 · 종류 · 경계)를 해석하는 공용 코덱으로(생성 바이트 목표 4 MB 미만은 그 뒤 다시 잰다). (e) `CultureInfo` 의 부모 병합 · 자식 요소를 리플렉션 컨테이너로(`<item>` 코덱 필요) — 불변 문화권 폴백을 없애면 초기값을 지우고 `RequiredAtRoot` 로. 에디터 레이아웃 캐시(`InspectorLayoutCache` 의 `const PropertyInfo*` 키)는 핫 리로드 때 비우는지 확인한 뒤 `PropertyHandle` 로.
- W-R4 `ReflectVisit` 하나(직렬화 9 루프 · 검증 · 문서 · 인스펙터 gather · 상태 해시 기본 구현) — `foldStateHash` 대체는 상태 해시 동등성 판정과 같은 묶음.
- W-R6 밀집 타입 색인: 5-3 SoA 와 함께만.
- N1 복제 스키마: `PROPERTY( Replicated, Quantize = … )` 로 작성기 생성, 스키마 해시를 와이어 버전에(W-R3 (a) 위).

### 5-5. 상용 대비 고도화 남은 것

- **T3:** 샤드 시간 균형, 파이프라인 정의로. 외부 CI 러너는 사용자 결정.
- **U1 햅틱:** 방아쇠 진동(DualSense · GameInput) · Android Vibrator 는 그 백엔드가 생길 때.
- **A1 포즈 노드:** Mirror · Montage(섹션 · 분기)를 `AnimationPoseNodeRegistry` 에 한 줄씩.
- **R1 리다이렉터:** 배포 팩이 도메인의 `redirects.xml` 을 담는지 본다 — 쿠킹 설정(`Config/Engine/PackConfig.json` 등)에 그 이름이 없어 Shipping 에서 표가 빠질 수 있다(팩 도달 계산은 참조로만 담는다).

### 6. 리소스

- **Sprite Fusion 타일셋**(상세 페이지마다 `License: CC0`): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer(Kenney)는 Kenney 원본에서.
- **Sprite Fusion VFX 25 종**(MIT — 저작권 고지를 credits 에): 캔버스에 그리는 TypeScript 코드다 → 브라우저 MCP 로 재생해 스프라이트 시트로 굽는다.
- **PixelFX Piper**(무료 브라우저 효과 생성기): 이용 조건을 먼저 확인해 credits 에, 기본 효과를 시트로 내보내 층 쌓기 기법으로 효과를 만든다.

### 7. 그 뒤

- Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기).
- 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 보기. 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 보기.
- 남은 단위 테스트: `py -3 Scripts/dev/TestCoverage.py` 의 "없음" 폴더, 온라인 `AccountTicketConnect` 의 만료 · 재시도 길.
- 흔들리는 게임 골든: 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스 규칙(2026-11-09):** 받아 오는 리소스는 무료 · 공개 라이선스(CC0 우선, MIT 처럼 재배포가 되는 것은 고지와 함께)만 — 묶음 페이지의 라이선스를 확인해 credits 에 URL · 날짜. 출처 하나에 고집하지 않고 뼈대가 달라도 리타깃한다. 에이전트가 리소스를 직접 그리지 않는다. PixelFX Piper(무료)는 쓴다. Agent Sprite Forge 는 도입하지 않는다(유료 생성 API 필요). Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌었다 — 새 묶음은 그 묶음 페이지가 CC0 일 때만. 원본 바이너리는 Git LFS, 받아 둔 팩은 `/AssetLibrary/`(무시)에 두고 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/` 로.
- 범용 스크립팅 언어는 도입하지 않는다(실제 요구가 확인될 때만 — [판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤, GC 는 권하지 않는다.
- 웹은 wasm32 다(유니티 · Godot 웹과 같다 — memory64 는 Safari 미지원 · 경계 검사 비용). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓰고 스스로 다시 짓는다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`(시연 모드는 테스트 자료). App 의 `Dev/` 는 남긴다(배포에 쓰지 않는 인터페이스를 두지 않는다).
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
