# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 본다. 끝난 일은 git 기록에, 지킬 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다 — 여기에 되풀이하지 않는다. 끝나면 줄을 지운다.

## 일하는 법

절차는 스킬이 정본이다: 묶음 끝 검증 · push `bundle-verify`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계 · 시간 기록은 `Test/README.md`. 사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset] [--backend] [--stages a,b] [체인…]`.
병렬 작업자 워크트리는 main 에서 fast-forward 한 뒤 시작한다(옛 기준에서 시작한 갈래는 통합 때 수십 파일이 충돌했다).

## 상태 (2026-11-10)

- 작업자 브랜치 열하나(dzn · GL 샘플러 · 5-1 · 5-2 · 5-3 · 5-4 · 5-5 · 게임 GPU 게이트 · W-S4)를 통합 브랜치(04fbacb866)에 합쳤다 — 워크트리 Debug 경고 0, 단위 3681/3681, 에디터 자체 테스트 47/0. main 병합 뒤 묶음 검증이 남았다(1 절).
- Android(Android-Shipping)는 대상 전체가 컴파일 · 링크되고 Vulkan 셰이더 쿠킹이 지난다. 웹(Web-Shipping)은 App.wasm 까지 지어진다.

## 남은 일

### 1. 묶음 검증

- main(c4ebf56542)의 묶음 검증 뒤 통합 브랜치를 main 에 합쳐 다시 검증 · push 한다. 통합 작업자가 남긴 위험: `.inl` 누락은 링크에서만 드러난다(Release · 서버의 다른 TU), `EditorTransaction` 을 연 채 같은 범위에서 CommandStack 을 읽는 자리.
- 흔들리는 단위 테스트 `AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`: 전체 실행 한 번에 "base 'testproject/base.audiograph.xml' could not be read" 로 졌다. 같은 씨앗 · 단독 1800 회로는 재현되지 않았다 — 다시 지면 그 로그의 앞 케이스와 스크래치 도메인 마운트 상태를 본다.
- 통합 때 확인하지 못한 동작: Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯(실장치), DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지).
- 통합 검증에서 함께 본다: 게이트 껍데기(`<빌드>/gates`) 생성 · 게임 타깃의 RHI · Shader 게이트, 5-4 생성기 출력(`_pApplyDefault` · `_pEditCondition` 줄, 파서 사례 `DefaultOnContainer` · `EditConditionUnknownName` · `RequiredAtRootWithoutParent`), `HapticEffectTest`, 시나리오 `editor/undocoverage`(실패 줄의 명령을 고친다 — 컴포넌트 대상 명령은 대상을 주는 줄을 더한다), G1 `slice` 12 게임(진 게임은 복원이 되살리지 못한 `state.objects` 를 본다), T2 격리(`AutomationUserDataRootTest` · `engine.isolation` · `Scenario.editor.statedir.*` · `net-pair mmo`), N2 `server-image` 한 번.

### 2. 플랫폼 ([PlatformTargets](PlatformTargets.md) "남은 일")

- **Android:** `CookAssets`(호스트 빌드로 굽기) 통과 → `py -3 -m Scripts android-apk --game <게임>` → 실기기에서 기동 · 회전(pre-rotation 방향) · APK 팩 읽기 · 터치. JNI 클립보드(`AndroidClipboard.cpp` 는 지금 `isAvailable` false).
- **웹:** 브라우저에 띄우기(팩 패키징 · COOP/COEP 로컬 서버 · 브라우저 MCP 로 기동 · WebGPU · 입력 · IDBFS), WebSocket 전송(클라이언트)과 서버 WebSocket 수락. WebGPU 단계 텍스처 한도는 `CookContract.json` 에서 null 이라 검사하지 않는다 — 어댑터 한도 16 인 브라우저에서 엔진 칸 슬롯 에뮬이 넘칠 수 있다.
- **Linux:** `.linux` 골든을 dzn 으로 다시 뜬다(shooter3d · empty.boat · wildwest mount …), themepark 체인 멈춤, dzn 에서 진 단계 netresourcedigest · sequencercuts · sequencerkeys(`--gpu software chain editor` 로 가른다), 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심), lavapipe `blendEnable` + `R16G16_SNORM` 재현 확인.

### 3. 통합 담당이 직접 (실행 · 측정이 필요)

- **에디터 첫 패널의 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초. `-gv_editorPanelTimes` · `-gv_traceSlowTickMs` 로 그 패널의 동기 준비를 찾아 `IEditorPanel::onPanelOpened` 의 비동기 잡으로(`ModulesModel::ensureLoaded` 가 후보).
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력), waterplay(강체 속도 · 잠듦 감시 값으로 조건 대기).
- **측정(S8 표):** Tracy `GT.Frame` 자식 구간 합 90% 이상, 유니티 배치 크기 12 · 24 · 48, `load/*` 4 시나리오 × 워커 1 · 2 · 4 · 8 기준선과 구조 지표(`GT.Scene.tick.waves` · `batchElements` · `taskEntries` · `mainEntries` · `objectEntries` · `stageLists`), W25 GPU 비교(c05a3ee251 대 지금 — 노이즈 안이면 되돌린다), W-S4 `-ftime-trace` 재컴파일 TU · ms 전후, W-R1 RT 프레임 힙 할당, W-R2 `RT.PSO.batchLookups` · `ensureMaterial` · `RT.Draw.gpuBatches`(이득이 있으면 배치 때 PSO 핸들을 풀어 둔다), 5-2 `RT.Bind.*` 비율, 에디터 MVVM(Hierarchy 큐브 8000 · 인스펙터 값 행 — 옮기기 전 커밋 대비), 골든 여유(`ScenarioCulprit --golden-margins`).

### 4. Shooter3D 총 (사용자 지적)

- **소총 자세:** 두 손 층 · 리타깃 프로필 · `WeaponDef::_bTwoHanded` 는 main 에 있지만 `weapons.xml` 의 `twoHanded` 는 꺼 두었다 — 켜면 KayKit 2h 조준 클립이 UBC 에서 조준 자세가 아니라(왼손이 앞손잡이를 놓고 펴짐 · 1 인칭 하체 빠짐) shooter3d 골든이 진다. 작업자가 리타깃 팔 사슬을 고치는 중(대안: Quaternius Toon Shooter Game Kit `Character_Soldier` — 묶음 페이지 CC0 확인 먼저) → 켜고 손 소켓 `Gun` 회전을 사진으로 맞춤 → 골든 다시.
- **걸을 때 총이 떨린다:** 원인 미확인. `-gv_shooterMotionTrace` 기록에 총 위치를 더해 손 · 카메라와 프레임별로 대조한다.

### 5. 작업자 · 설계 문서가 남긴 일

- **린트를 빌드 구조로:** `editor-view-includes` 를 Panels View 부분의 `#error` 껍데기 게이트로(먼저 린트가 못 보는 전이 include 를 끊는다). 에디터 플러그인 타깃(`GameFramework/Plugin/Editor` 의 CharacterEditor 등)은 게임 GPU 게이트 밖이다.
- **이름:** `Engine/Graphics/RenderState/` 의 형식이 아직 RHI 머리 이름(`RHIBlendTargetState` 등 약 500 곳 — XML 요소 이름 · 리플렉션 이름과 얽힘).
- **렌더링:** Vulkan placed 텍스처(해제 큐에 메모리 참조 세기), TransientLeasePool 을 뷰 풀에 잇기, 비동기 컴퓨트 큐 패스 타임스탬프 · Tracy 프레임 패스 내보내기, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단 외형의 디퍼드(G버퍼 칸 부족 — 포워드 전용으로 남김), 몸 머티리얼이 피부 층 표면 덮어쓰기(`_skinLayerSurfaceMap`)를 읽기, 텍스처 전이 · VFX · 프로브 디스패치의 버퍼별 전이를 묶음 API 로. DX11 postprocess_ssr 이 체인 안에서만 골든과 16.1% 다르다(단독은 0% — 앞 단계 상태, 원래 있던 결함).
- **표면 변형:** 지형(`terrain.hlsl`)은 머티리얼을 픽셀에서만 읽어 정점 변위를 받지 못한다 — 엔진 텍스처 칸 `SurfaceDeform` 과 패스 상수 `g_SurfaceDeformParams`(창)를 더해 지형 정점 셰이더가 읽게 한다(언리얼 RVT · 유니티 `SetGlobalTexture` 자리, 그림자 패스도 같은 오프셋). 네 백엔드 셰이더 · 바인딩을 함께 바꾸므로 빌드 · 시나리오를 돌릴 수 있는 작업자가 한 묶음으로 한다.
- **폴더 자리:** 엔진 쪽 `Dev/` 예외(`Config/Rules/Lint/FolderLayout.json` `_devFolderAllowed` — ModuleManager/Dev · Host/Dev · App/Dev · GF GameData/Dev · Graphics/Material/Dev)를 DevTools 로 — ModuleManager 를 공유 라이브러리로(언리얼 HotReload 처럼), DevTools 에 리플렉션 단계를 더하면 핫 리로드 · 머티리얼 그래프가 옮겨 간다.
- **W-S4 막는 구조:** clang `-Werror=undefined-func-template` 을 Engine · GameFramework · Games 에 켜 `.inl` 누락을 링크가 아니라 컴파일에서 잡을 수 있는지 본다.
- **RHI(5-2):** DX12 기록 경로 락 — 바인딩마다 `shared_mutex`(`resolveBufferAddress` · `resolveOfflineView`), 배리어마다 `_resourceStateMutex` + `_mapOffscreenTexture.find` 를 병렬 기록 구간의 레지스트리 불변식(`setParallelRecording`)으로 걷어 내고 텍스처 상태를 `RHIHandleTable` 레코드로(`bench.json` p50 전후). 측정 뒤에만: Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, 중립 커맨드 스트림 · RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, Android `VkDeviceMemory` 서브할당.

### 5-3. 병렬 오브젝트 · 물리 모델 남은 것

GameObject/Component API 는 저작 · 수명 · 직렬화 층, 프레임 일은 접근을 선언한 잡, 핫 데이터는 종류별 SoA(완전 ECS 는 기각). 단계마다 빌드 · 테스트가 통과해야 하고, 이득이 흔들림 안이면 되돌린다.

- **컴포넌트 틱의 컨텍스트:** Batch 레인의 `onTick` 도 `SceneJobContext` 형식(읽기 전용 남의 오브젝트 + 명령 버퍼)으로 — 남의 컴포넌트 직접 쓰기를 컴파일에서 막는다. 102 개 타입 서명이 바뀐다(`TickBatchRegistry.h` 머리말).
- **서비스 갱신 병렬화:** 같은 단계 서비스 갱신을 접근이 겹치지 않는 것끼리 워커로 — 본문의 게임 스레드 전용 API 를 서명으로 먼저 가린다.
- **물리 훅 배치:** 훅 4 종(`beginPhysicsFrame` · `prePhysicsStep` · `postPhysicsStep` · `endPhysicsFrame`, 재정의 타입 12)을 생성된 `SW_REGISTER_PHYSICS_HOOKS( 타입 )` 배치 함수로(재정의 여부는 `&T::hook` 의 클래스 형식으로 컴파일에서).
- **SoA 다음 사용자:** 군중(`NavAgentCrowdSync`)의 에이전트별 `setWorldPosition` 을 `queuePoseWrite` · `applyTransformBatch` 일괄 쓰기로.
- **공간 질의:** 근접 기믹 · 감지 · 넷 관심 격자를 `SceneSpatialGrid` 층으로 옮길지 측정 뒤.

### 5-4. 리플렉션 남은 것

- W-R1 나머지: 보관처를 (타입, 색인, 세대)로, 판정 비트의 `mutable` 지연 자리 제거(검증 판정이 칸 타입을 재귀로 묻는다).
- W-R3 (a) 정적 서술자 표(constexpr, 해시 사전 계산, 컨테이너 래퍼 정적 객체 — 목표 등록 할당 타입당 3 이하, 생성 바이트 4 MB 미만; 지금은 `CodeGenerator::emitPropertyInfoEntry` · `emitPropertyMetadata` 가 즉시 호출 람다로 짓는다). (d) 같은 모듈 리프 이름 겹침을 모듈 목록 단계에서 오류로. (e) `CultureInfo` 의 부모 병합 · 자식 요소를 리플렉션 컨테이너로(`<item>` 코덱 필요) — 불변 문화권 폴백을 없애면 초기값을 지우고 `RequiredAtRoot` 로.
- W-R5 빌드 시간: 헤더당 `.gen.cpp` 대신 모듈 안 8~16 헤더 묶음 TU(목표 생성 비중 8 % 미만 — `BuildTimeReport.py`).
- W-R4 `ReflectVisit` 하나(직렬화 9 루프 · 검증 · 문서 · 인스펙터 gather · 상태 해시 기본 구현) — `foldStateHash` 대체는 상태 해시 동등성 판정과 같은 묶음.
- W-R6 밀집 타입 색인: 5-3 SoA 와 함께만. `_cachedNameHash`.
- N1 복제 스키마: `PROPERTY( Replicated, Quantize = … )` 로 작성기 생성, 스키마 해시를 와이어 버전에(W-R3 (a) 위).

### 5-5. 상용 대비 고도화 남은 것

- **T1 골든:** 골든 옆 `*.golden.json`(기록 커밋 · 측정값), WebGPU 골든 0 — 체인 백엔드 집합을 한 곳에서 정의해 골든 폴더와 대조.
- **T2:** 표준 입력으로 모는 스크립트 실행(`LockstepPairRun.py` · `TelemetryLocalRun.py`)이 세이브를 쓰면 `-unattended` 를 붙이거나 판정에 더한다.
- **T3:** 묶음 끝 스크립트가 보고 폴더를 `TestHistory.py ingest` 하고 ctest 에 `quarantine --ctest-exclude` 를 `-E` 로(bundle-verify 스킬과 함께), 샤드 시간 균형, 파이프라인 정의로. 외부 CI 러너는 사용자 결정.
- **U1 햅틱:** 방아쇠 진동(DualSense · GameInput) · Android Vibrator 는 그 백엔드가 생길 때.
- **A1 포즈 노드:** Mirror · Montage(섹션 · 분기)를 `AnimationPoseNodeRegistry` 에 한 줄씩.
- **R1 리다이렉터:** 에디터 옮기기 · 이름 바꾸기가 `redirects.xml` 에 줄을 더하게(`move-asset`).

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
