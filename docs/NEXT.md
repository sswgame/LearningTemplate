# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 적는다. 끝난 일은 git 기록에, 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다. 끝나면 줄을 지운다.

절차의 정본은 스킬이다: 묶음 끝 검증 · push `preflight`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계는 `Test/README.md`, 사람이 보는 실행은 `py -3 Scripts/dev/watch_scenarios.py`.

## 0. 다음 순서

1. **통합 묶음 5 검증:** main 1b9293f5ed 에 작업자 갈래 약 30 개를 합친 `integ5`(끝 a9e93f39a2)는 병합이 끝났고 메인 작업 폴더에서 묶음 끝 검증(`preflight`) 중이다. 통과하면 main 에 합쳐 push 한다. 새 갈래를 합칠 때는 `git cherry <통합 갈래> <갈래>` 로 이미 들어간 커밋을 거른다.
   - 작업자 몫: `GameObjectManager.inl` 머리말의 "포함을 잊으면 링크에서 멈춘다" 를 맞춘다 — clang 은 `-Werror=undefined-func-template`(`sw_require_template_definitions`)로 컴파일에서 멈추고 MSVC 만 링크에서 멈춘다. 그 뒤 에셋 GUID 2 · 3 단계(아래 절 · 감사 A-20 · "다음 할 일" 16).
2. **감사 다음 할 일:** [CommercialGapAudit](CommercialGapAudit.md) `## 다음 할 일` 의 남은 줄(5 · 14 · 16 · 20 · 30 · 31 · 34 ~ 37 · 43 · 46 · 47 · 50). 2026-11-17 판정으로 열린 항목은 P1 9 · P2 7 건이다. 묶음마다 "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고치고, 끝난 항목은 감사 문서에서 지운다. 작업자에게는 감사 문서 · NEXT 를 고치지 말게 하고 통합 커밋이 지운다 — 커밋 메시지는 `감사 <ID>` 꼴(옛 백로그 번호와 겹친다).

## 1. 통합 빌드에서 함께 볼 것 (빌드 없이 넘어온 묶음)

- **통합 묶음 5(integ5):** 병합이 겹친 자리 — O-3 해시 줄(w-net `LoginHashNeverRunsOnAStoreWorker`)과 main 의 `PasswordHashBudget`, A-12 · R-6 의 팩 · 텍스처 스트리밍(w-pk34 · w-rhi 대 92caa26b99). 단위: `DeactivateWithoutEditDropsPending`(E-7), `OverrideFollowsComponentWhenAnEarlierSiblingIsRemoved` · `SavedHandleSurvivesSiblingInsertion`(A-2), `MoveCharactersMatchesMoveCharacter`(S-8), `CancelLeavesOtherRequesters`(A-19), `TimeTest.SimulationTimeRebaseKeepsQuarterSecondSteps` · `RTSWorldTest.SpeedScaleKeepsStepRatio`(K-5 · K-6), `PrefabOverridesTest.OmitDefaultValuesKeepsComponentLocalID`. 형식 판이 오른 것: `.mesh` 판 7(R-15 — 다시 쿠킹), 팩 형식 3(A-10), 서브리소스 전이의 Vulkan · DX11 · GL · WebGPU(R-10, lightculling · shadow_* · postprocess_* dx12 · vk). 렌더 배율 GPU 프레임 시계(RN-8, w-rs). 키트 시연 씬 · 시나리오 17 개(K-14, w-kitdemo)와 키트 규칙 자산(K-4 · K-7 · K-9 · K-10, w-kits), 에디터 형제 순서 · 에셋 색인(E-6 · E-8). `MemoryBenchTest` · `ContainerBenchTest` 는 꺼 둔 벤치라 숫자만(C-6 · C-9).
- **5-4 리플렉션:** 묶음 TU(`ReflectionBundle.N.cpp` — 헤더 충돌 · Windows 매크로 오염), `build_time_report.py` 생성 비중(전 30.7 %, 목표 8 % 미만), `kMethods` · `kEvents` 상수 초기화, `.gen.cpp` 합계(전 7.86 MB), `_bDerivedFlagsBuilt` 단언, 단위 `PhysicsHookRegistryTest.*` · `ReflectionTypeRegistryTest.PropertyHandleGoesStaleWhenItsDeclaringTypeChanges` · `ReflectionTypeInfoTest.MethodTableBuildsMethodList` · `ReflectionPropertyRoleTest.DerivedFlagsAreBuiltByRegistrationNotByFirstQuery` · `ReflectionTypeInfoTest.PropertyDescTableSharesDefaultMetadata`, `ReflectionPipeline.LeafNameCollision`.
- **RHI 5-2**(Vulkan 기록 경로 락 제거 · 텍스처 전이 묶음 ABI v54 · 컴퓨트 큐 타임스탬프 ABI v55): RHI 단위(`RHITrackedStateTest`), lightculling · shadow_* · postprocess_* 시나리오 dx12 · vk, `D3D12.ComputeTimestampClock` 어긋남, Vulkan 비동기 컴퓨트 `GPU.<패스>` · Tracy 겹침.
- **실장치 · 백엔드 동작:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯, DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(`RT.Frame.transientPlacements`, 검증 레이어 경고 없이 — lavapipe · Android 포함).
- **설정 표 범위 쓰기 · Ctrl+Z(52533d1b86 · a36b93f499):** `ProjectSettingsJSONDocumentTest.*`(모든 Config 파일이 고칠 수 있게 열리는지 · `HandLayoutIsKeptOnEdit` · `UndoRestoresEachEdit` 의 기대 글), 에디터에서 표 값을 고친 뒤 Ctrl+Z(형식 페이지 그리드는 아직 단계를 올리지 않는다). 다른 게임 프리셋 형식 페이지(f983064659): `ProjectSettingsEntryTest.OtherGamePresetsAreTyped`, 목록의 'Game Config - Shooter3D' 를 열어 저장.
- **플랫폼 설정 층(감사 A-23, 3fd9c63d41):** `ConfigManagerTest.PlatformOverride*`, `test_shipping_host_defaults.py`. 위험: 에디터 프로젝트 설정 저장이 합친 값(`Config/Platform/<플랫폼>/EngineConfig.json` 덮어쓰기 포함)을 바탕 `Config/Engine/EngineConfig.json` 에 쓰면 덮어쓰기 값이 바탕으로 샌다 — 저장이 바탕 파일 글만 고치는지(덮어쓰기 키를 빼는지) 테스트로 묶는다.
- **원장 공급 합 맵(dbf959bd7e):** `TestLedger.cpp` · `TestServiceMail.cpp` · `TestEconomyReceipts.cpp` 의 감사 · 분개 TTL 케이스.
- **그 밖:** 게이트 껍데기(`<빌드>/gates`) · `editorextension` 의 RHI · Shader 게이트, 5-4 생성기 출력(`_pApplyDefault` · `_pEditCondition`, 파서 사례 `DefaultOnContainer` · `EditConditionUnknownName` · `RequiredAtRootWithoutParent`), `HapticEffectTest`, `editor/undocoverage`(컴포넌트 대상 명령은 대상을 주는 줄을 더한다), G1 `slice` 12 게임, T2 격리(`AutomationUserDataRootTest` · `engine.isolation` · `Scenario.editor.statedir.*` · `net-pair mmo`), N2 `server-image`, 지형 표면 변형(`game.empty.terraindeform` 골든 첫 기록, envshowcase · waterplay · surfacelibrary 골든 유지).

## 2. 알려진 실패 · 흔들림

- **`editor.tilemapdatalayers` 단독 실패:** 이름 칸을 누른 뒤 `<EditorText>` 가 "글 칸이 키보드 초점을 잡지 않았다"(화면에는 칸이 활성). 체인에서는 앞 단계가 가린다 — `EditorSelfTestInput::isKeyboardTargetStable` 가 두 프레임 같은 대상을 못 보는 까닭을 `scenario-debug` "단독으로만 지면" 으로.
- **Linux:** editor 체인 sequencercuts · sequencerkeys 의 `SequencerUnboundCount`(Linux 에서만, `--gpu software chain editor` 로 가른다), netresourcedigest, 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`. 골든: 에디터 `.linux` 24 장(창 크기가 달라 눈으로), empty 의 boat · decals · distortion(없음) · realtimegi · surfaceshowcase 2 · tessellationstage · geometrystage(Windows 와 57 ~ 100 % 다름 — dzn 기능 차이인지 결함인지). 데스크톱 WebGPU 골든 0(`scenario_bisect.py --golden-coverage`).
- **`AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`:** 전체 실행 한 번에 "base … could not be read" 로 졌다(단독 1800 회 재현 안 됨) — 다시 지면 그 로그의 앞 케이스와 스크래치 도메인 마운트 상태를 본다.
- **Linux `editor.screenrectview` 골든 흔들림:** 2026-11-13 체인 한 번에 1.09 % 차이(허용 1 %)로 졌고, 단독 · 체인 재실행은 통과했다. 다시 지면 다른 화소가 어디인지(`scenario_bisect --golden-margins`)와 직전 단계를 적는다.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 3. 플랫폼 ([PlatformTargets](PlatformTargets.md) "남은 일")

- **Android:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 실기기 기동 · 회전(pre-rotation) · APK 팩 읽기 · 터치.
- **웹:** Web-Shipping 빌드(쿠킹이 단계 텍스처 16 계약을 처음 건다) → `py -3 -m Scripts web-check --game Empty`. 단위 `WebSocketProtocolTest` · `WebSocketAcceptTransportTest`. 입력 · 클립보드, `wss://`, 게임 세션(UDP)의 브라우저 길.

## 4. 실행 · 측정이 필요한 일

- **에디터 첫 패널 약 1 초:** `-gv_editorPanelTimes` · `-gv_traceSlowTickMs` 로 동기 준비를 찾아 `IEditorPanel::onPanelOpened` 의 비동기 잡으로(`ModulesModel::ensureLoaded` 후보).
- **검증 시간(Windows 19 분 — 2026-11-13 증분 실측, 11-12 의 53 분에서):** build 5 분(배포 프리셋 셋이 4 분 — Release · 서버 빌드와 겹친다) · cpu 2.7 분 · Release ctest 전체 11.3 분. Debug GPU 단계는 없앴다(검사는 `SW_DO_CHECKS` 로 Release 에 든다). 공통 헤더(`Macros.h`)나 CMake 폴더를 고친 묶음은 전체 재빌드라 50 분대다. 남은 고정 대기 목록은 `scenario_bisect --fixed-waits`. Linux `all` 21 분(shooter3d 체인 872 초) + sanitize 3 분.
- **N×M 측정 후보**(N 이 데이터를 따르지만 목록이 작으면 해시가 손해이거나 순서를 지켜야 한다 — 재고 나서): `DataInheritance.cpp` `findEntry`(열쇠 목록 합치기, 가장 큰 후보), `ComponentRegistry.cpp:60,75`, `PrefabPool.cpp:80,92`, `ShooterNetPawnComponent.cpp:535,571`(틱마다 적 수²), `MaterialCache.cpp:350`, `DestructionState.cpp:779,855`, `ContentBrowserCollectionStore.cpp:108`, `EditorAssetFileCommands.cpp:154,199,569`, `EditorNodeGraph.cpp:333,543,696`(프레임마다 노드 × 고른 노드), `ModResolver.cpp:161`, `ProjectSettingsDocument.cpp:45,62`, `ComponentStateStore.cpp:82`.
- **측정([DesignBacklog](DesignBacklog.md) S8 표):** Tracy `GT.Frame` 자식 합 90 % 이상, 배치 크기 12 · 24 · 48, `load/*` × 워커 1 · 2 · 4 · 8 과 구조 지표, W25 GPU 비교(c05a3ee251 대 지금 — 노이즈 안이면 되돌림), W-S4 `-ftime-trace`, W-R1 RT 프레임 힙 할당, W-R2 PSO 조회, 5-2 `RT.Bind.*`, DX12 기록 경로 락 제거 전후 `bench.json` p50, 에디터 MVVM(Hierarchy 큐브 8000 · 인스펙터 값 행), 골든 여유(`scenario_bisect --golden-margins`).
- **Shooter3D 소총 자세 클립:** 지금은 리그 IK 견착(`ubc_player_body.rig.xml` TwoHandHold). 상용(Lyra · ALS)처럼 쥔 자세 클립 + 왼손 IK 로 간다 — CC0 출처 poly.pizza "SWAT"(Quaternius, `https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`, `Idle_Gun` · `Gun_Shoot` · `Run_Shoot` 등)을 `models_raw/quaternius` 에 넣고 UBC 로 리타깃해 상체 층(Aim)으로, IK 견착은 손 자리 보정으로.

## 5. 설계 남은 일

- **렌더링:** TransientLeasePool 을 뷰 풀에, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단의 디퍼드(G버퍼 칸 부족 — 지금은 포워드 전용), VFX · 프로브 디스패치 전이를 묶음 API 로.
- **RHI 5-2(측정 뒤에만):** Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, Vulkan 서브할당(감사 R-4).
- **5-3 병렬 오브젝트**(완전 ECS 는 기각, 이득이 흔들림 안이면 되돌림): 남은 `getOwner()->getManager()`(전 45 곳, 지금 9 곳 — `git grep`) 를 `context.getManager()`(읽기) · `context.deferPostTick`(쓰기)으로 옮긴 뒤 쓰기 길을 닫고 `onSubTick` 도 같은 컨텍스트로. 같은 단계 서비스 갱신 병렬화(게임 스레드 전용 API 를 서명으로 먼저 가림). 캐릭터 컨트롤러 3D · 2D 입력을 `ComponentDataStore` 페이지로. 근접 기믹 · 감지 · 넷 관심 격자를 `SceneSpatialGrid` 로 옮길지 측정 뒤.
- **5-4 리플렉션:** W-R3 (a) 고정 배치형 코덱 본문을 필드 서술자 공용 코덱으로(생성 바이트 4 MB 미만은 그 뒤 측정), `InspectorLayoutCache` 키를 `PropertyHandle` 로(핫 리로드 때 비우는지 먼저). W-R6 밀집 타입 색인은 5-3 SoA 와 함께. 경합 같은 모양 둘: 상속 목록은 RCU 로 바꿨지만(babab8149c) `computeDerivedFlags` 가 지은 비트필드를 읽기와 겹쳐 고칠 수 있는지, 핫 리로드가 `TypeInfo` 를 `operator=` 로 제자리 교체하는 동안 다른 스레드가 읽는지 — 같은 판 교체로 막는다.
- **상용 대비:** T3 샤드 시간 균형(외부 CI 러너는 사용자 결정), U1 방아쇠 진동 · Android Vibrator(백엔드가 생길 때).

### 에셋 정체성(GUID) 2 · 3 단계 — 판단은 [DesignBacklog](DesignBacklog.md) "에셋 정체성" 절

- **통합 검증에서 볼 것(1 단계):** 단위 테스트 `AssetGUIDTest` · `AssetDatabaseTest` · `AssetPackIndexTest`, 에디터 시나리오 `editor.assetidentity`(가져오기가 새 GUID, `asset.replaceContent` 가 GUID 유지), `editor.assetmove` · `editor.flowimport` · `contentbrowser`(`.meta` 가 브라우저 · 빠른 열기 · 참조 색인에 안 보이는지 — `EditorContentScan` 이 뺀다), 모든 쿠킹(팩마다 색인이 들고 `PackCooker.PackCookNoIdentity` 가 없는지), `--check-text` · 머티리얼 그래프 생성 검사(머리 때문에 "낡았다" 가 나오지 않는지), Content XML 을 바이트로 다시 쓰고 견주는 테스트가 머리로 지지 않는지. `py -3 -m Scripts asset-ids` 를 린트 규칙 `asset-ids` 로 들어갔다(`lint --all` 이 전체를 본다).
- **2 단계 — 참조를 GUID 로:**
  - Content 전수 이전: `py -3 -m Scripts migrate-asset-refs --fix` 로 `AssetRef<T>` 칸의 경로 글을 `guid:<32자>` 로(지금 Content 의 GUID 참조는 0). 그 뒤 읽기를 GUID 만으로 좁힌다(contract — `AssetRefBase::assign` 의 옛 경로 글 읽기를 걷는다). 경로 글 칸(`_base` · `DataPath`)과 임포트 산출물 참조는 아래 산출물 줄이 생긴 뒤. 에디터 · `move-asset` 의 참조 고쳐 쓰기는 GUID 참조에서는 할 일이 없어진다.
  - 세이브(`SaveGame` 판 올림 `SW_SAVE_FORMAT_UPGRADE`)와 서버 기록(서비스 저장소 마이그레이션 `Content/common/sql/*/migrations.xml`)에 적힌 경로를 GUID 로 — 배포된 팩 색인 + 마지막 리다이렉트 표로 한 번 풀어 쓴다.
  - `Content/engine/data/enginedefaultassets.xml`(`EngineDefaultAssets`) 같은 설정의 경로 칸, 게임 설정 · 카탈로그 `_base`(`common/data/items.xml#helmet_knight`) 의 파일 부분.
  - 임포트 산출물 줄: 원본 GUID → 산출물 경로(`textures_raw/x.png` → `textures/x.dds`)를 임포트 기록(스탬프)에서 팩 색인에 더한다. 내용 주소 공유 파일(`textures_raw/_shared/`)은 저장소 경로 해시 GUID 로 `.meta` 를 함께 쓴다(판단 절).
  - 옮기기용 리다이렉트(`AssetRedirect` · 도메인 `redirects.xml` · `move-asset` 의 줄 더하기)를 걷고, 정체성 대체 · 합치기용 GUID→GUID 표만 남긴다(`deprecation-and-migration` — 표를 읽는 쪽을 먼저 GUID 로 옮긴 뒤 지운다).
  - localization 표(글 수집이 바이트로 쓰는 `strings` · `translation` · `tm`)의 쓰기가 머리를 옮겨 쓰게 하고 규칙 파일의 `*/localization/*` 건너뛰기를 걷는다. 시나리오 사본(`scratch/`)은 원본 GUID 를 그대로 들어 `scan` 에서 겹친다 — 사본 도메인을 색인에서 빼거나 사본에 새 GUID 를 준다.
- **3 단계 — 원본 저장소 떼기:** 원본(png · ogg · glb)을 LFS · 별도 저장소로 떼고 서술의 `_source` 가 그 자리를 가리킨다.

## 6. 리소스 · 그 뒤

- **Sprite Fusion 타일셋**(상세 페이지마다 CC0): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer 는 Kenney 원본에서. **VFX 25 종**(MIT, credits 에 고지): 캔버스 TypeScript 를 브라우저 MCP 로 재생해 시트로 굽는다. **PixelFX Piper**: 이용 조건 확인 뒤 시트로 내보낸다.
- Empty 게임을 TestBed 로 전수 개명(옛 이름 린트로 막기). `new-online-kit` 로 키트 하나 지어 보기. `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지. 단위 테스트 빈 곳(`Scripts/dev/test_coverage.py` "없음"), `AccountTicketConnect` 만료 · 재시도.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점 — `Config/Server/Online/Providers/`, `Scripts/net/online_local_run.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스:** 무료 · 공개 라이선스만(CC0 우선, MIT 등은 고지와 함께) — 묶음 페이지 라이선스를 확인해 credits 에 URL · 날짜. 에이전트가 직접 그리지 않는다. 유료 생성 API(Agent Sprite Forge)는 들이지 않는다. Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌어 새 묶음은 그 페이지가 CC0 일 때만. 원본은 Git LFS, 받은 팩은 `/AssetLibrary/`(무시), 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/`.
- 범용 스크립팅 언어는 들이지 않는다([판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤(출하 할당기 선택은 감사 C-6 의 `SW_MEMORY_BACKEND`), GC 는 권하지 않는다.
- 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓴다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다.
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
