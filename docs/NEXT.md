# 할 일

> 결론: 할 일 · 계획은 이 문서 하나다. 끝난 줄은 지운다(기록은 git). 일할 때 지킬 규칙은 `AGENTS.md` 목차의 `docs/agents/`, 절차는 스킬(`preflight` · `linux-verify` · `scenario-debug`).

- 작업자는 이 문서를 고치지 않는다 — 통합 커밋이 줄을 지운다(커밋 메시지에 `감사 <ID>`). 코드 주석의 `감사 <ID>` 는 `git log --grep '<ID>'` 로 찾는다.
- 측정 줄은 문턱을 넘을 때만 고치고, 못 넘으면 값을 커밋 메시지에 남기고 줄을 지운다. "켤 코드" 커밋은 따로 — 값이 흔들림 안이면 그 커밋만 되돌린다.
- 차례: 결함 · 계약 → 구조 장치(형식으로 막기) → 측정이 문턱을 넘은 성능.

## 진행 중(통합 때 지운다)

- RHI 정리(w-rhiclean).

## 1. 다음 순서

1. **zonecutscene 결함 후보:** 안에서 생긴 겹침 진입이 8 프레임 안에 온다는 가정(shortcut 주석) — 모자라면 물리가 "막 더한 바디" 를 표시. 씬 XML 로 붙인 원격 조종자가 시작 때 `RemoteController.NoConnection` — 형식으로 막는다. `setInputPeer` 공개를 닫는다. 와일드웨스트 매니페스트에 `GF_NetSequence` 를 더한다. 서버 판정이 필요한 볼륨(자동 저장 · 튜토리얼)의 `hasActivationAuthority` 기본 참을 고친다. `ZoneTracker` 진입에 나타남 규칙을 넣는다.
2. **머티리얼:** `_base` 부모를 고쳐도 자식이 옛 값을 쥐는지 확인한다. 정적 변경은 부모가 선언한 런타임 스위치 색인으로만, 값 API 는 정적 종류를 거절한다(런타임 키워드 값 변경이 굽지 않은 조합을 만든다).
3. **허브 흡수:** 7 절 표를 단계 5 부터 한 갈래씩. 이름 바꾸기 도구가 못 하는 곳은 그 도구에 더한다.
4. 씬 스트리밍 (라), 렌더 그래프 ③ · ④, 헤더 팬인 ③ · ④(혼자 마지막), 에셋 GUID 2 · 3 단계(8 절).
5. **엔진 전수 조사 남은 몫(6 절):** 구조 장치 → 에디터 · 키트 → 성능(측정 뒤)을 갈래로 나눠 맡긴다. 씬 관리 · 월드 스트리밍 전수 조사(사용자 요청) 결과가 없으면 다시 돌린다.
6. **감사 열린 항목(9 절):** 묶음마다 "확인" 테스트를 먼저 써서 지는 것을 본 뒤 고친다.

## 2. 통합 빌드에서 볼 것

- **5-4 리플렉션(감사 A-24):** 생성 TU 비중 Release 13.4 %(목표 8 % 미만). 후보 둘(`ReflectionMetadataText`, `FunctionDesc::invokeConstructor`)을 커밋마다 Release 로 지어 `py -3 Scripts/dev/build_time_report.py --preset Ninja-Release --top 20` — 흔들림 안이면 그 커밋만 되돌린다.
- **RHI 5-2:** `D3D12.ComputeTimestampClock` 어긋남, Vulkan 비동기 컴퓨트 `GPU.<패스>` · Tracy 겹침.
- **실장치 · 백엔드:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯, DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(`RT.Frame.transientPlacements`, 검증 레이어 경고 0 — lavapipe · Android 포함).
- **손 확인을 시나리오로:** 에디터 설정 표 값을 고친 뒤 Ctrl+Z(형식 페이지 그리드는 아직 단계를 올리지 않는다), 'Game Config - Shooter3D' 열어 저장, 옵션 메뉴 렌더 품질 변경 → 그림자 · 텍스처 풀 값.
- **배포 도구:** N2 `py -3 -m Scripts server-image`, G1 `slice` 12 게임.
- **에디터 MVVM:** w-edthread4 · w-edmvvm2 통합 빌드 확인. 남은 View 정리: `Common/GUI` 나머지(상태 표시 · 도킹 레이아웃 · 테마)를 `[panelViewBan] views` 로.
- **preflight tidy:** tidy 를 겹친 첫 preflight 에서 캐시 적중 · changed 시나리오 영향을 실측한다.
- **D3D11 첫 시나리오:** DXBC DDC 키가 바뀌어 첫 실행 컴파일이 늘어난다 — 정지 시한에 걸리지 않는지 본다.

## 3. 알려진 실패 · 흔들림

- **`Scenario.playground.desktop.editor.playreload` 격리(만료 2026-11-27, `Config/Rules/Test/Quarantine.json`):** 게임 모듈 교체가 시계를 170 초 넘게 붙잡는 단계 — 격리를 풀고 다시 돌린다.
- **shooter3d dx11 골든 한 번씩 짐:** crowd `fight` 25.7 %, enemydeath `death_b` 1.37 %. 다시 지면 같은 체인 앞 단계와 `scenario_bisect --golden-margins` 로 화소 자리(dx11 군중 장면의 정착 조건 의심).
- **`editor.tilemapdatalayers` 단독 실패:** `<EditorText>` "글 칸이 키보드 초점을 잡지 않았다" — `EditorSelfTestInput::isKeyboardTargetStable` 를 `scenario-debug` "단독으로만 지면" 으로.
- **Linux:** sequencercuts · sequencerkeys `SequencerUnboundCount`(`--gpu software chain editor` 로 가른다), netresourcedigest, 에디터 체인 단계 약 18(입력 초점 의심), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`(재현 안 되면 지운다). `editor.screenrectview` 골든 1.09375 %(`all` 에서만) — 그 사진 앞 대기를 정착 조건으로. 골든: 에디터 `.linux` 24 장, empty 의 boat · decals · distortion(없음) · realtimegi · surfaceshowcase 2 · tessellationstage · geometrystage(Windows 와 57 ~ 100 % 다름). 데스크톱 WebGPU 골든 0(`scenario_bisect.py --golden-coverage`).
- **`AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`:** 전체 실행 한 번 "base … could not be read" — 다시 지면 앞 케이스와 스크래치 도메인 마운트 상태.
- **라이브 리로드(Debug 전용):** Building 중인 변형이 옛 바이트코드를 집고 옛 키 항목이 쌓인다.
- **preflight 밖 손 빌드:** Ninja 재압축 이름 바꾸기 실패(`.ninja_deps` 실종)를 마저 끝내는 보정이 preflight 에만 있다 — 손 빌드에도 넣는다.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 4. 플랫폼(웹부터)

- **웹 실행:** Web-Shipping 빌드 → `py -3 -m Scripts web-check --game Empty`(WebGPU · 오류 없음 · 화면 PNG · IDBFS 영속 · `[WebAudio] output started` — `WebAudioBackend` 는 아직 컴파일한 적 없다). 입력 · 클립보드 판정용으로 웹 시나리오 쿠킹이나 앱 반응을 페이지로 내보내는 길을 먼저 낸다. 웹 `waitIdle` 호출 스택을 웹 시나리오로 본 뒤 `WebGPURHIDevice` 의 프레임 콜백 대기를 `waitFrameFence` 로.
- **웹 온라인:** https 페이지용 `wss://`(수락 쪽 TLS 종단), 게임 세션의 브라우저 길(WebSocket · WebTransport 데이터그램).
- **Android 실기기:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 기동 · 회전(pre-rotation) · APK 팩 읽기(`AAssetDir_getNextFileName` 폴더 앞머리) · 터치 · 클립보드(JNI `ClipboardManager` Looper 제약) · AAudio 장치 분리 → 다시 열기 · IME 조합(한국어 키보드로 UI 글 칸에 "한글", 조합 중 밑줄).
- **Fileserve 실기기:** Android-Release 에서 팩 하나를 다시 구운 뒤 `adb reverse` + `-fileserve` 기동 시간 대 `android-apk` 다시 묶기 + 설치 — APK 길의 1/2 아래가 아니면 구간 크기 · 동시 받기를 본다.

## 5. 측정(조건을 넘으면 고친다)

- **에디터 첫 패널 · Hierarchy 8000(E1):** `App -EnableEditor -gv_editorPanelTimes=120 -gv_traceSlowTickMs=100` — 첫 그리기 50 ms 넘는 패널은 준비를 `onPanelOpened` 잡으로(Model `isReady()`, View 는 준비 전 한 줄), 100 ms 넘는 프레임 0. 큐브 8000: Hierarchy 정지 avg 0.5 ms 미만, Ctrl+A · 이름 거르기 16 ms 미만.
- **S8 후보(Release, 반복):**
  - 대기 호출 표: `OnlineServerCallTableTest.CompletionCostByPending` — 차이가 흔들림보다 크면 `_mapCall` 을 둔다.
  - 반사 검증 중첩 타입: `ReflectionValidationTest.NestedElementCost` — 흔들림보다 작으면 `NestedTypeCache` 를 되돌린다.
  - 파서 토큰화: `ReflectionParser.TokenizeStats` — 10 % 넘으면 토큰 저장소 재사용(기본 인자 · 매크로 이름 · 별칭 fixture 결과 보존).
  - 씬 저장: `game/abilityarena/automation/editor/scenesave`, `Scene.save.*` — write 가 16 ms 넘고 serialize 보다 크면 `base` · `write` 를 워커로(성공 전에 source path · dirty 를 완료 처리하지 않는다).
  - 씬 로드: `instantiate` 가 우세할 때만 순수 전처리 병렬(생성 · 부착 · 등록은 게임 스레드).
  - 씬 쿠킹: `--cook-all` DDC 없이 · 있이 — scenes 가 벽시계 절반을 넘고 가장 느린 씬이 합의 1/4 아래면 씬 단위 병렬(캐시 격리 · 결과 병합 먼저).
  - 게임 스레드 에셋 로드: `AssetLoadProfiler` 스톨 상위만 비동기로.
  - 팩 씬 청크 고르기: `PackCooker.SceneChunkPlan` — 씬마다 다시 훑는 몫이 팩 쿠킹의 1/4 을 넘으면 `ReachTraversal` 뿌리 표식으로 한 번에.
  - GPU 업로드: shooter3d · meadowvillage 첫 진입 × 백엔드 셋 × `-gv_gpuUploadQueue=0/1` — pump 가 RT 의 10 % 를 넘거나 waiting 이 계속 0 이 아니면 RHI Thread 형 시제품. copy queue 는 중첩 이득을 잴 때만.
  - 렌더 그래프 폭: `-gv_renderGraphParallelMinWidth=2 · 3 · 4 · 99` × DX12 · Vulkan — 3 · 4 가 흔들림 밖으로 빠르면 기본값을 바꾼다.
  - AI 군중: `load/navcrowd` 가 문턱을 넘으면 지역 분할.
  - 그 밖: Tracy `GT.Frame` 자식 합 90 % 이상, 배치 크기 12 · 24 · 48, `load/*` × 워커 1 · 2 · 4 · 8, W25 GPU 비교(c05a3ee251 대 지금), W-S4 `-ftime-trace`, W-R1 RT 프레임 힙 할당, W-R2 PSO 조회, 5-2 `RT.Bind.*`, DX12 기록 경로 락 제거 전후 `bench.json` p50(291d69074e 와 그 앞), 골든 여유, 잔상 메모리 `game.empty.afterimage`, 볼륨 안개 `RT.Fog.Volume`, HDR 퍼뮤테이션의 SDR `bench.json` p50, 렌더 디버그 꺼진 상태 비용 `bench.json` p50.
- **bindless 다시 쓰기(감사 R-6):** `GT.Material.textureResyncs` 밉이 오르는 동안 0, `RT.Bindless.mirrorWrites` ≤ 3 × (등록 + rebind) · 등록 시간 증가 1 µs 아래, dx12 · vk 검증 레이어로 shooter3d · load 체인 디스크립터 메시지 0, `load/texturebudget`(4K 300 장) 지난 뒤 120 프레임 안에 예산 아래 · 골든은 예산 없는 실행과 1 % 안.
- **텍스처 스트리밍 밉 범위(R-6 · A-12):** `TextureStreaming.bytesInFlight` 늘 `_maxBytesInFlight` 아래, 첫 화면 읽은 바이트(2K 이상) 전체의 10 % 아래 · 느리면 첫 로드만 전체 읽기로, 화면에 든 뒤 전체 밉까지 p95 가 10 프레임 넘게 늘면 `_maxBytesInFlight` · 프레임당 제출 수를 올린다.
- **자동 렌더 배율(RN-8):** Shooter3D `benchmark` vsync + 자동 배율이 부하가 걷힌 뒤 `_upFrames` × 단계 수 프레임 안에 1.0, 시계 켬 · 끔 `RT.Frame` p50(dx12 · vk · gl) 흔들림 안, `renderscalestep`(0.7 ↔ 1.0) `RT.Frame.transientAllocations` 0 · 프레임 시간 중앙값 1.2 배 아래, 0.7 그림 대 `gv_renderScale=0.7` 다섯 백엔드 차이 1 % 아래.
- **입자 정렬(RN-15):** `vfxsort` 다섯 백엔드 골든(두 뷰 모두 먼 입자가 먼저, 정렬 끈 그림과 5 % 넘게 다름), `RT.VFX.sortParticles`(방출기 32 × 256) 0.2 ms 넘으면 16 비트 기수 정렬, `GPU.VFXSort` 0.1 ms 아래, `None` 투명 패스 GPU 시간 앞뒤가 흔들림 밖이면 간접 읽기 없는 PSO 변형.
- **메시 인덱스 전환 뒤:** 모든 골든 대조(차이는 버그)와 농장 · 군중의 캐릭터당 GPU.
- **글리프 아틀라스 압축(감사 RN-14):** `App --test --test-filter=GlyphAtlasTest.HangulScrollGlyphsRasterized` 를 1e4d3dbe8c 와 fc54031411 에서 — `rasterized` 가 흔들림 밖으로 안 줄거나(예상 30184 → 약 15300) `missing` 이 프레임당 10 자를 넘으면 `_glyphRetainFrameCount` 를 1 로 재고, 그래도 지면 fc54031411 만 되돌린다.
- **구독 제어 블록(C-20):** `DelegateTest.SubscribeCost` — ns/짝 증가가 30 ns 를 넘거나 `load/characters` · `load/housingbulk` 의 `Scene.load` 차이가 흔들림 밖이면 출처가 생성자로 스레드 정책을 고른다(한 스레드 출처는 원자 · 대기 없음).
- **팩 규모(A-9 ~ A-12):** `PackScaleTest` 로 블록 크기 확정(64 · 128 · 256 KiB, 크기 차이 2 % 안이면 128 KiB). `lookupNs` 가 해시 맵의 1.5 배를 넘으면 해시 앞 8 비트 버킷 표. `DerivedDataCache::store` 출력 통째 · `PackPatchBuilder` 앞 빌드 팩 읽기 같은 통째 읽기 자리를 센다.
- **출하 할당기(C-6):** `MemoryProfiler` 프레임당 할당 수 × `MemoryBenchTest.AllocateFreeCost` ns 가 프레임의 3 % 이상이거나 8 스레드 ns 가 1 스레드의 3 배 이상이면 `SW_MEMORY_BACKEND=mimalloc` 비교 — 프레임 p50 2 % 또는 씬 로드 5 % 이상 빠르고 최고 메모리 증가 10 % 이하면 Shipping 기본으로.
- **비밀번호 해시(O3-5):** `login_storm` 을 가벼운 해시 없이 해시 처리율의 0.5 · 1 · 2 배 도착률로(O-12 열린 루프 전에는 닫힌 루프로 대기 최고치만) — 2 배에서 다른 키트 p99 가 한가할 때의 1.5 배 이내, 최고 메모리 기준 + `_maxConcurrent × 38 MiB` 이내, 거절은 모두 `kBusy`. 실제 해시 부하 구성은 `Config/Server/Online/` · `OnlineLoadBot`. O3-2: 저장소 람다 안의 `ILoginCrypto` 밖 암호 호출(서명 · 압축)을 찾아 보고한다.
- **자원 구간 할당(R-4):** `load/materialinstances` 에서 할당 수(`RT.Vulkan.allocationCount` · DX12 커밋 자원) · 프레임 p50 전후 — 1/10 아래로 줄고 프레임이 흔들림 안이면 둔다.
- **N×M 측정 후보**(재고 나서 해시로): `DataInheritance.cpp` `findEntry`, `ComponentRegistry.cpp:60,75`, `PrefabPool.cpp:80,92`, `ShooterNetPawnComponent.cpp:535,571`, `MaterialCache.cpp:350`, `DestructionState.cpp:779,855`, `ContentBrowserCollectionStore.cpp:108`, `EditorAssetFileCommands.cpp:154,199,569`, `EditorNodeGraph.cpp:333,543,696`, `ModResolver.cpp:161`, `ProjectSettingsDocument.cpp:45,62`, `ComponentStateStore.cpp:82`.
- **컨테이너(Release `App --test --test-run-disabled --test-filter=ContainerBenchTest.*` 5 번 중앙값, Shipping 으로 한 번 더):** `VectorGrowString` 이 예약한 경우의 1.5 배 이상이면 `is_trivially_relocatable` 표식 형식만 `memcpy`, `VectorShiftBitwise` 가 `memmove` 의 2 배 이상이면 `Memory::move` 한 번, 성장 배수 1.5 는 메모리 최대치와 함께, `UnorderedMapFind` 원소당 20 ns 이상이면 열린 주소 + 제어 바이트 그룹(없는 키 20 % · 있는 키 10 % 빠르고 순회 · 삽입이 안 느려질 때만), `flat_map` · `flat_set` 저장소를 `sw::vector` 로(`std::vector` 반복자 가정 호출부를 먼저 센다), `ChunkedArray` 자체 구현은 쓰는 곳이 늘 때. AC-1: `list.h` · `set.h` · `unordered_set.h` 범위 연산과 `string` 범위 `append` 의 본문 복잡도 확인.
- **잠금 종류(Release `--test-filter=LockKindBenchTest.*` 5 번 중앙값, 스레드 2 · 4 · 8):** `RenderPSOCacheLookup` · `CodecRegistryLookup` · `ShaderBindingLayoutLookup` — 1/1000 · 1/100 쓰기에서 4 · 8 스레드 `shared_mutex` 가 20 % 이상 빠르고 2 스레드에서 5 % 넘게 느리지 않으면 `shared_mutex`, `SpinLock` 이 둘보다 20 % 이상 빠르면(잎일 때만) 그쪽, `ShaderBindingLayout` 은 1/10 에서 30 % 넘게 느리면 두기. `LockedSwapQueuePush` 4 · 8 생산자에서 `SpinLock` 이 10 % 이상 빠르고 2 생산자에서 안 느리면 기본 잠금 인자를 `SpinLock`. `JoltJobQueue` 는 어느 스레드 수에서든 `ConcurrentQueue` 가 10 % 넘게 느리면 `SpinLock` + `deque` 로 되돌린다. 벤치 없는 후보(프로파일러 잠금 대기 먼저): `AudioClip::_mutex` · `PrimitiveRegistry::_mutex` → `shared_mutex`, `TransientAttachmentPool::_clearedMutex` · `ConstantBufferPagePool::_mutex` · D3D12 · Vulkan `_cmdListPoolMutex` · `_liveCmdListMutex` → `SpinLock`, `_transferJobMutex`(`RHIReleaseQueue::_spinLock` 과의 순서를 교착 검출로 먼저), `RHIMemoryTracker::_arrStat`.
- **큐 칸 정렬:** `TestQueueCellAlignmentBench` — 정렬을 뺀 판의 처리량이 3 % 넘게 안 떨어지면 칸 정렬을 뺀다. 로그 큐 칸 크기는 줄 길이 분포를 센 뒤 줄인다.
- **오디오 실시간 스레드:** 허브 부하 시나리오에서 `Audio.RealtimeAllocations` · `RealtimeLocks` · `OutputUnderruns` · `RetireOverflows` 0.
- **게임 프레임워크:** 세이브 찍기 · 디렉터 걸음 시간이 Android 4 ms 를 넘으면 게임 스레드 밖으로. 미니게임 나가기 메모리(`Minigame.LeaveAssetCacheBytes` · `LeaveHeapBytes`). 칸 바뀜 비트는 `net-pair` 측정 뒤(엔티티 상한 255 B). 카탈로그 이진 꼴은 Android · 웹 카탈로그 읽기 합이 50 ms 를 넘을 때(XML 데이터 이진 쿠킹과 한 측정으로).
- **엔진 등급 · 틱:** 허브 부하에서 컴포넌트 틱이 10 % 를 넘으면 틱 주기 등급. Android 코어 등급 워커는 실기기 측정 뒤. 명시 SIMD 형식(`Float4x`) 확대는 Engine 구간 측정이 2 % 를 넘을 때.
- **리플렉션:** 생성 코드 상수 표화 · 컨테이너 래퍼 얇은 템플릿(`VectorWrapper` 인스턴스 1,702) · 원격 호출 서버 측 비용을 먼저 잰다. 쿠킹본 고정 배치 읽기(`BinarySerializer` 태그 없는 방식, 스키마 해시가 같으면 선언 순서)는 그 뒤. `InspectorLayoutCache` 의 `findDisplay` 가 그리드 그리기의 5 % 를 넘으면 표시 정보를 묶음 행 곁 배열로.
- **에디터:** 차이 저장은 기록 16 MB 또는 편집당 1 ms 초과 때. 임포트 우선순위는 임포터 스레드 안전성 확인 뒤. 패널 지연 생성 · 교체 때 상태 직렬화 비용.
- **CMake 기준선 뒤:** 고치는 파일을 유니티 묶음에서 빼기, configure 시간, 주석만 바뀐 헤더의 ABI 스탬프 재링크, split DWARF.
- **Scripts:** 린트 규칙별 · 훅 호출 시간을 찍고, 훅이 `Config/Dev/Verify.json` `_hookBudgetMilliseconds` 를 넘으면 `py -3 -I -S` 와 PostToolUse 매처 좁히기. 시나리오 체인의 멈춘 단계 뒤 이어 돌기 · 나눠 주기 배정을 기록에서.
- **작업 시스템(계기 `Wait runParallel` · `Task.Join.*` · `Task.Idle.parks` 먼저):** 묶음 제출 `submitBatch`, `runParallel` 우선순위 레인, guided 청크, 후속 하나 이어 실행, 단계 그래프 위상 순서 재사용.
- **측정 뒤에만:** DX12 PipelineLibrary(차가운 시작 PSO 합), 깊이 전용 배치 키(그림자 드로우 수), 서버 MMO 관찰자 거리 기반 틱 주기 · 컴포넌트 TickInterval 버킷, 서버 쿠킹의 축약 애니메이션 클립, 온라인 프로토콜 퍼저 타깃.
- **GPU 구동 그리기:** 개수 버퍼 압축은 `RT.Draw.gpuBatchCount` 대비 보이는 배치가 절반 아래이고 빈 간접 명령이 GPU 프레임에 드러날 때(DX12 · Vulkan · GL4.6 만, 머티리얼 bin 마다 간접 하나는 머티리얼 상수를 bindless 구조 버퍼로 옮긴 뒤). 지난 월드 행렬 GPU 복사는 `RT.GPUScene.motionHistory` 가 인스턴스 수만에서 드러날 때. 큰 투명 배치(`kGPUSortMaxElements` 초과) 다단 bitonic 은 그런 배치가 생길 때.
- **Shooter3D 소총 자세 클립:** CC0 poly.pizza "SWAT"(Quaternius, `https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`, `Idle_Gun` · `Gun_Shoot` · `Run_Shoot`)를 `models_raw/quaternius` 에 넣고 UBC 로 리타깃해 상체 층(Aim)으로, IK 견착은 손 자리 보정으로. "지금 이 섹션으로" 재생이 필요하면 `AnimationPoseNodeRegistry` 에 슬롯 노드 한 줄.

## 6. 설계 할 일

### 렌더링

- TransientLeasePool 을 뷰 풀에, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, VFX · 프로브 디스패치 전이를 묶음 API 로.
- 셰이더: 공통 `PixelContext` · 표면 평가와 패스별 출력 분리, 표면 모델 · 필수 입력 선언과 미지원 조합의 쿠킹 실패 — Lit · Unlit · Emissive · 툰 · 알파 마스크 패스 분류 목록부터. 기믹 표면(`gimmicksurface*.material`)이 직접 적은 `MATERIAL_FORWARD_ONLY` 를 골든과 함께 뺀다.
- **R-15 후속 1 — GI 복셀화를 GPU 원본으로(`SceneVoxelizer.cpp`):** 프레임 그래프 실행기가 첫 리스트 앞에 고정 기록하는 프레임 업로드 단계(파이프라인 XML 패스 아님)에서 정점 풀 GPU 복사 · 전이를 기록(풀 정점 버퍼 GPU 전용, 인덱스는 CPU 그대로). 복셀화는 기하 셰이더 없는 3 축 투영(삼각형 축은 컴퓨트로 미리, 픽셀 셰이더가 3D UAV 원자 쓰기, 얇은 벽은 복셀 절반 바깥 확장). 스킨 · 모프 메시는 CPU 정점을 쥔다.
- **R-15 후속 2(1 뒤):** `MeshCPUAccess` 를 `Mesh::create` · `MeshCache::request` 에 기본값 없이(절차 · 스킨 · 모프 Keep, 요청이 갈리면 Keep, 놓인 뒤 Keep 요청이면 `.mesh` 다시 읽기), 업로드 뒤 CPU 정점을 놓고, 장치 교체 때 `.mesh` 를 다시 읽는다.
- **렌더 그래프 ③ 뷰 패스:** 뷰 그래프 실행마다 뷰 표에 뷰 소유 자원을 등록하고 `importResource`, 레벨 콜백이 그 표를 본다. 차례: 자동 노출(측정을 뷰 패스 `AutoExposure` 로, 톤맵이 입력으로 읽음, 원본 `prepareTextureForShaderRead` 삭제) → `HZBOcclusion::recordInViewPass` 의 두 짓기 → `FramePassExecutorPasses` · `Draw`(디퍼드 타일 분류 · 컴퓨트 후처리 · 그림자 층 · 아틀라스). 깊이 히스토리 복사의 텍스처 전이 3 곳은 `_listUnorderedAccess` 를 더할 때.
- **렌더 그래프 ④ 형식으로 막기:** 패스 몸통이 받는 명령 리스트를 전이 함수 없는 면(`RenderPassCommandList`)으로, `transitionBuffer` · `prepareTexture*` 는 그래프 실행기 · 프레임 끝만 쓰는 `IRHICommandList` 에. 판정: 단계마다 네 백엔드 골든 그대로, DX12 · Vulkan 검증 층 경고 없음, `RT.Graph.transitions` · 프레임 GPU 시간이 늘지 않음. 그 뒤 VFX → FrameGraphExecutor → FramePassExecutor.
- **Renderer 차례:** ② 직렬 execute 의 `_pCmdList` 누락 · 패스 순번 payload · 꺼짐 판정을 맨 앞으로 · 등록 조건 한 자리 ③ 고르기 마스크별 컴파일 계획 캐시(상한 · 계측) + Clear 를 계획에서 + 별칭 계획 값 비교 ④ 주 시점 = 뷰 목록 0 번 ⑤ 엔진 상수 칸 번호 계획 — `RT.Bind.passCbAcquires` 를 재고 FrameCB · ViewCB(C++ · HLSL 대조는 리플렉션 오프셋), M3(칸 고갈이면 그리지 않음)은 ⑤ 전에 잰다 ⑥ (배치, 패스 종류, 뷰 모드) → PSO 표(잠금 없음) + PSO 키 값 비교 ⑦ 확장 지점 등록 때 선언 → 이름 정리 → DX11 컬링 없음을 기동 때 알리고 진단 스위치는 배포에서 막는다. GPUSceneBuilder 의 카메라 의존은 `GT.GPUScene.build.*` 를 잰 뒤.
- **렌더러 규격(접근 핸들 · R-4 뒤):** 그래프 일시 버퍼(`graph.createBuffer`) → 뷰 히스토리, `RHITextureSlot`(텍스처 손 생성 · 뷰 등록 8 곳), 컴퓨트 패스 도우미(그룹 크기는 `[numthreads]` 리플렉션), 풀 버퍼(펜스 재사용), 되읽기 단일 면. RHI 자원 생성은 패스키로 막는다.
- **RHI ③ 남은 몫:** 이동 전용 소유 형식, bindless 번호(`RHIDescriptorIndex`)도 강타입 핸들로, 닫힌 리스트 토큰, 손실 보고 하나, 에디터 확장 경계의 uint64 텍스처 id.
- **RHI 5-2(측정 뒤에만):** Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, 서브할당(Vulkan 은 `RT.Vulkan.allocationCount` 가 허브 월드에서 한도 절반을 넘을 때).
- **L-2 상수 버퍼 기록(측정 뒤에만):** dx12 · vk, `-gv_renderGraphParallelMinWidth=2` 에서 `RT.Bind.mirrorLockWait` 합이 `RT.Graph.parallelLevel` 합의 2 % 를 넘으면 매핑 주소 · 슬롯 크기 · 미러 값을 버퍼 레코드(`_pMapped` · `_alignedSize` · `_lastWriteFrame` · `_pendingSlotMask`)로 옮겨 `_bindlessMutex` · 미러 전역 락을 없앤다(채울 목록은 리스트 기록 상태, 제출 때 한 스레드가 합침, 같은 프레임 두 번째 쓰기는 `_lastWriteFrame` 비교 교환으로 오류). RT 프레임 중앙값이 안 빨라지면 되돌린다.
- **자원 구간 할당 ④:** Vulkan VMA · DX12 D3D12MA(MIT)로 백엔드 메모리를 덩이에서 나눈다 — 사용자 결정 뒤(R-4 `MaterialRenderProxy.cpp:48`, 확인: 머티리얼 인스턴스 5000 의 버퍼 바이트 · Vulkan 할당 수).
- **bindless stride 검사 범위:** 인스턴스 · 배치 · 모프 · 머티리얼 버퍼도 `FrameResourceRegistry::registerBuffer( name, const RHIStructuredBuffer& )` 로 등록해 `validateStride` 를 받게 한다.
- 렌더링 런타임 디버그: 그래프 밖 버퍼를 읽는 컴퓨트 PSO 의 리플렉션(`ShaderBindingSlot::_listElementMember`)을 버퍼 이름에 잇는 표.
- 화질 남은 몫: 볼류메트릭 구름 · 비 가림 맵. 스카이 라이트 GPU 찍기는 하늘이 프레임마다 바뀌는 장면이 생길 때.
- 시나리오: 실시간 GI 정착 조건(넣으면 GI 장면 사진 시점이 바뀐다 — 골든과 함께).
- **키트 셋 가르기(규칙 · 디렉터 · 클라이언트):** TacticsSRPG 가 본보기(규칙은 키트 루트, 디렉터 `Component/SRPGGameModeComponent`, 뷰 · 입력 · HUD 는 `Client/` 의 `GF_Client_TacticsSRPG`). Fighting · Farming · CityBuilder 는 키트 안에 섞인 곳이 없고 디렉터 · 뷰가 폐기할 게임(Playground FightingDemo · HarvestValley · MeadowVillage · FrontierTown · NileCity)에 있다 — 허브 미니게임으로 옮길 때 같은 계약으로 키트 `Component/` · `GF_Client_<X>` 에 둔다. 나머지 순서(같은 계약): ActionPlatformer · ActionCombat · BattleRoyale · MechArena · Metroidvania · CardGame · KartRacing · PartyArena · Rhythm · AsymmetricHorror · CoopScavenger · GhostHunt · SurvivalHorror · ClassicJRPG · MonsterCollector · MonsterHuntRPG · OpenWorldWestern · CreatureLife · RestaurantSim · ThemePark · RealTimeStrategy · SideScrollConquest · ActionAdventure.

### 코어 · 리플렉션

- **5-3 병렬 오브젝트:** PO-1 근접 카운터 `Gimmick.ProximityCandidates`(노드 수, 후보 수)를 `GimmickCircuitComponent` 틱에 두고 게임 12 개 `slice` 최고치, 서비스 구간 `GT.Scene.service.<이름>` 으로 `load/characters` · `load/housingbulk` · `load/townsfolk` · Shooter3D `benchmark` 3 번씩 → PO-2 한 단계 직렬 서비스 합 0.3 ms 이상이고 겹치지 않는 둘 이상이 각 0.1 ms 이상일 때만 서명을 나눈다(`ISceneMainService::update` / `ISceneParallelService::updateOnAnyThread( SceneJobContext& )`, 매니저는 const, 쓰기는 `deferPostTick`, 게임 스레드 전용 API 는 `const GameThreadKey&` 인자, 파도는 `FrameSystemParallelPlan`, 뒤 `GT.Scene.tick.FlushEnd` 감소 + `TestSceneStateHash` 동일). 같은 열쇠 인자를 `ISceneService` 렌더 기여 훅(`contributeLights` 등)에도 → PO-3 4 워커에서 남는 약 5 ms 직렬 몫(요청 모으기 · 결과 반영 · Jolt 공용 상태)을 구간으로 잰다.
- **5-4 리플렉션:** W-R3 (a) 통합 빌드 `.gen.cpp` 합계(목표 4 MB 미만) · 생성 비중(8 % 미만) — 못 미치면 `ReflectionParser.EmitBytes` 에서 합계 25 % 넘는 방출 함수만 표로(`emitMethodInvoker` 는 못 바꾼다). W-R4 뒤 손 `serializeState` 본문(약 158 곳)을 키트별로 `StateIO::reflected` 로(소비자별 커밋 앞뒤 3 % 넘으면 그 커밋만 되돌림, 롤백 프레임 경로가 쓰면 타입별 칸 계획 캐시). RF-4 `ObjectStateSerializer` 상태 쓰기 · 읽기 짝과 `ComponentDefaults` 기본값 비교를 `ReflectVisit` 로. RF-7 · W-R6 밀집 타입 색인은 `TickRegistry::_mapTypeDispatch` · `GameObjectStore::_mapComponentPool` · `ComponentDefaults::_mapResolved` 조회 합이 게임 스레드 프레임의 1 %(또는 0.1 ms) 이상일 때 `TypeInfo::_typeIndex` + `TypeIndexedArray<T>`(5-3 SoA 와 함께). N1 칸별 변경 비트(`writeDelta`)는 대역폭 측정 뒤. FUNCTION: 인자 있는 메서드를 타입 위젯 · `ReflectCallResult` 오류 표시로, 그다음 반복 작업용 액션 목록.
- 리플렉션 적용 후보: `ScreenshotStabilityMode` 열거형 칸(데이터 철자 `stability="assets"` 113 · `"none"` 33, 61 파일을 일괄 다시 쓸지 정한 뒤), `RigCopyTransformNode::Mode`(노드 클래스를 헤더로 옮길 때), DevTools 손 이름 표(`ReloadSource` · `MemoryDiffStatus`)와 `AutomationRunnerParams.h` 의 낡은 "DevTools 는 코드젠을 하지 않는다" 주석 — `ENUM()` 으로 옮길지 팬인 점수와 견준다. 각 형식 `Radians` 남은 몫(벡터 각 `float2` 경사 띠 · `float3` 오일러)은 회전 형식을 정할 때.
- **이름:** `naming-words` 밖 멤버 · 지역 · 매개변수 형태(`_camelCase` · `out…` · 단수 컨테이너)를 단계적으로 자동 검사에 잇는다.
- **App 종료 코드(낮음):** `Config/Engine/ProcessExitCodes.json` 스키마에 플랫폼 종료 코드 범위 · 예약 코드 · 자동화 결과 매핑 누락 검사와 생성기 테스트.
- 형식 개명 남은 몫: `HousingPlotComponent`(GameMode 의 상태 바이트 세이브 · 비우기 수명을 쓴다 — 프리팹 스폰만으로 바꿀 수 없다), `UnitStatsComponent` 를 Runtime 으로 옮기려면 키트 형식(`ActionCombatEvents` · `MonsterCatalog`) 의존을 먼저 끊는다, `CueTriggerComponent` 의 `TriggerComponent` 상속을 정리한다. `Source/Games` 흡수 때 컴포넌트 이름을 기전으로 다시 짓는다([NamingCpp](agents/NamingCpp.md) "컴포넌트 이름").
- `FrameViewports` 가 서버 · 쿠커 타깃에 불필요한 의존을 끄는지 include 그래프로 보고, 위반이 재현되면 뷰 계약을 나눈다.
- 원점 이동 뒤 AI 인지의 마지막 본 자리(`AIPerceivedTarget::_lastKnownPosition`)가 옛 원점 기준인 것을 고친다.

### 엔진 전수 조사 남은 몫

- **구조 장치:** Action · Condition 레지스트리 → `NamedEntryRegistry<T>`. 리플렉션 `tryParseEnum<E>` · 배포에도 남는 열거자 데이터 철자(`ENUMERATOR(Text=)`) · 반사 동등 비교 · 해시 · 기본값 · 기반 형식으로 하위 형식 생성 · `FUNCTION(Exec)` 콘솔, `.cpp` 안 `REFLECT` 는 `#include "Foo.gen.inl"` 꼴로(익명 이름공간 FQN 충돌 키 먼저). 매크로: `SW_CONCAT` 사본 정리, 에셋 캐시 6 · 엔진 서비스 6 매크로를 하나씩으로, `SourceLocation`(`__builtin_FILE/LINE/FUNCTION`) — Error · Warning 로그 · `SW_FATAL` 은 함수로(`%#` 컴파일 검사 길 먼저), `-fmacro-prefix-map`. `HeadlessSubsystem::rejectClientOnlyTask` 표를 `CommandLineArgumentUsage` 의 `Client` 칸으로.
- **Dev → DevTools:** 엔진 개발 명령은 DevTools 로, 쿠킹 코드(`runCookTask` · `PackCooker` · `SceneCooker` · `ShaderCookDriver`)는 `DevTools/Cook` 으로(`-cook-all` 인자 줄은 `Dev`).
- **파일:** 자기 `.cpp` 만 포함하는 60 줄 미만 헤더 65 개 접기, 한 곳만 쓰는 도우미 헤더 36 개 합치기(`header-fanin` 합이 늘면 안 함).
- **쿠킹:** 셰이더 컴파일 증분 판정 세 벌(`.cookrec` · `cook.stamp`+`.deps` · 메모리 지문)을 `CookJob` 하나로. `DerivedDataCache` 생성자 비공개 + `open(bucket)`.
- **파일 감시:** OS 감시자 생성은 `FileWatchService` 만(열쇠 형식) — 루트마다 하나 공유, 프레임 안 경로별 합치기, 안정화 창. 다시 읽기 알림 · 텍스처 다시 읽기는 프레임 단위 일괄.
- **머티리얼:** 파라미터 핸들 `MaterialParameterHandle{index, offset, kind, layoutGen}` → 바뀐 칸만 덧칠 · 범위 업로드. 전역 값 · `MaterialParameterCollection` 을 전역 CB(`MaterialGlobals`)로. 인스턴스 모델을 MIC=`_base` · MID=`create` 둘로. `_permutations` 이름 단위 합치기. `MaterialPropertyKind` 를 리플렉션 내장 형식 특성에 잇기.
- **컨테이너:** `list` 는 LRU 전용 형식으로 바꾸고 지운다, `string` 자체 구현. 레이스 가드 별도 깃발 떼기는 측정 뒤(뗄 때 그 깃발 체인 하나를 preflight 에).
- **디버그 허브:** `DebugHUDRegistry` 를 범주 단위로 넓혀 그리기 · 감시 값 · `VisualLogger`(생산자 0) · 에디터 패널을 함께. `gv_*DebugDraw` 를 범주 켜짐으로. 차례: 내비 · AI, 물리, 렌더링, 네트워크, UI · 입력. 진단 getter 118 개는 감시 값 · 로그 참조를 다시 센 뒤 살림 · 지움으로 가른다.
- **에디터 · 키트(영향 순):** VFX 편집기, 폴리지 칠하기, 스플라인 점 손잡이, 물리 에셋 시각 편집, 패키징 플랫폼 선택, 로컬라이제이션 패널. 시퀀서 · 카메라 프리셋 · 충돌 프리셋 패널이 마우스 공통 층(EditorUI R10 ~ R13)을 붙인다. 키트: "꽂으면 도는" 컴포넌트 층(데모 래퍼를 키트 `Component/` 로), 카탈로그 편집 · 미리보기 일반화 + Map Check 키트 검증 등록점, 새 게임 · 장르 키트 생성기. 서버만 있는 키트(Social · Trade · LiveOps · ServerDirectory · NetTurnRelay)는 지우지 않고 붙인다. 검토: GF `Gimmick/Genre/*` 를 키트로, 호러 4 종 공통(은신 · 소음 · 조명) 합치기.
- **에디터 체인 단계의 뷰 누수**를 찾아 막는다.
- **성능(측정 뒤):** 동기 대기(히치) 먼저 — 프레임 단계 안 동기 로드 경고 · 렌더 스레드 디스크 IO 단언을 구조로 넣고, VAT 굽기 · Vulkan 파이프라인 캐시 저장 · 파괴 셰이프 · 프레임 경로 동기 에셋 로드 5 곳을 비동기로. 핫 경로 API 는 `string_view` · `hashed_string` 만. 리플렉션 로드 경로 칸 형식 해석을 `PropertyInfo` 캐시로. 생성 코드 크기는 측정 범위부터 맞춘다(1.65 MB 대 빌드 트리 9.33 MB).

### 에디터 · 도구

- 있는 기능의 Undo 누락 · 취소 불가 편집을 시나리오로 찾아 보강한다(감사 E-1 · E-2 · E-13).
- 첫 사용: 빈 상태(새 프로젝트 · 빈 폴더 · 첫 임포트 · Map Check 오류 · 첫 패키징)에 다음 행동을 잇는다.
- 시각 저작 첫 사례(애니메이션 마스크 · 소켓)로 공통 코드가 있는지 본다 — 완료는 XML 왕복 · 다른 스켈레톤 · 재로드 배치 일치 · `uiaudit` · `uicapture`.
- 머티리얼 제작 UX: 실제 제작 시나리오로 병목을 찾은 뒤 게임 재료 시작 그래프 · 미리보기 대표 메시 · 조명 선택 · 누락 텍스처 · 쿠킹 상태 인라인 표시 가운데 고른다.
- 게임 액션 저작(사용성 문제가 확인되면 이 차례): 등록 메타데이터(설명 · 분류 · 문맥) → 에디터 분류 · 검색 · 인라인 문제 표시 · 동작 교체 시 호환 안 되는 값 처리 → 범용 그래프 전에 막히는 콘텐츠 사례 수집. 완료는 XML 왕복 · 잘못된 참조 거부 · 화면 검토.
- **도구(문턱을 넘을 때만):** V2 timeline 모으기 → V3 그리지 않는 대기 → V4 shooter3d 적용, T2 체인 샤드(가장 긴 체인이 벽시계의 40 % 를 넘을 때 `SHARDS n`, 시간표 `Config/Dev/ScenarioCosts.json`, 단위 샤드는 합계 60 초 넘을 때, Release ctest 벽시계가 10 % 이상 안 줄면 되돌림), R3 에셋 색인 디스크 캐시, E2 문턱 넘는 패널, D3 공유 DDC 측정.
- **preflight tidy 캐시:** `Saved/TidyCache` 정리, MSVC STL · SDK 판을 캐시 키에 넣는다.
- 리다이렉트 표 ②: `shipping-check` 가 팩 목록에서 도메인마다 `redirects.xml` 이 담겼는지 확인한다.
- 퍼저 타깃(신뢰 경계 파서만): 게임 세션 수신 패킷 디코드, `JSONDocument`, 세이브 머리. 크래시 입력은 `Tests/Snapshots/` + 단위 케이스. exec/s 100 미만이면 입력마다 범위를 다시 세우는 것을 고치고, 30 초 뒤 `cov` 가 씨앗 재생 직후보다 안 늘면 타깃이 죽은 것.
- 체크포인트 · 리스폰: 아바타가 죽는 게임(LanternVale 아바타 사망 규칙)이 생기면 그 게임 `netpair/` 에 두 프로세스 시나리오.

### 데이터 · 게임플레이

- 매직 리터럴 기준선이 큰 파일의 튜닝 값과 게임 모드 상태 `switch` 를 PROPERTY · 규칙 카탈로그로, `sw::expected` Core 밖 이전.
- `FighterCatalog` 엄격도와 `ActionPlatformerCatalog` 전이 검사를 참조 표로 옮길지 정하고, 잘못된 참조가 쿠킹 · 시작을 통과하는 회귀 테스트.
- 카탈로그 키: 쿠커가 상속을 따라가게(키 칸을 든 형식을 물려받으면 표 대조 테스트가 멈춘다), 같은 파일 안 정의끼리의 참조(`_dropID` · `_replacedID`)를 `<Reference>` 줄로.
- `GameInstance` 조정 책임: 수명 · 호스트 · 실패 정책이 다른 책임만 나누고 `update` 순서는 계약 테스트로 고정한다.
- 키트별 리소스: 각 게임 `credits.md` 를 키트별로 이어 중복을 막는다.
- 시험이 없는 기능에 시험을 단다 — 단위 · 시나리오 둘 다 없음: SmartObject · 소프트바디 · HDR 출력. 시나리오 없음: 3D 파괴 · 표정 · 립싱크 · AI 인지 · 스폰 디렉터 · 락온 · 턴 순서 · 인벤토리 · 제작 · 상점 · 날씨 전환 · 데칼 비 씻김 · 카메라 흔들림 · 사진 모드 · 스플라인 강. 단위 없음: GrabberComponent · 튜토리얼 힌트 · 안개 · 무기 교체. 완결 루프가 빈 장르 게임만 수직 슬라이스 시나리오.
- 없는 기능을 만든다(Playground 장소 · 미니게임에서 쓰고 시나리오로 확인): 텔레포트 장치 · 포털, 풀베기(FoliageComponent 인스턴스 제거 API), 나무 베기(Fracture · RigidBody · LootTable), 피 데칼 · 지속 출혈, 번개 · 비 · 눈 파티클 · 쌓이는 눈 · 웅덩이 고임 · 마름 · 밟으면 튐, 실내 차폐(지붕 광선 · AudioRoomGraph), 날씨 · 낮밤 → 오디오 · 조명, 바람 → 천, 3D 등반 · 맨틀 · 수영 · 로프 · 사다리, 상자 열기 · 줍기(LootTable), 이모트 · 미니맵 · 낚시.
- 시연 컴포넌트 19 개가 카탈로그를 경로 칸으로 직접 읽는다(`FightingDemoComponent.cpp:91` 등) → `GameFeatureScope::loadCatalog<T>()` 로.
- 미니게임 결과(`MinigameResult`) → 경제 · 진행 매핑(복원 뒤에). 허브가 쓰는 키트(스트리밍 오픈월드 · 하우징 · 농사 · 상점 …)를 허브 맵에 배치(흡수 단계마다), 전용 서버 허브(NetMMO), 매치형 미니게임은 매칭이 미니게임 이름으로 세션을 가르고 결과를 허브 서버에 보고. 로컬 멀티 미니게임이 생기면 `InputUser`. 미니게임 미리 읽기.
- 키트: MMO 키트의 부하 목표와 운영 비용.
- 상용 대비: T3 샤드 시간 균형(외부 CI 러너는 사용자 결정), U1 방아쇠 진동 · Android Vibrator(백엔드가 생길 때).
- 퀘스트 목표가 처치 · 상호작용을 셀 때(목표 형식이 그것을 셀 때 구현): `QuestLogComponent` 가 `onRegister` 에서 "game" 채널의 `InteractionCompletedEvent` · `AbilityOwnerDiedEvent` 를 구독하고(`onUnregister` 에서 끊는다) 자기 조종자의 폰이 낸 것만 `QuestLog::notify( kind, target )` 로 센다 — 받는 쪽이 0 · 여럿인 선택적 전역 알림이라 이벤트 버스가 맞고(필수 효과면 받는 쪽 인터페이스), 일지 주인이 구독해야 플레이어마다 따로 센다(언리얼 Lyra 의 `UGameplayMessageSubsystem` 구독자 자리).
- 받는 쪽 없는 "game" 채널 이벤트(제품 코드에 `EventDispatcher` 구독이 하나도 없다): `GimmickDamageEvent` · `DamageAppliedEvent`(컴포넌트 델리게이트만) · `PossessionChangedEvent` · `SaveGameSavedEvent` · `SaveGameLoadedEvent` · `SceneLoadRequestedEvent` · `SceneLoadCompletedEvent` · `AbilityPresentationEvent`(`registerGameplayCue` 만) · `AbilityOwnerDiedEvent` · `InteractionCompletedEvent` · `RoomClearedEvent` · `PlayerDefeatedInRoomEvent` · `ClearGateStateChangedEvent`(셋은 테스트만) · `GimmickCueEvent` · `GimmickNoiseEvent`(연출 · AI 는 직접 건다) — 받는 쪽이 하나면 직접 호출로 걷고, 여럿이면 실제 구독자(HUD · 퀘스트 · 업적)를 잇는다. 기믹 시나리오: 쇼케이스에 동적 상자 · 탈것 · 경비 AI 가 없어 발사 · 부스트 · 소리 효과를 시나리오 값으로 못 본다(단위 테스트만).

### 씬 스트리밍 (라)

- 그리기 수가 문제가 되면 `MeshMerge` 대리를 같은 `_hlodFileID` 자리에. 인스턴스 메시에 LOD 고정 칸이 생기면 대리가 최소 LOD. 셀 로드와 텍스처 · 메시 스트리밍 우선순위 공유, 로드 큐 디버그는 개발 명령 · HUD 자리.
- 셀 안에 PROPERTY 밖 상태를 가진 컴포넌트(`ComponentStateStore` — 지형 편집)가 세이브를 넘게 한다. 바닥 광선이 비탈 · 가장자리의 큰 바디를 잘못 쥐면 셰이프 스윕으로.
- 원점 이동: 내비 · 군중이 원점 기준 질의를 받게 해야 길찾기 월드가 원점을 옮긴다. 하위 레벨 문서 읽기를 게임 스레드 동기에서 뺀다.

### 헤더 팬인 다이어트(혼자 마지막)

- ③ `TypeRegistry.h` — `TaskTypes.h` · `EngineMinimal.h` · `ReflectionCast.h` 를 cpp 로. ④ `ReflectionTypes.h` → `TypeInfo.h` 묶음은 리플렉션 생성기 템플릿(`.gen.cpp`)과 함께. 단계마다 점수 합 5 % 이상 · 그 헤더 점수 30 % 이상 안 줄면 되돌린다(Release · Shipping · ASan · 서버 증분 빌드).

## 7. 허브 흡수(한 단계 = 한 갈래)

단계마다 사용처를 옮기고 시나리오 · 골든이 통과한 뒤 폴더를 지운다. 골든은 이름 재기록만, CTest 체인은 미니게임별로. 통합 때 Release · Shipping · ASan 증분 빌드 + 표의 시나리오 먼저. 남길 것: 시험용 작은 둘째 팩 NileCity(`SW_GAMES` · `-game=` · `_cookRules` · `_bAutomationOnly`).

| 단계 · 갈래 | 내용 | 통합 때 돌릴 것 |
| --- | --- | --- |
| 5 `w-absorb-wildwest` | 허브 맵 바탕(OpenWorldWestern · 말 · 날씨 · 도착 컷씬), 골든 14 | 옮긴 9 + 날씨 · 자동 저장 |
| 6 `w-absorb-citybuilder` | FrontierTown → 도시 건설 미니게임(CityBuilder · RTS) | 옮긴 3 + `Scenario.nilecity` |
| 7 `w-absorb-harvestvalley` | 허브 농장(Farming · 낮밤 · NPC 일과 · 튜토리얼 힌트), `_listVillagerPreset` 를 Playground 로 | 옮긴 7 + `.editor` 1 |
| 8 `w-absorb-meadowvillage` | 허브 하우징 · 생물(NetSimulation · 넷 하우징), 골든 8 | 옮긴 5 + `.editor` 1 |
| 9 `w-absorb-themepark` | 테마파크(하위 레벨), 골든 25 | 옮긴 3 + `.editor` 2 |
| 10 `w-absorb-voxelcraft` | 광산(Voxel, 씬 전환) | 옮긴 3 |
| 11 `w-absorb-abilityarena` | 아레나(어빌리티 디버거 · 씬 저장 편집기 시나리오) | 옮긴 3 + `.editor` 2 |
| 12 `w-absorb-velmora` | 액션 어드벤처 + DLC 세이브 승계 → 허브 세이브 · ModSupport(`Test/Mods/velmora_tidecaves` 의 `-game=`) | 옮긴 1 + sequence 2 쌍 |
| 13 `w-absorb-starskirmish` | RTS 매치(`net: Lockstep`), `lockstep-pair` 의 `_GAME`, 팩 리다이렉트 표 | 옮긴 4 + load 1 + `lockstep-pair` |
| 14 `w-absorb-lanternvale` | 허브 서버(NetMMO · 계정 저장), `net-pair mmo` 의 `_MMO_GAME` · 로그 정규식 | 옮긴 2 + `net-pair mmo` |
| 15 `w-absorb-shooter3d` | 사격장 · 온라인 매치, `Config/Server/Online/Shooter3D*` → `Playground*`(비밀 환경 변수 `SW_SHOOTER3D_*` 도), `online-local` · `telemetry-local` 기본값, 프리셋 `SW_GAMES=Shooter3D` 3 곳 → `NileCity`, `sequential_scenario_packs`, 골든 510 | 옮긴 32 + showcase 8 + desktop 4 + sequence 1 + editor 2, 서버 `Server --test`, `online-local run` |

단계마다 같이 고칠 자리: `Scripts/dev/linux_scenarios.py` `ALL_CHAINS`, `Config/Dev/AssetReferenceFields.generated.json` 의 `owner: game:<게임>`(다시 생성), `Config/Rules/Lint/*` 의 `Source/Games/<게임>/` 경로, `Test/README.md`, `Test/Scenario/RunStateDirPair.cmake`(Velmora · Shooter3D 쌍). 팩 흡수는 `move-asset game/<옛> game/<새>/<하위>` · `move-source` · `rename-symbol`(모두 `--dry-run` 먼저), 옛 C++ 이름은 `_bannedNames`.

## 8. 에셋 정체성(GUID)

- **1 단계 통합 검증:** 단위 `AssetGUIDTest` · `AssetDatabaseTest` · `AssetPackIndexTest`, 시나리오 `editor.assetidentity` · `editor.assetmove` · `editor.flowimport` · `contentbrowser`(`.meta` 가 안 보이는지), 모든 쿠킹(`PackCooker.PackCookNoIdentity` 없음), `--check-text` · 머티리얼 그래프 생성 검사.
- **2 단계 — 참조를 GUID 로:**
  - Content 전수 이전: `migrate-asset-refs --fix` 로 `AssetRef<T>` 칸을 `guid:<32자>` 로(4003 개). 먼저 경로를 담은 같은 이름 글 칸(`GameFramework` `_material` · `_sound`, `GF_ThemePark` `_shape`, `GF_Server_Leaderboard` · `GF_Server_Matchmaking` `_catalog` — 값 59 개)을 `AssetRef<T>` 로 바꾸고 빌드로 `AssetReferenceFields.generated.json` 을 다시 낸 뒤 돌린다. 임포트 산출물 참조 898 개 · `_base` · `DataPath` 칸은 산출물 줄 뒤.
  - 세이브(`SW_SAVE_FORMAT_UPGRADE`)와 서버 기록(`Content/common/sql/*/migrations.xml`)의 경로를 GUID 로 — 배포된 팩 색인 + 마지막 리다이렉트 표로 한 번 풀어 쓴다.
  - 설정의 경로 칸(`enginedefaultassets.xml`), 게임 설정 · 카탈로그 `_base` 의 파일 부분. 경로 캐시 키는 `AssetRedirect::toCacheKey` 한 곳을 GUID 로.
  - 임포트 산출물 줄: 원본 GUID → 산출물 경로를 임포트 기록에서 팩 색인에. 내용 주소 공유 파일(`textures_raw/_shared/`)은 저장소 경로 해시 GUID 로 `.meta` 를 함께.
  - 옮기기용 리다이렉트를 걷고 GUID→GUID 표(정체성 대체 · 합치기용)만 남긴다(읽는 쪽을 먼저 옮긴 뒤 — `deprecation-and-migration`).
  - localization 표 쓰기가 머리를 옮겨 쓰게 하고 `*/localization/*` 건너뛰기를 걷는다. 시나리오 사본(`scratch/`)의 GUID 겹침 — 사본 도메인을 색인에서 빼거나 새 GUID.
- **3 단계 — 원본 저장소 떼기(S2 · S3, 원본 합계 2 GB 또는 LFS 비용이 문제일 때):** 원본(png · ogg · glb)을 LFS · 별도 저장소로 떼고 `_source` 는 상대 경로 또는 `store:<SHA-256>/<파일 이름>` 두 꼴만, 읽는 창구는 `AssetSourceLocator::resolve` 하나, 못 가져오면 임포트 오류. 서브모듈 · 서술에 절대 경로 · URL 을 쓰지 않는다. 공유 DDC 도 이 단계.

## 9. 감사 열린 항목

줄 앞은 감사 ID 와 옛 번호 `[n]`(코드 주석이 가리킨다 — 다시 매기지 않는다). 온라인 줄은 `security-and-hardening`, 외부 API 주장은 `source-driven-development` 로 대조한다.

- **P-4 [14] Android IME 조합:** 확인 `WindowTextInputTest` Android 조합 케이스, `web-check` 의 `compositionend` 화면, 실기기(4 절).
- **O-12 [30] 원장 경로 부하 측정(통합):** PostgreSQL · RESP 를 띄우고 `SW_SHOOTER3D_DB_PASSWORD` · `SW_SHOOTER3D_CACHE_PASSWORD` 를 둔 뒤 빌드 `Bin` 에서 `OnlineLoadBot --scenario=<저장소>/Source/Programs/OnlineLoadBot/Scenarios/ledger_mix.json --load=<저장소>/Source/Programs/OnlineLoadBot/Scenarios/ledger_mix.load.xml --local-server=0 --online-config=<저장소>/Source/Programs/OnlineLoadBot/LoadBot.online.json --store-config=<저장소>/Config/Server/Online/Shooter3D.external.json --report=<빌드>/Testing/Logs/OnlineLoadBot.ledgerMix.json` — `_server._conflictRetryBasisPoints` · `_busyBasisPoints` · `_droppedArrivalCount` 를 이 줄에 적는다. 종료 코드 5 면 원장 감사(재화 총량 보존)가 깨진 것이다.
- **R-4 [31] 자원마다 전용 할당:** 6 절 "자원 구간 할당 ④".
- **RN-6 [34] GPUScene 부분 수집 측정:** `31f2492dc8` 과 4 단계 커밋에서 `ctest -R Scenario.empty.load.instancechurn` — `GT.GPUScene.build.collect` · `build.batches` · `RT.GPUScene.slotInstances` p50 비교(`build.patchEnter` 0 아님). 문턱을 넘으면 배치 용량부터.
- **A-13 [36] 씬 스트리밍 측정 길:** 개발 구성 쿠킹본 실행을 ctest 하나로 세운다(쿠킹 → `-cooked-content` 기동 → 시나리오 하나, 쿠킹 순서는 배포 확인과 같게: 텍스처 임포트 · 현지화 · 셰이더 — 지금은 `engine.en.locres` · dx12 셰이더 바이너리가 팩에 없어 멈춘다). 그 위에서 `GT.Scene.service.SceneStreaming` 최대 프레임(셀 400 × 상자 2000, 짓기 예산 2000 us)을 잰다.
- **A-24 [47]** 리플렉션 묶음 TU 의 헤더 다시 파싱 — 2 절 5-4.
- **K-11 [50] 슈터 적 풀링 확인(통합):** Tracy `GameModeFlush` 를 풀 전후로 재고, shooter3d `enemydeath` · `dismemberment` · `effectpool` · `autoplay` 를 한 번에 — 다시 꺼낸 적이 래그돌 · 절단 · 무기 · 빙의 없이 서는지.

## 10. 다음 감사 범위

- **RHI · 렌더러:** 백엔드별 PSO 생성 스레드 안전 · 전송 티켓 계약, WebGPU · GL CommandContext, Vulkan 장치 고르기, EGL · GLX 문맥, `FramePassExecutorPasses.cpp`, `RenderGraph::executeParallel` 배리어 묶음, 모프 · 정점 애니메이션 풀, 실시간 프로브, forward 파이프라인 XML.
- **직렬화 · 입력:** `SerializerUtil.cpp` · `XMLSerializer.cpp` · `DataInheritance.cpp` · `ResourceComposer.cpp`, ReflectionParser `CodeGenerator.cpp`, `InputMap` 평가와 고정 틱 사이 엣지 유실, `WaylandWindow.cpp`, UI 접근성 브리지.
- **온라인:** `OnlineCore/Server/HTTP`(HPACK · HTTP/2 동적 표 · 프레임 길이 상한), Apple · Google 영수증 검증기, RESP 재연결 뒤 응답 짝, O-7 죽은 서버의 파티 티켓 풀기, 비교 후 쓰기(748bca0b0d), O-6 명단 캐시 기록.
- **게임플레이 · 에디터:** `ScheduleSystem.cpp`, BehaviorTree · Blackboard, Appearance, 장르 키트 14 개 내부, 임포터(Model · Texture · Vrm), Animation 패널, 프리팹 오버라이드 적용, 도킹 · 레이아웃.
- **실행으로만 확인되는 것:** 성능 항목 전부, 외부 API 동작에 기댄 항목(R-1 · P-5 · P-13 · O-15).

## 11. 코드 `shortcut:` 정리

정본 모양이 있으면 크기와 무관하게 고친다. 정본이 없고 "규모가 커지면" 인 것만 문턱을 적고 남긴다.

- **렌더 · 셰이더:** 변위 UV 이음매(`material_displacement.hlsli:22` — PN-AEN 식 패치 인덱스), 코트 칸 없는 외형의 젖음 · 기름막(`appearance_lit.hlsl:408`), 홍채 노멀(`appearance_lit.hlsl:38` — 오목한 홍채 노멀을 BRDF 둘째 노멀로). Vulkan 은 HDR 인 동안 다른 창을 새로 열지 않는다 — 에디터 도구 창을 HDR 에서 열어야 할 때 고친다.
- **게임플레이:** 프리팹 JSON 저장 칸 빼기(`PrefabAsset.cpp:336` — JSON 프리팹이 생길 때). Activator 컷씬 재생 중 다른 플레이어 진입은 틀지 않는다 — 동시 개인 컷씬이 필요하면 플레이어마다 디렉터.
- **에디터 · 온라인 · 도구:** 상태 문서 자동 저장(`EditorStateDocumentPanel.h:26`), 관리 도구 마지막 대상만(`AdminToolSession.h:158`), 시나리오 옛 `<Wait>`(`AutomationScenario.cpp:86` — 22 파일 123 곳, 시나리오마다 조건 대기 · 진행을 가르고 돌려 본다). 문턱만: 팩 쓰기 재읽기 해시(`PackWriter.cpp:226`), 백엔드 교체 예산(`AutomationPerBackend.cpp:575` — 상한 540 초), 데스크톱 팩 변형(`AssetAndToolTargets.cmake:57`), `test_history.py:284`, `naming_words.py:107`.

## 12. 리소스

- **Sprite Fusion 타일셋**(CC0): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer 는 Kenney 원본에서. **VFX 25 종**(MIT, credits 에 고지): 캔버스 TypeScript 를 브라우저로 재생해 시트로 굽는다. **PixelFX Piper**: 이용 조건 확인 뒤.
- **로우폴리 에셋을 게임 품질로(CC0):** 게임마다 플레이스홀더 · 원시 도형이 남은 자리를 `-gv_screenshot` 사진으로 모아 CC0 팩으로 채운다(판정: 원시 도형 · 단색 플레이스홀더 0).
- Empty 게임을 TestBed 로 전수 개명(옛 이름 린트로 막기). `new-online-kit` 로 키트 하나 지어 보기. `-cooked-content` 로 띄워 조립 경로 에셋이 빠지지 않는지. 단위 테스트 빈 곳(`Scripts/dev/test_coverage.py` "없음"), `AccountTicketConnect` 루프백 성공 · 만료 · 재시도 실패와 비밀 값이 로그에 새지 않는지.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점 — `Config/Server/Online/Providers/`). 새 의존성(인코더 라이브러리 · VMA · D3D12MA — 둘 다 MIT)의 라이선스. 공유 DDC 자리와 쓰기 권한 머신, 원본 확장자 전체를 Git LFS 로(`.gitattributes` 를 `Content/**/*_raw/**` 종류 줄로 — 용량 · 비용, S1)와 이력 다시 쓰기(`git lfs migrate`, 되돌리기 어렵다), 원본 별도 저장소, 퍼징 밤샘 · 샤드 · 여러 머신 분산용 외부 CI 러너.

## 지킬 결정

코드 규칙(`docs/agents/`)에 들지 않는 제품 범위 결정이다. 다시 열려면 그 줄의 조건이 바뀌었는지 먼저 본다.

- **리소스:** 무료 · 공개 라이선스만(CC0 우선 — Kenney · Poly Haven · ambientCG, 재배포되는 MIT 류는 고지와 함께). 금지: Quaternius QAL(저장소의 기존 묶음 여덟은 둔다, 새 묶음은 그 페이지가 CC0 일 때만) · CC-BY · NC · ND · 불명확 · 상표 · 초상 · 유명 IP. 추가 전 credits 에 쓰는 파일 · 키트 id, 원본 페이지 · 라이선스 원문 URL, SHA-256 · 받은 날짜, 변환 경로. 에이전트가 직접 그리지 않고, 유료 생성 API · 자동 다운로드 도구를 들이지 않는다. 원본은 쓰는 것만 Git LFS, 받은 팩은 `/AssetLibrary/`(무시), 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/`. 네트워크 · 온라인 · 저장소 키트는 합성 fixture(실제 공급자 로고 · 사용자 자료 금지).
- **들이지 않는 것:** 범용 스크립팅 VM(모드가 새 로직을 저작해야 한다는 요구 전까지 — 먼저 `BehaviorTreeAsset` · `ConditionExpr` · `DialogueGraphAsset` · `AnimationGraphPlayer` 에 노드 · 식을 더한다), 네이티브 모드 DLL, 원격 명령 서버, 엔진 안 MCP 서버, 런타임 아틀라스, GC, 별도 테스트 프레임워크, taskflow · magic_enum · FidelityFX SDK(모양만 옮긴다), 파이버 잡 시스템, 범용 할당기 자체 구현(자체 크기 등급 · jemalloc · tcmalloc · 전역 `operator new` 교체 · 타입별 풀 먼저), 전면 ECS · Replication Graph 통째 도입, 물리 step 겹침, 컴포넌트 청크 풀, 모션 매칭, 롤백 넷코드, 컴파일 캐시 · sccache · C++20 모듈 · 분산 빌드 · 프리셋 생성기, 흔들리는 테스트 자동 재시도, 함수 단위 핫 패치. 메모리 풀은 측정 뒤(출하 할당기는 `SW_MEMORY_BACKEND`).
- **게임 · 키트 범위:** `Source/Games/*` 시험용 게임은 Playground 하나로 합친다 — 엔진 · GameFramework 기능은 빠짐없이 Playground 장소나 미니게임에서 쓰이고 시나리오로 확인된다. 폐기할 게임에서만 쓰이던 기능은 지우기 전에 옮긴다. 키트는 모두 링크하고 게임 기능이 등록 · 서비스 바인딩만 켜고 끈다(런타임 모듈 로드 · 언로드는 기각). 게임 이름 · "대형 오픈월드" 키트를 만들지 않고, 장르 키트는 두 번째 사용처에서 같은 소유 · 저장 · 데이터 계약이 확인될 때만 나눈다(`GF_ActionMMO` 는 부하 · 운영 지표 전에 월드 규모를 약속하지 않는다, `GF_OpenWorldActionAdventure` 는 MMO 의존 없음, DLC 는 기존 세이브를 깨지 않는 선택 확장). 하우징: 구조 안정성 시뮬레이션 · 임의 메시 → 벽 생성 · NPC 가구 자동 이용 · 온라인 동시 건축은 넣지 않는다. 모드: 쿠킹된 콘텐츠 · 데이터 모드만, 런타임 토글 없음, 스토어 연동은 어댑터로, 외부 모드를 받으면 서명 · 출처 · 권한 정책을 먼저. 비행은 범위 밖. 에디터 제외: 코드 에디터 · 마켓플레이스 · 클라우드 협업 · 원격 빌드 · AI 검색 · 멀티 유저 편집.
- **에디터:** UI 스레드는 나누지 않고 MVVM 까지(다시 열 조건: 비동기 로딩 · 컴파일 · 베이크 · 에디터 틱 낮추기로도 UI 응답이 기준을 넘는 측정값). 다른 UI 프레임워크 전환은 나중 — 그때 C API 경계와 스냅샷 변경 알림을 먼저. 소스 관리 연동은 Git 만, 외부 계정 · 유료 공급자 없음.
- **길드 창고(감사 O-10):** 이전 불가 자산(유료 재화 · 귀속 아이템)은 길드 창고를 거쳐도 맡긴 당사자가 허용한 권한 안에서만 다른 계정으로 간다. 허용이 없으면 맡긴 사람만 꺼낸다.
- **플랫폼 · 빌드:** 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓰고, 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다. 해시는 xxh3 를 들이지 않는다(느리면 하드웨어 CRC32C). Server 배포 구성은 묶음마다 검증한다.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다. 줄끝은 `.gitattributes` 의 `* text=auto eol=lf`.
