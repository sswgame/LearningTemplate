# 할 일

> 결론: 할 일 · 계획은 이 문서 하나다. 끝난 줄은 지운다(기록은 git). 규칙은 `AGENTS.md` 목차의 `docs/agents/`, 절차는 스킬(`preflight` · `linux-verify` · `scenario-debug`).

- 작업자는 이 문서를 고치지 않는다 — 통합 커밋이 줄을 지운다(커밋 메시지에 `감사 <ID>`, 주석의 `감사 <ID>` 는 `git log --grep`).
- 측정 줄은 문턱을 넘을 때만 고친다. 못 넘으면 값을 커밋 메시지에 남기고 줄을 지운다. "켤 코드" 커밋은 따로 — 흔들림 안이면 그 커밋만 되돌린다.
- 차례: 결함 · 계약 → 구조 장치(형식으로 막기) → 문턱을 넘은 성능.

## 0. 인계(2026-11-21 — 다음 세션은 여기부터)

- **통합 중:** main 에 w-devmetrics · w-chores · w-gimmickdamage(w-gimmickevents 포함) · w-kitsplit2 · w-material · w-zonefix · w-playerstate · w-pathproducers(w-pathjoin 포함)를 합친다. 합친 뒤 충돌 파일에 `py -3 -m Scripts byte-reader-values` · `disk-path-args` · `disk-path-join` 을 다시 돌리고, Release · Shipping · Server · ASan 빌드 → 갈래 테스트 · 시나리오만 dx12 로 → preflight `--no-linux` → push.
- **갈래마다 볼 테스트 · 시나리오:** `kitdemo_respawn` · `minigamememory` · `triggercontract` · `kitdemo_actioncombat` · `kitdemo_actionplatformer` · `kitdemo_tactics` · `meleeduel` · TestCombatAttributeSet · TestMaterialInstance · 존 · 트리거 겹침 · 시퀀스 트리거 테스트 · wildwest `arrival*` · `cellcutscene` · 에디터 `netsequence` · PlayerStateComponentsTest · LootGrantTest · 세이브 시나리오 전부(ComponentStateStore 판 3) · meadowvillage · harvestvalley · frontiertown · velmora · wildwest · playground 허브. `AssetReferenceFields.generated.json` 은 빌드 산출로 커밋.

## 1. 다음 순서

- **모드 세션 색인:** `ModSession::apply` · 해제가 게임 전체 에셋 색인(4518 개)을 매번 다시 설치한다 — ASan 단독 케이스 2.9 초, tidy 와 겹친 샤드에서 30 초 시한을 넘었다(2026-11-21 `ModSessionTest.LayerModAddsEntriesAndReleases`). 모드 층만 덧대고 떼는 증분 색인으로 바꾼다(언리얼 패치 팩 마운트가 기존 색인에 항목만 더하는 자리).
- **이벤트 버스 소비자:** `QuestLogComponent` 가 `InteractionCompletedEvent` · `AbilityOwnerDiedEvent` 를 구독해 자기 조종자 폰의 것만 `QuestLog::notify` 로 센다(`onRegister` 구독 · `onUnregister` 해제). 나머지 받는 쪽 없는 이벤트(세이브 · 씬 로드 → UI, 방 클리어 → HUD, 빙의 → 카메라)는 쓰일 때 붙인다.
- `IHealthSource` 읽기 계약과 맞음 받기를 한 받는 쪽 인터페이스로 합친다.
- `ActionRoom::drawDebug` 를 클라이언트로. Linux 검증은 여러 묶음 push 뒤 한 번(`linux-verify`).

3. **허브 흡수:** 7 절을 단계 5 부터 한 갈래씩. 이름 바꾸기 도구가 못 하는 곳은 도구에 더한다.
4. 씬 스트리밍 (라), 렌더 그래프 ③ · ④, 헤더 팬인 ③ · ④(혼자 마지막), 에셋 GUID 2 · 3 단계.
5. **엔진 전수 조사 남은 몫(6 절):** 구조 장치 → 에디터 · 키트 → 성능(측정 뒤). 씬 관리 · 월드 스트리밍 전수 조사 결과가 없으면 다시 돌린다.
6. **감사 열린 항목(9 절):** "확인" 테스트를 먼저 써서 지는 것을 본 뒤 고친다.

## 2. 통합 빌드에서 볼 것

- **5-4 리플렉션(A-24):** 생성 TU 비중 Release 13.4 %(목표 8 % 미만). 후보 커밋마다 `build_time_report.py --top 20` — 흔들림 안이면 되돌린다.
- **RHI 5-2:** `D3D12.ComputeTimestampClock` 어긋남, Vulkan 비동기 컴퓨트 `GPU.<패스>` · Tracy 겹침.
- **실장치 · 백엔드:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯, DX12 placed 텍스처(`ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(검증 경고 0).
- **손 확인을 시나리오로:** 설정 표 Ctrl+Z, 게임 설정 열어 저장, 렌더 품질 변경 → 그림자 · 텍스처 풀 값.
- **배포 도구:** N2 `py -3 -m Scripts server-image`, G1 `slice` 12 게임.
- **에디터 View:** `Common/GUI` 나머지(상태 표시 · 도킹 레이아웃 · 테마)를 `[panelViewBan] views` 로.
- **preflight tidy:** 첫 겹쳐 돌기에서 캐시 적중 · changed 시나리오 영향을 잰다.
- **D3D11 첫 시나리오:** DXBC DDC 키가 바뀌어 첫 컴파일이 는다 — 정지 시한에 안 걸리는지.
- **경로 형식(w-pathjoin · w-pathproducers, 빌드 없이 바꿈):** 지는 TU 는 스크립트가 멤버를 지역으로 본 자리 · DDC 층을 칸별로 채우던 자리일 가능성이 크다. `join` 은 기준 위로 오르는 `..` 를 빈 경로로 거절, 공유 DDC 환경 변수가 상대 글이면 `File.RelativePath` 오류 — I/O 실패가 나면 그 글을 고친다. 판 읽기 옛 꼴(`deserializeVersioned( version, … )` · `readSection( a, tag, version, body )`)은 손으로 옮긴다.
- **디스크 경로 형식 남은 몫:** `toDiskPath` 감싸기 1062 곳. `AssetPaths` 루트 칸 넷 · `AssetIdentityRules` 의 `string_view absPath` 셋은 받는 쪽(`importAll*` · `registerWatch` · `collectFiles`)을 `AbsolutePath` 로 먼저(`disk-path-args`), 그다음 `disk-path-join --migrate-producers <이름>`. 그 뒤 `FileUtil::joinPath` 를 글 전용으로 좁힌다. 출력 인자 읽기 남은 몫: `BinarySerializer::deserializeExactVersion( …, uint32& outReadVersion )`.

## 3. 알려진 실패 · 흔들림

- **`editor.playreload` 격리(만료 2026-11-27, `Config/Rules/Test/Quarantine.json`):** 게임 모듈 교체가 시계를 170 초 넘게 잡는다 — 풀고 다시 돌린다.
- **shooter3d dx11 골든:** crowd `fight` 25.7 %, enemydeath `death_b` 1.37 % — 다시 지면 `scenario_bisect --golden-margins`(군중 정착 조건 의심).
- **`editor.tilemapdatalayers` 단독 실패:** 글 칸 키보드 초점 — `EditorSelfTestInput::isKeyboardTargetStable` 를 `scenario-debug` "단독으로만 지면" 으로.
- **Linux:** sequencercuts · sequencerkeys `SequencerUnboundCount`(`--gpu software chain editor`), netresourcedigest, 에디터 체인 약 18 단계(입력 초점), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`(재현 안 되면 지운다), `editor.screenrectview` 1.09375 %(`all` 에서만 — 사진 앞 대기를 정착 조건으로). 빈 골든은 `scenario_bisect.py --golden-coverage` 로 찾아 채운다.
- **`AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`:** 한 번 "base … could not be read" — 다시 지면 앞 케이스 · 스크래치 마운트.
- **라이브 리로드(Debug):** Building 중인 변형이 옛 바이트코드를 집고 옛 키 항목이 쌓인다.
- **손 빌드:** Ninja 재압축 이름 바꾸기 보정(`.ninja_deps` 실종)이 preflight 에만 있다 — 손 빌드에도.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 4. 플랫폼(웹부터)

- **웹 실행:** Web-Shipping → `py -3 -m Scripts web-check --game Empty`(WebGPU · 오류 0 · IDBFS · `[WebAudio] output started`). 입력 판정용 웹 시나리오 길 먼저. `waitIdle` 호출 스택을 본 뒤 `WebGPURHIDevice` 프레임 콜백 대기를 `waitFrameFence` 로.
- **웹 온라인:** `wss://`(TLS 종단), 게임 세션의 브라우저 길(WebSocket · WebTransport).
- **Android 실기기:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 기동 · 회전 · APK 팩 읽기 · 터치 · 클립보드 · AAudio 다시 열기 · IME 조합.
- **Fileserve 실기기:** `adb reverse` + `-fileserve` 기동이 `android-apk` 다시 묶기 + 설치의 1/2 아래가 아니면 구간 크기 · 동시 받기.

## 5. 측정(문턱을 넘으면 고친다)

- **E1 에디터 첫 패널 · Hierarchy 8000:** `-gv_editorPanelTimes=120 -gv_traceSlowTickMs=100` — 첫 그리기 50 ms 넘는 패널은 준비를 `onPanelOpened` 잡으로, 100 ms 넘는 프레임 0. 큐브 8000 정지 0.5 ms · Ctrl+A · 거르기 16 ms 미만.
- **S8 후보(Release):** `CompletionCostByPending`(흔들림보다 크면 `_mapCall`), `NestedElementCost`(작으면 `NestedTypeCache` 되돌림), **파서 토큰화** `TokenizeStats`(10 % 넘으면 토큰 저장소 재사용), **씬 저장** `Scene.save.*`(write 16 ms 초과면 워커로), 씬 로드(`instantiate` 우세일 때만 전처리 병렬), **씬 쿠킹**(scenes 가 절반 넘으면 씬 단위 병렬), `AssetLoadProfiler` 스톨 상위 비동기, `SceneChunkPlan`(1/4 넘으면 `ReachTraversal` 표식), **GPU 업로드**(`-gv_gpuUploadQueue=0/1`, pump 가 RT 10 % 초과면 RHI Thread 시제품), **렌더 그래프 폭**(`MinWidth=2 · 3 · 4 · 99`, 빠른 쪽을 기본값), `load/navcrowd`(넘으면 지역 분할).
- 그 밖 후보: `GT.Frame` 자식 합 90 %, 배치 12 · 24 · 48, `load/*` × 워커 수, W25 GPU(c05a3ee251), W-S4 `-ftime-trace`, W-R1 · W-R2, 5-2 `RT.Bind.*`, DX12 기록 락 제거(291d69074e), 잔상 메모리, `RT.Fog.Volume`, HDR SDR p50, 렌더 디버그 꺼짐 비용.
- **bindless 다시 쓰기(R-6):** 밉 오름 중 `textureResyncs` 0, `mirrorWrites` ≤ 3 × (등록 + rebind), 디스크립터 검증 메시지 0, `load/texturebudget` 뒤 120 프레임 안 예산 아래.
- **텍스처 스트리밍(R-6 · A-12):** `bytesInFlight` < 상한, 첫 화면 읽기 10 % 아래, 전체 밉 p95 가 10 프레임 넘게 늘면 상한을 올린다.
- **자동 렌더 배율(RN-8):** 부하 뒤 1.0 복귀, `renderscalestep` 일시 할당 0, 0.7 그림 대 `gv_renderScale=0.7` 1 % 아래.
- **입자 정렬(RN-15):** `vfxsort` 다섯 백엔드 골든, `sortParticles` 0.2 ms 넘으면 기수 정렬, `GPU.VFXSort` 0.1 ms 아래.
- **메시 인덱스 전환 뒤:** 모든 골든과 캐릭터당 GPU.
- **글리프 아틀라스(RN-14):** `HangulScrollGlyphsRasterized` 1e4d3dbe8c 대 fc54031411 — `rasterized` 가 약 15300 으로 안 줄거나 `missing` 이 프레임당 10 자 넘으면 `_glyphRetainFrameCount` 1, 그래도 지면 fc54031411 되돌림.
- **구독 제어 블록(C-20):** `SubscribeCost` 증가 30 ns 초과면 출처 생성자가 스레드 정책을 고른다.
- **팩 규모(A-9 ~ A-12):** `PackScaleTest` 블록 크기(2 % 안이면 128 KiB), `lookupNs` 가 해시 맵 1.5 배 넘으면 버킷 표.
- **출하 할당기(C-6):** 할당 비용이 프레임 3 % 이상이면 `SW_MEMORY_BACKEND=mimalloc` 비교 — p50 2 % 빠르고 메모리 10 % 이하 증가면 Shipping 기본.
- **비밀번호 해시(O3-5):** `login_storm` 2 배 도착률에서 다른 키트 p99 1.5 배 이내 · 거절은 `kBusy`. O3-2: `ILoginCrypto` 밖 암호 호출 보고.
- **자원 구간 할당(R-4):** `load/materialinstances` 할당 수 1/10 아래 · 프레임 흔들림 안이면 둔다.
- **N×M 후보**(재고 해시로): `findEntry`, `ComponentRegistry.cpp:60,75`, `PrefabPool.cpp:80,92`, `ShooterNetPawnComponent.cpp:535,571`, `MaterialCache.cpp:350`, `DestructionState.cpp:779,855`, `ContentBrowserCollectionStore.cpp:108`, `EditorAssetFileCommands.cpp:154,199,569`, `EditorNodeGraph.cpp:333,543,696`, `ModResolver.cpp:161`, `ProjectSettingsDocument.cpp:45,62`, `ComponentStateStore.cpp:82`.
- **컨테이너(`ContainerBenchTest.*` 5 번 중앙값):** `VectorGrowString` 1.5 배 이상이면 relocatable 만 `memcpy`, `VectorShiftBitwise` 2 배면 `Memory::move`, `UnorderedMapFind` 20 ns 이상이면 열린 주소, `flat_map` · `flat_set` 저장소 `sw::vector`, `ChunkedArray` 는 쓰는 곳이 늘 때. AC-1 범위 연산 복잡도.
- **잠금 종류(`LockKindBenchTest.*`, 스레드 2 · 4 · 8):** 조회 셋은 4 · 8 스레드 20 % 빠르고 2 스레드 5 % 이내면 `shared_mutex`, `SpinLock` 이 20 % 빠르면(잎만) 그쪽. `LockedSwapQueuePush` 10 % 빠르면 `SpinLock` 기본, `JoltJobQueue` 는 10 % 느리면 되돌림. 벤치 없는 후보는 잠금 대기를 먼저 잰다(`AudioClip` · `PrimitiveRegistry` · `_cmdListPoolMutex` · `_transferJobMutex` 등).
- **큐 칸 정렬:** 정렬 뺀 판이 3 % 넘게 안 떨어지면 뺀다.
- **오디오 실시간 스레드:** 허브 부하에서 `Audio.Realtime*` · `OutputUnderruns` · `RetireOverflows` 0.
- **게임 프레임워크:** 세이브 찍기(`Save.CaptureMs`) · 디렉터 걸음이 Android 4 ms 를 넘으면 게임 스레드 밖으로. 미니게임 나가기 메모리. 칸 바뀜 비트는 `net-pair` 뒤. 카탈로그 이진 꼴은 읽기 합 50 ms 초과 때.
- **엔진 틱:** 컴포넌트 틱 10 % 초과면 틱 주기 등급. `Float4x` 확대는 2 % 초과 때.
- **리플렉션:** 생성 상수 표화 · `VectorWrapper` 얇은 템플릿 · 원격 호출 비용 먼저, 쿠킹본 고정 배치 읽기는 뒤. `findDisplay` 5 % 넘으면 곁 배열.
- **에디터:** 차이 저장은 기록 16 MB · 편집당 1 ms 초과 때.
- **CMake:** 유니티 제외, configure 시간, 주석 헤더 재링크, split DWARF.
- **Scripts:** 훅이 `_hookBudgetMilliseconds` 를 넘으면 `py -3 -I -S` · 매처 좁히기.
- **작업 시스템(`Task.Join.*` 먼저):** `submitBatch`, 우선순위 레인, guided 청크.
- **측정 뒤에만:** DX12 PipelineLibrary, 깊이 전용 배치 키, MMO 거리 틱, 축약 애니메이션, 개수 버퍼 압축 · 지난 행렬 GPU 복사 · 다단 bitonic.
- **Shooter3D 소총 자세:** CC0 poly.pizza "SWAT"(`https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`)를 `models_raw/quaternius` 에, UBC 리타깃 상체 층.

## 6. 설계 할 일

### 렌더링

- TransientLeasePool 을 뷰 풀에, 후처리 머티리얼이 앞 결과를 이어 읽기, VFX · 프로브 전이를 묶음 API 로.
- 셰이더: `PixelContext` · 표면 평가와 패스별 출력 분리, 표면 모델 · 필수 입력 선언, 미지원 조합은 쿠킹 실패. `gimmicksurface*.material` 의 `MATERIAL_FORWARD_ONLY` 를 골든과 함께 뺀다.
- **R-15 후속 1:** GI 복셀화(`SceneVoxelizer.cpp`)를 GPU 원본으로 — 프레임 업로드 단계의 정점 풀 GPU 복사, 기하 셰이더 없는 3 축 투영(3D UAV 원자 쓰기). 스킨 · 모프는 CPU 정점.
- **R-15 후속 2:** `MeshCPUAccess` 를 `Mesh::create` · `MeshCache::request` 에 기본값 없이, 업로드 뒤 CPU 정점을 놓고 장치 교체 때 `.mesh` 를 다시 읽는다.
- **렌더 그래프 ③ 뷰 패스:** 뷰 소유 자원을 뷰 표에 등록해 `importResource`. 차례: 자동 노출(`AutoExposure` 뷰 패스) → `HZBOcclusion::recordInViewPass` → `FramePassExecutorPasses` · `Draw`.
- **렌더 그래프 ④:** 패스 몸통은 전이 없는 `RenderPassCommandList`, 전이는 실행기 · 프레임 끝만. 판정: 네 백엔드 골든 그대로, 검증 경고 0, `RT.Graph.transitions` 안 늚.
- **Renderer 차례:** ② 직렬 execute `_pCmdList` 누락 · 꺼짐 판정 앞으로 ③ 마스크별 컴파일 계획 캐시 ④ 주 시점 = 뷰 0 번 ⑤ 엔진 상수 칸 번호 계획(`passCbAcquires` 먼저) ⑥ (배치, 패스, 뷰 모드) → PSO 표 ⑦ 확장 지점 등록 선언, DX11 컬링 없음 기동 알림, 진단 스위치 배포 차단.
- **렌더러 규격(R-4 뒤):** 그래프 일시 버퍼 → 뷰 히스토리, `RHITextureSlot`(손 생성 8 곳), 컴퓨트 패스 도우미, 풀 버퍼, 되읽기 단일 면. RHI 자원 생성은 패스키로.
- **RHI ③ 남은 몫:** 이동 전용 소유 형식, `RHIDescriptorIndex`(지금 `uint32`) 강타입, 닫힌 리스트 토큰, 손실 보고 하나, 에디터 확장 경계 uint64 텍스처 id.
- **RHI 5-2(측정 뒤):** Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, RHI 스레드, 복사 큐, SM6.6, RT · 메시 셰이더, 서브할당.
- **L-2 상수 버퍼(측정 뒤):** `mirrorLockWait` 가 `parallelLevel` 의 2 % 넘으면 버퍼 레코드로 옮겨 `_bindlessMutex` 제거.
- **자원 구간 할당 ④:** VMA · D3D12MA — 사용자 결정 뒤(R-4).
- bindless stride: 인스턴스 · 배치 · 모프 · 머티리얼 버퍼도 `registerBuffer` 로 `validateStride`.
- 그래프 밖 버퍼를 읽는 컴퓨트 PSO 리플렉션을 버퍼 이름에 잇는 디버그 표.
- 화질: 볼류메트릭 구름 · 비 가림 맵. 실시간 GI 정착 조건(골든과 함께).

### 키트 셋 가르기(규칙 · 디렉터 · 클라이언트)

- TacticsSRPG 본보기(규칙은 키트 루트, 디렉터 `Component/`, 뷰 · 입력 · HUD 는 `GF_Client_<X>`). Fighting · Farming · CityBuilder 는 허브 미니게임으로 옮길 때. 남은 순서: BattleRoyale · MechArena · Metroidvania · CardGame · KartRacing · PartyArena · Rhythm · AsymmetricHorror · CoopScavenger · GhostHunt · SurvivalHorror · ClassicJRPG · MonsterCollector · MonsterHuntRPG · OpenWorldWestern · CreatureLife · RestaurantSim · ThemePark · RealTimeStrategy · SideScrollConquest · ActionAdventure.

### 코어 · 리플렉션

- **5-3 병렬 오브젝트:** PO-1 `Gimmick.ProximityCandidates` · `GT.Scene.service.<이름>` 을 부하 씬에서 잰다 → PO-2 직렬 서비스 합 0.3 ms 이상 · 겹치지 않는 둘이 각 0.1 ms 이상일 때만 `ISceneParallelService::updateOnAnyThread` 로 나눈다(게임 스레드 API 는 `const GameThreadKey&`, `TestSceneStateHash` 동일) → PO-3 4 워커 직렬 몫 약 5 ms 를 구간으로.
- **5-4 리플렉션:** W-R3 (a) `.gen.cpp` 합계 4 MB · 비중 8 % 미만 — 못 미치면 `EmitBytes` 25 % 넘는 방출 함수만 표로. 손 `serializeState`(약 158 곳)를 키트별 `StateIO::reflected` 로(3 % 넘으면 되돌림). RF-4 `ObjectStateSerializer` · `ComponentDefaults` 를 `ReflectVisit` 로. RF-7 밀집 타입 색인은 조회가 프레임 1 % 이상일 때. N1 칸별 변경 비트(`writeDelta`)는 대역폭 측정 뒤. FUNCTION: 인자 있는 메서드 위젯 → 액션 목록.
- 리플렉션 적용 후보: `ScreenshotStabilityMode` 열거형, `RigCopyTransformNode::Mode`, DevTools 손 이름 표(`ReloadSource` · `MemoryDiffStatus`) — 팬인 점수와 견준다. `Radians` 남은 몫은 회전 형식을 정할 때.
- **이름:** 멤버 · 지역 · 매개변수 형태(`_camelCase` · `out…` · 단수 컨테이너)를 자동 검사에 잇는다.
- App 종료 코드(낮음): `ProcessExitCodes.json` 범위 · 예약 · 매핑 검사와 생성기 테스트.
- 형식 개명 남은 몫: `HousingPlotComponent`, `UnitStatsComponent` Runtime 이전(키트 의존 먼저), `CueTriggerComponent` 상속. 흡수 때 컴포넌트 이름을 기전으로([NamingCpp](agents/NamingCpp.md)).
- `FrameViewports` 가 서버 · 쿠커에 불필요한 의존을 끄는지 include 그래프로.
- 원점 이동 뒤 `AIPerceivedTarget::_lastKnownPosition` 을 고친다.

### 엔진 전수 조사 남은 몫

- **구조 장치:** Action · Condition 레지스트리 → `NamedEntryRegistry<T>`. 리플렉션 `tryParseEnum<E>` · 배포 열거자 철자 · 반사 비교 · 해시 · 기본값 · `FUNCTION(Exec)`, `.cpp` 안 `REFLECT` 는 `.gen.inl`. 매크로: `SW_CONCAT` 사본, 에셋 캐시 · 엔진 서비스 매크로 각 하나로, `SourceLocation` 으로 Error · Warning · `SW_FATAL` 을 함수로. `rejectClientOnlyTask` 표를 `CommandLineArgumentUsage` 로.
- **Dev → DevTools:** 엔진 개발 명령과 쿠킹 코드(`PackCooker` · `SceneCooker` · `ShaderCookDriver`)를 DevTools 로.
- **파일:** 자기 `.cpp` 만 포함하는 60 줄 미만 헤더 65 개 접기, 한 곳만 쓰는 도우미 헤더 36 개 합치기(`header-fanin` 이 늘면 안 함).
- **쿠킹:** 셰이더 증분 판정 세 벌을 `CookJob` 하나로. `DerivedDataCache` 는 `open(bucket)` 만.
- **파일 감시:** OS 감시자는 `FileWatchService` 만(열쇠 형식, 루트마다 공유, 프레임 단위 일괄).
- **머티리얼:** `MaterialParameterHandle` → 바뀐 칸만 업로드, 전역 값을 전역 CB 로, 인스턴스 모델을 MIC · MID 둘로, `_permutations` 합치기.
- **컨테이너:** `list` 를 LRU 전용 형식으로 바꾸고 지운다, `string` 자체 구현.
- **디버그 허브:** `DebugHUDRegistry` 를 범주 단위(그리기 · 감시 값 · `VisualLogger` · 패널)로, `gv_*DebugDraw` 를 범주 켜짐으로. 진단 getter 118 개는 참조를 센 뒤 가른다.
- **에디터 · 키트:** VFX 편집기, 폴리지 칠하기, 스플라인 손잡이, 물리 에셋 편집, 패키징 플랫폼, 로컬라이제이션 패널, 프리셋 패널 마우스 공통 층(R10 ~ R13). 키트: "꽂으면 도는" 컴포넌트 층, 카탈로그 편집 + Map Check 등록점, 키트 생성기. 서버만 있는 키트는 지우지 않고 붙인다.
- 에디터 체인 단계의 뷰 누수를 막는다.
- **성능(측정 뒤):** 프레임 안 동기 로드 경고 · 렌더 스레드 디스크 IO 단언, 동기 로드 5 곳 비동기, 핫 경로 API 는 `string_view` · `hashed_string`, 칸 형식 해석을 `PropertyInfo` 캐시로.

### 에디터 · 도구

- Undo 누락 · 취소 불가 편집을 시나리오로 찾아 보강(E-1 · E-2 · E-13).
- 빈 상태(새 프로젝트 · 첫 임포트 · Map Check 오류 · 첫 패키징)에 다음 행동을 잇는다.
- 시각 저작 첫 사례(애니메이션 마스크 · 소켓) — XML 왕복 · 재로드 · `uicapture` 로 완료.
- 머티리얼 제작 · 게임 액션 저작 UX 는 사용성 문제가 확인된 뒤.
- **도구(문턱 넘을 때):** V2 ~ V4 timeline, **T2** 체인 샤드(가장 긴 체인이 벽시계 40 % 초과 때, 시간표 `ScenarioCosts.json`), **R3** 에셋 색인 디스크 캐시, D3 공유 DDC.
- preflight tidy 캐시: `Saved/TidyCache` 정리, STL · SDK 판을 캐시 키에.
- 리다이렉트 표 ②: `shipping-check` 가 `redirects.xml` 을 확인.
- **퍼저 타깃**(신뢰 경계만): 세션 패킷, `JSONDocument`, 세이브 머리. 크래시 입력은 `Tests/Snapshots/` + 단위 케이스, 30 초 뒤 `cov` 가 안 늘면 죽은 타깃.
- 아바타가 죽는 게임이 생기면 `netpair/` 리스폰 시나리오.

### 데이터 · 게임플레이

- 매직 리터럴 큰 파일의 튜닝 값 · 상태 `switch` 를 PROPERTY · 카탈로그로, `sw::expected` Core 밖 이전.
- `FighterCatalog` · `ActionPlatformerCatalog` 검사를 참조 표로 옮길지 정하고 잘못된 참조 회귀 테스트.
- 카탈로그 키: 쿠커가 상속을 따라가게, 같은 파일 참조(`_dropID` · `_replacedID`)를 `<Reference>` 로.
- `GameInstance`: 정책이 다른 책임만 나누고 `update` 순서는 계약 테스트로.
- 키트별 `credits.md`.
- 테스트 없는 기능 — 둘 다 없음: SmartObject · 소프트바디 · HDR 출력. 시나리오 없음: 3D 파괴 · 표정 · 립싱크 · AI 인지 · 스폰 디렉터 · 락온 · 턴 순서 · 인벤토리 · 제작 · 상점 · 날씨 · 카메라 흔들림 · 사진 모드 · 스플라인 강. 단위 없음: Grabber · 튜토리얼 힌트 · 안개 · 무기 교체.
- 없는 기능(Playground 에서 쓰고 시나리오로): 텔레포트, 풀베기, 나무 베기, 피 데칼, 비 · 눈 · 웅덩이, 실내 차폐, 날씨 → 오디오 · 조명, 바람 → 천, 등반 · 수영 · 사다리, 상자 · 줍기, 이모트 · 미니맵 · 낚시.
- 시연 컴포넌트 19 개의 카탈로그 경로 읽기를 `GameFeatureScope::loadCatalog<T>()` 로.
- 미니게임 결과 → 경제 · 진행. 허브 키트를 허브 맵에, 전용 서버 허브(NetMMO), 매치형 미니게임 세션 가르기 · 결과 보고, 로컬 멀티는 `InputUser`, 미리 읽기.
- MMO 키트 부하 목표 · 운영 비용. T3 샤드 균형(외부 CI 는 사용자 결정), U1 진동.
- 기믹 시나리오: 쇼케이스에 동적 상자 · 탈것 · 경비 AI 를 두어 발사 · 부스트 · 소리 효과를 시나리오 값으로 본다(지금은 단위 테스트만).

### 씬 스트리밍 (라)

- 그리기 수가 문제면 `MeshMerge` 대리를 `_hlodFileID` 자리에. 셀 로드와 스트리밍 우선순위 공유, 로드 큐 디버그.
- 셀 안 PROPERTY 밖 상태(`ComponentStateStore`)가 세이브를 넘게. 비탈 바닥 광선은 셰이프 스윕.
- 원점 이동: 내비 · 군중이 원점 기준 질의를. 하위 레벨 문서 읽기를 비동기로.

### 헤더 팬인(혼자 마지막)

- ③ `TypeRegistry.h` 의 `TaskTypes.h` · `EngineMinimal.h` · `ReflectionCast.h` 를 cpp 로. ④ `ReflectionTypes.h` → `TypeInfo.h` 는 생성기 템플릿과 함께. 점수 합 5 % · 그 헤더 30 % 안 줄면 되돌린다.

## 7. 허브 흡수(한 단계 = 한 갈래)

사용처를 옮기고 시나리오 · 골든이 통과한 뒤 폴더를 지운다. 남길 것: 작은 둘째 팩 NileCity.

| 단계 · 갈래 | 내용 | 통합 때 돌릴 것 |
| --- | --- | --- |
| 5 `w-absorb-wildwest` | 허브 맵 바탕(OpenWorldWestern · 말 · 날씨 · 컷씬) | 옮긴 9 + 날씨 · 자동 저장 |
| 6 `w-absorb-citybuilder` | FrontierTown → 도시 건설 | 옮긴 3 + `nilecity` |
| 7 `w-absorb-harvestvalley` | 허브 농장(Farming · 낮밤 · NPC 일과) | 옮긴 7 + editor 1 |
| 8 `w-absorb-meadowvillage` | 하우징 · 생물(NetSimulation) | 옮긴 5 + editor 1 |
| 9 `w-absorb-themepark` | 테마파크(하위 레벨) | 옮긴 3 + editor 2 |
| 10 `w-absorb-voxelcraft` | 광산(Voxel) | 옮긴 3 |
| 11 `w-absorb-abilityarena` | 아레나 | 옮긴 3 + editor 2 |
| 12 `w-absorb-velmora` | 액션 어드벤처 + DLC 세이브 승계 · ModSupport | 옮긴 1 + sequence 2 쌍 |
| 13 `w-absorb-starskirmish` | RTS 매치(Lockstep), 리다이렉트 표 | 옮긴 4 + load 1 + `lockstep-pair` |
| 14 `w-absorb-lanternvale` | 허브 서버(NetMMO · 계정 저장) | 옮긴 2 + `net-pair mmo` |
| 15 `w-absorb-shooter3d` | 사격장 · 온라인 매치, `Shooter3D*` 설정 · 비밀 변수 → `Playground*`, 프리셋 → `NileCity` | 옮긴 32 + showcase 8 + desktop 4 + `Server --test` · `online-local run` |

같이 고칠 자리: `linux_scenarios.py` `ALL_CHAINS`, `AssetReferenceFields.generated.json` owner, `Config/Rules/Lint/*` 경로, `Test/README.md`, `RunStateDirPair.cmake`. 도구는 `move-asset` · `move-source` · `rename-symbol`(`--dry-run` 먼저), 옛 이름은 `_bannedNames`.

## 8. 에셋 정체성(GUID)

- **1 단계 통합 검증:** GUID · 데이터베이스 · 팩 색인 단위 테스트, `editor.assetidentity` · `assetmove` · `flowimport` · `contentbrowser`, 모든 쿠킹(`PackCookNoIdentity` 0).
- **2 단계 — 참조를 GUID 로:** 경로 글 칸(값 59)을 `AssetRef<T>` 로 바꾼 뒤 `migrate-asset-refs --fix`(4003). 세이브 · 서버 기록 경로를 한 번 풀어 쓴다. `enginedefaultassets.xml` · `_base` 파일 부분, 캐시 키는 `AssetRedirect::toCacheKey` 한 곳. 산출물 줄(원본 GUID → 산출물 경로) 뒤 산출물 참조 898. 옮기기용 리다이렉트를 걷고 GUID→GUID 표만. localization 표 머리 쓰기, `scratch/` GUID 겹침.
- **3 단계 — 원본 저장소 떼기(원본 2 GB 또는 LFS 비용이 문제일 때):** `_source` 는 상대 경로 또는 `store:<SHA-256>/<이름>`, 창구는 `AssetSourceLocator::resolve` 하나. 공유 DDC 도 이 단계.

## 9. 감사 열린 항목

줄 앞은 감사 ID 와 옛 번호 `[n]`(주석이 가리킨다 — 다시 매기지 않는다).

- **P-4 [14] Android IME 조합:** `WindowTextInputTest` Android 케이스, `web-check` `compositionend`, 실기기.
- **O-12 [30] 원장 경로 부하(통합):** PostgreSQL · RESP 로 `OnlineLoadBot` `ledger_mix`(`--store-config=Config/Server/Online/Shooter3D.external.json`) — 재시도 · busy · 버린 도착 수를 여기 적는다. 종료 코드 5 는 재화 보존 깨짐.
- **R-4 [31]:** 6 절 "자원 구간 할당 ④".
- **RN-6 [34] GPUScene 부분 수집:** `31f2492dc8` 대 지금 `instancechurn` 의 `build.collect` · `build.batches` p50.
- **A-13 [36] 씬 스트리밍 측정 길:** 쿠킹본 실행 ctest(지금은 `engine.en.locres` · dx12 셰이더가 팩에 없어 멈춤) 위에서 `SceneStreaming` 최대 프레임.
- **A-24 [47]:** 2 절 5-4.
- **K-11 [50] 슈터 적 풀링(통합):** 다시 꺼낸 적이 래그돌 · 절단 · 무기 · 빙의 없이 서는지(`enemydeath` · `dismemberment` · `effectpool`).

## 10. 다음 감사 범위

- **RHI · 렌더러:** PSO 생성 스레드 안전, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX, `executeParallel` 배리어, 모프 풀, 실시간 프로브.
- **직렬화 · 입력:** `SerializerUtil` · `XMLSerializer` · `DataInheritance` · `ResourceComposer`, `CodeGenerator`, `InputMap` 고정 틱 엣지, `WaylandWindow`, 접근성 브리지.
- **온라인:** HTTP/2(HPACK · 프레임 상한), 영수증 검증기, RESP 재연결, O-7 파티 티켓, O-6 명단 캐시.
- **게임플레이 · 에디터:** `ScheduleSystem`, BehaviorTree, Appearance, 장르 키트 14 개, 임포터, Animation 패널, 프리팹 오버라이드, 도킹.

## 11. 코드 `shortcut:` 정리

정본 모양이 있으면 크기와 무관하게 고친다. "규모가 커지면" 인 것만 문턱을 적고 남긴다.

- 렌더: 변위 UV 이음매(`material_displacement.hlsli:22`), 코트 없는 젖음(`appearance_lit.hlsl:408`), 홍채 노멀(`:38`), Vulkan HDR 중 새 창.
- 게임플레이: `PrefabAsset.cpp:336` JSON 저장 칸, Activator 동시 개인 컷씬.
- 도구: `EditorStateDocumentPanel.h:26`, `AdminToolSession.h:158`, 옛 `<Wait>`(`AutomationScenario.cpp:86`, 123 곳). 문턱만: `PackWriter.cpp:226`, `AutomationPerBackend.cpp:575`, `AssetAndToolTargets.cmake:57`, `test_history.py:284`, `naming_words.py:107`.

## 12. 리소스

- Sprite Fusion 타일셋(CC0, `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`), 1Bit Platformer 는 Kenney 원본, VFX 25 종(MIT, 시트로 굽기), PixelFX Piper 는 조건 확인 뒤.
- 플레이스홀더 · 원시 도형 자리를 `-gv_screenshot` 으로 모아 CC0 팩으로(판정: 0).
- Empty → TestBed 개명, `new-online-kit` 시험, `-cooked-content` 에셋 누락, `test_coverage.py` 빈 곳, `AccountTicketConnect` 루프백 · 비밀 값 로그 누출.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인 계정 · 키, 새 의존성(인코더 · VMA · D3D12MA) 라이선스, 공유 DDC 머신, 원본 Git LFS(S1)와 이력 다시 쓰기, 원본 별도 저장소, 외부 CI 러너.

## 지킬 결정

코드 규칙에 들지 않는 제품 범위 결정이다. 다시 열려면 조건이 바뀌었는지 먼저 본다.

- **리소스:** 무료 · 공개 라이선스만(CC0 우선, MIT 류는 고지). 금지: 새 Quaternius QAL(기존 여덟 묶음은 둔다) · CC-BY · NC · ND · 불명확 · 상표 · 초상 · 유명 IP. credits 에 파일 · 원본 · 라이선스 URL · SHA-256 · 날짜 · 변환 경로. 직접 그리기 · 유료 생성 API · 자동 다운로드 금지. 받은 팩은 `/AssetLibrary/`, 쓰는 것만 `common/*_raw/<출처>/<묶음>/`(LFS). 온라인 키트는 합성 fixture.
- **들이지 않는 것:** 범용 스크립팅 VM(먼저 BT · `ConditionExpr` · 대화 · 애니 그래프에 노드), 네이티브 모드 DLL, 원격 명령 서버, 엔진 안 MCP, 런타임 아틀라스, GC, 별도 테스트 프레임워크, taskflow · magic_enum · FidelityFX SDK, 파이버 잡, 범용 할당기 자체 구현, 전면 ECS · Replication Graph, 물리 step 겹침, 컴포넌트 청크 풀, 모션 매칭, 롤백 넷코드, sccache · C++20 모듈 · 분산 빌드, 테스트 자동 재시도, 함수 핫 패치.
- **게임 · 키트:** 시험용 게임은 Playground 하나 — 모든 기능은 Playground 장소 · 미니게임에서 쓰이고 시나리오로 확인된다. 키트는 모두 링크하고 게임 기능이 등록만 켜고 끈다(런타임 모듈 로드 기각). 게임 이름 · "대형 오픈월드" 키트 금지, 장르 키트는 두 번째 사용처에서 같은 계약이 확인될 때 나눈다. DLC 는 세이브를 깨지 않는 선택 확장. 하우징 구조 시뮬 · 동시 건축 제외. 모드는 쿠킹된 데이터만(외부 모드는 서명 · 권한 먼저). 비행 · 코드 에디터 · 마켓 · 클라우드 협업 · 멀티 유저 편집은 범위 밖.
- **에디터:** UI 스레드는 나누지 않고 MVVM 까지(비동기화로도 응답이 기준을 넘는 측정값이 나오면 다시 연다). 소스 관리는 Git 만.
- **길드 창고(O-10):** 이전 불가 자산은 맡긴 당사자가 허용한 권한 안에서만 다른 계정으로.
- **플랫폼 · 빌드:** 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`)의 ReflectionParser · App. xxh3 금지(느리면 CRC32C). Server 배포 구성은 묶음마다 검증.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다. 줄끝은 `* text=auto eol=lf`.
