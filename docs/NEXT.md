# 할 일

> 결론: 할 일 · 계획은 이 문서 하나다. 끝난 줄은 지운다(기록은 git). 규칙은 `AGENTS.md` 목차의 `docs/agents/`, 절차는 스킬(`preflight` · `linux-verify` · `scenario-debug`).

- 작업자는 이 문서를 고치지 않는다 — 통합 커밋이 줄을 지운다(커밋 메시지에 `감사 <ID>`, 주석의 `감사 <ID>` 는 `git log --grep`).
- 게임이 완성되기 전의 측정값은 고칠지를 정하는 기준이 아니다(사용자 결정). 5 절과 "측정 뒤" 줄은 상용 엔진과 구조를 견줘 정한다: 틀린 모양 · 규모에 따라 나빠지는 모양이면 재지 말고 고치고, 이미 정본이거나 상용 엔진도 하지 않으면 줄을 지운다. 측정은 조절 값(합성 벤치)과 고친 뒤 느려지지 않았는지 보는 데만 쓴다.
- 차례: 결함 · 계약 → 구조 장치(형식으로 막기) → 정본과 다른 성능 구조.

## 0. 인계(2026-11-24 — 다음 세션은 여기부터)

- 끝난 갈래는 모두 main 에 합쳤다(허브 흡수 전 단계 · 키트 셋 가르기 · 디스크 경로 형식 · VMA · D3D12MA · mimalloc 포함). 남은 갈래는 `w-buildtime`(셰이더 DDC 공유, 11-21) 하나 — main 의 `Saved/DDC` 와 견줘 남은 몫이 없으면 지운다.
- 다음 차례: ① 합친 묶음 검증 · push(2 절) → ② Linux 검증(`linux-verify`) → ③ 에셋 GUID 데이터 이전(8 절) → ④ 헤더 팬인(6 절, 혼자 마지막).
- `[코드]` 는 빌드 없이 지금 짤 수 있는 일, 나머지는 실행 · 실기기 · 계정이 있어야 한다.

## 1. 다음 순서

1. 2 절 통합 실행 → 3 절 Linux.
2. 8 절 데이터 이전.
3. 6 절 `[코드]` 줄을 갈래로 나눠 병렬로, 헤더 팬인은 맨 끝에 혼자.

## 2. 통합 빌드에서 볼 것

- **합친 묶음:** `net-pair` 전체(mmo · match), `lockstep-pair`, `online-local run`, `telemetry-local`, Server · Shipping · 웹 · Android 구성이 온라인 키트와 클라이언트 키트를 링크.
- **RHI 메모리(VMA · D3D12MA):** 네 백엔드 체인 `-rhi-validation`, 장치 소실 · 창 크기 · 되읽기, 종료 로그 `RHI.AllocationLeak` 없음.
- **셰이더:** `--cook-shaders -webgpu`(Tint 변환 — `vfx_simulate` · `mesh_morph` · `scene_voxelize`).
- **쿠킹본 실행:** `Test/Pack/RunCookedContent.cmake`(A-13 [36]), 모든 쿠킹에서 `PackCookNoIdentity` 0.
- **배포 도구:** N2 `py -3 -m Scripts server-image`.
- **임포트 산출물:** 모델 전체 다시 임포트(`.mesh` 판), `--gather-text` 결과 커밋.
- **슈터 적 풀링(K-11 [50]):** `enemydeath` · `dismemberment` · `effectpool` 로 다시 꺼낸 적이 래그돌 · 절단 · 무기 · 빙의 없이 서는지.

## 3. 알려진 실패 · 흔들림

- **Linux:** sequencercuts · sequencerkeys `SequencerUnboundCount`(`--gpu software chain editor`), netresourcedigest, 에디터 체인 약 18 단계(입력 초점), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`(재현 안 되면 지운다), `editor.screenrectview` 1.09375 %(`all` 에서만 — 사진 앞 대기를 정착 조건으로). 빈 골든은 `scenario_bisect.py --golden-coverage` 로 찾아 채운다.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 4. 플랫폼

- **웹 실행:** Web-Shipping → `py -3 -m Scripts web-check --game Playground`(WebGPU · 오류 0 · IDBFS · `[WebAudio] output started`), 입력 시나리오 `playground/web/dominput`.
- **웹 온라인:** `wss://` · WebSocket 게임 세션을 실제 브라우저 · 서버로 한 번.
- **Android 실기기:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 기동 · 회전 · APK 팩 읽기 · 터치 · 클립보드 · AAudio 다시 열기 · IME 조합(P-4 [14]).
- **Fileserve 실기기:** `adb reverse` + `-fileserve` 기동.
- **실장치:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯.

## 5. 성능 구조(상용 엔진과 견줘 아직 정하지 않은 것)

정한 줄은 지웠다. 남은 줄은 정본과 견줘 고치거나 지운다 — 재서 정하지 않는다.

- `[코드]` **DX12 PSO 캐시:** 디스크 PSO 캐시(`ID3D12PipelineLibrary` — 언리얼 D3D12 드라이버 캐시 자리)가 없다.
- `[코드]` **개수 버퍼 압축:** 컬링 뒤 살아남은 인자만 앞으로 모아 개수 버퍼 간접 그리기(언리얼 인스턴스 컬링). 투명 정렬의 다단 bitonic(큰 배치).
- `[코드]` **컴포넌트 틱 주기:** 언리얼 `TickInterval` 자리가 없다 — 멀거나 느린 컴포넌트가 손 타이머를 든다.
- `[코드]` **직렬 씬 서비스(PO-2 · PO-3):** 겹치지 않는 서비스의 갱신을 작업으로 나눈다(`ISceneParallelService`, 게임 스레드 API 는 `const GameThreadKey&`, `TestSceneStateHash` 동일).
- `[코드]` **카탈로그 쿠킹 꼴:** 카탈로그를 굽는 쿠커가 없다 — 언리얼 DataTable 처럼 이진 꼴로 구울지 정한다.
- **RHI 큰 것:** RHI 스레드, 복사 큐, SM6.6 동적 자원, Vulkan 동적 오프셋 · 푸시 디스크립터, RT · 메시 셰이더 — 기능을 쓸 게임 요구가 생길 때.

## 6. 설계 할 일

### 렌더링

- `[코드]` **렌더러 규격:** 그래프 일시 버퍼(`graph.createBuffer`), 손으로 든 뷰 히스토리(TAA · GI · 자동 노출)를 한 형식으로, 되읽기 버퍼의 펜스 재사용 풀.
- `[코드]` 렌더 스레드의 디스크 IO 를 단언으로 막는다(`FileUtil` 읽기 · 쓰기 입구).

### 코어 · 리플렉션

- `[코드]` 손 `serializeState` 남은 몫(칸 나열이 긴 32 파일)을 `StateIO::reflected` 로.
- `[코드]` `.cpp` 안 `REFLECT`(테스트 형식마다 헤더를 따로 두는 자리)를 `.gen.inl` 로.
- `[코드]` `Radians` 남은 몫: 벡터 각(`float3` 오일러) — 회전 형식과 함께 정한다.
- `[코드]` `sw::string` 자체 구현(지금 `std::basic_string` 상속 — 교체 빌드는 컴파일되게). AC-1: `set` · `unordered_set` · `string` 범위 연산의 복잡도.
- `[코드]` 멤버 · 지역 · 매개변수 이름 형태(`_camelCase` · `out…` · 단수 컨테이너)를 clang 색인으로 자동 검사(`naming_words.py` 는 정규식이다).
- `[코드]` **lockfmt 후속:** 잠금을 쥔 채 로그를 쓰는 자리를 고치고 `SW_ENFORCE_LEAF_LOCK_LOG` 를 켠다.
- `[코드]` 자기 `.cpp` 만 포함하는 60 줄 미만 헤더 72 개 접기(`header-fanin` 이 늘면 안 함).
- `[코드]` `FrameViewports.h` 를 `Engine.h` · `GameModuleManager.h` 가 앞 선언만 하게 — 서버 · 쿠커가 RHI 핸들 헤더를 읽지 않는다.
- `[코드]` 받는 쪽 없는 이벤트(방 클리어 `ActionCombatEvents.h`, 빙의 `ControlEvents.h` 등 — 버스 구독은 둘뿐이다)를 HUD · 카메라에 잇거나 지운다.

### 에디터 · 도구

- `[코드]` `new-online-kit` 의 생성 결과를 보는 Scripts 테스트.
- `[코드]` 지운 NEXT 줄 · 측정 문턱을 가리키는 코드 · 시나리오 주석 정리(`LockKindBenchTest` · `ContainerBenchTest` · `TestQueueCellAlignmentBench` · `TestDelegate` · `TestOnlineServerCallTable` · `TextureCache.cpp` · `GameInstance.cpp` · `StreamingSourcePriority.h` · `EditorAssetFileCommands.h` · `LogTypes.h` · `editor/load/*` · `abilityarena_scenesave` · `kitdemo_respawn` · `materialinstances` · `minigamememory`). 판정이 끝난 벤치는 지운다.
- 에디터 체인 단계의 뷰 누수를 막는다(다시 보일 때).
- 공유 DDC(D3) · T3 샤드 균형 — 머신 · 외부 CI 가 정해진 뒤.

### 게임플레이

- `[코드]` 두 사람이 싸우는 온라인 미니게임의 `netpair/` 리스폰 시나리오(허브 아바타는 죽지 않는다).
- MMO 키트 부하 목표 · 운영 비용(측정).

### 헤더 팬인(혼자 마지막)

- `[코드]` ③ `TypeRegistry.h` 의 `EngineMinimal.h` · `ReflectionCast.h` 를 cpp 로. 점수 합 5 % · 그 헤더 30 % 안 줄면 되돌린다.

## 7. 허브 흡수

끝났다. 남길 것: 작은 둘째 팩 NileCity. 새 게임 폴더를 두지 않는다.

## 8. 에셋 정체성(GUID)

- **2 단계 데이터 이전:** 모든 갈래를 합친 상태에서 빌드 → `--import-models` → `asset-ids --fix` → `migrate-asset-refs` → `--fix` → 한 커밋. 뒤에 GUID · 데이터베이스 · 팩 색인 단위, `editor.assetidentity` · `assetmove` · `flowimport` · `contentbrowser`, 세이브 옛 판 읽기.
- **3 단계 원본 저장소 떼기:** 코드(`store:<SHA-256>` · `AssetSourceLocator`)는 있다. 데이터는 원본 2 GB 또는 LFS 비용이 문제일 때.

## 9. 감사 열린 항목

줄 앞은 감사 ID 와 옛 번호 `[n]`(주석이 가리킨다 — 다시 매기지 않는다).

- **P-4 [14] Android IME 조합:** 실기기(4 절).
- **O-12 [30] 원장 경로 부하:** PostgreSQL · RESP 로 `OnlineLoadBot` `ledger_mix`(`--store-config=Config/Server/Online/Playground.external.json`) — 재시도 · busy · 버린 도착 수. 종료 코드 5 는 재화 보존 깨짐.
- **A-13 [36] · K-11 [50]:** 2 절.

## 10. 코드 `shortcut:` 정리

정본 모양이 있으면 크기와 무관하게 고친다. 문턱이 규모인 것(`PackWriter` · `LinkJobPool` · `AssetAndToolTargets` · `GPUSceneBuilder` 둘 · `SceneVoxelizer`)만 남긴다.

- `[코드]` 물리: 캐릭터 쌍 전부 훑기(`JoltPhysicsScene.h:221`), 바디 · 캐릭터 단계를 프레임마다 모두 보기(`ScenePhysics.cpp:202`) → 공간 색인.
- `[코드]` 렌더: 방출기 버퍼 선형 찾기(`VFXGPUSimulation.cpp:820`), HLOD 대리 합치기를 쿠킹 산출물로(`HLODMeshComponent.h:8`).
- `[코드]` 허브: 카드 탁자의 숨긴 손패를 서버가 나눈다(`HubCardTableComponent.h:7`), 허브 폰 복제를 절대 자리로(`HubPawnReplicationComponent.h:14`).
- `[코드]` 도구: `EditorVirtualList.h:6` 높이 구간, Fileserve 받기(`FileServeContentSource.cpp:169,258` · `.h:98`).

## 11. 리소스

- Sprite Fusion 타일셋(CC0, `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`), 1Bit Platformer 는 Kenney 원본, VFX 25 종(MIT, 시트로 굽기), PixelFX Piper 는 조건 확인 뒤.
- 플레이스홀더 · 원시 도형 자리를 `-gv_screenshot` 으로 모아 CC0 팩으로(판정: 0).
- 사격장 소총 자세: CC0 poly.pizza "SWAT"(`https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`)를 `models_raw/quaternius` 에, UBC 리타깃 상체 층.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인 계정 · 키, 인코더 라이선스, 공유 DDC 머신, 원본 Git LFS(S1)와 이력 다시 쓰기, 원본 별도 저장소, 외부 CI 러너.

## 지킬 결정

코드 규칙에 들지 않는 제품 범위 결정이다. 다시 열려면 조건이 바뀌었는지 먼저 본다.

- **리소스:** 무료 · 공개 라이선스만(CC0 우선, MIT 류는 고지). 금지: 새 Quaternius QAL(기존 여덟 묶음은 둔다) · CC-BY · NC · ND · 불명확 · 상표 · 초상 · 유명 IP. credits 에 파일 · 원본 · 라이선스 URL · SHA-256 · 날짜 · 변환 경로. 직접 그리기 · 유료 생성 API · 자동 다운로드 금지. 받은 팩은 `/AssetLibrary/`, 쓰는 것만 `common/*_raw/<출처>/<묶음>/`(LFS). 온라인 키트는 합성 fixture.
- **들이지 않는 것:** 범용 스크립팅 VM(먼저 BT · `ConditionExpr` · 대화 · 애니 그래프에 노드), 네이티브 모드 DLL, 원격 명령 서버, 엔진 안 MCP, 런타임 아틀라스, GC, 별도 테스트 프레임워크, taskflow · magic_enum · FidelityFX SDK, 파이버 잡, 범용 할당기 자체 구현, 전면 ECS · Replication Graph, 물리 step 겹침, 컴포넌트 청크 풀, 모션 매칭, 롤백 넷코드, sccache · C++20 모듈 · 분산 빌드, 테스트 자동 재시도, 함수 핫 패치.
- **게임 · 키트:** 시험용 게임은 Playground 하나 — 모든 기능은 Playground 장소 · 미니게임에서 쓰이고 시나리오로 확인된다. 키트는 모두 링크하고 게임 기능이 등록만 켜고 끈다(런타임 모듈 로드 기각). 게임 이름 · "대형 오픈월드" 키트 금지, 장르 키트는 두 번째 사용처에서 같은 계약이 확인될 때 나눈다. DLC 는 세이브를 깨지 않는 선택 확장. 하우징 구조 시뮬 · 동시 건축 제외. 모드는 쿠킹된 데이터만(외부 모드는 서명 · 권한 먼저). 비행 · 코드 에디터 · 마켓 · 클라우드 협업 · 멀티 유저 편집은 범위 밖.
- **에디터:** UI 스레드는 나누지 않고 MVVM 까지(비동기화로도 응답이 기준을 넘는 측정값이 나오면 다시 연다). 소스 관리는 Git 만.
- **길드 창고(O-10):** 이전 불가 자산은 맡긴 당사자가 허용한 권한 안에서만 다른 계정으로.
- **플랫폼 · 빌드:** 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`)의 ReflectionParser · App. xxh3 금지(느리면 CRC32C). Server 배포 구성은 묶음마다 검증.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다. 줄끝은 `* text=auto eol=lf`.
