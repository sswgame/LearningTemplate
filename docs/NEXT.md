# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 적는다. 끝난 일은 git 기록에, 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다. 끝나면 줄을 지운다.

절차의 정본은 스킬이다: 묶음 끝 검증 · push `preflight`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계는 `Test/README.md`, 사람이 보는 실행은 `py -3 Scripts/dev/watch_scenarios.py`.

## 0. 다음 순서

1. **이름 일괄 변경**(지난 세션 결정): 갈래 `rename-cpp`(1차 목록 360 줄 · 2차 31 줄 · lane → MainThread · Parallel 계열 · 시나리오 XML `<Remember>` → `<Store>` · 조어 어간 재검사)와 `rename-py`(파이썬 PEP 8 전환 · 조어 스크립트 이름 — ScenarioCulprit · VerifyBundle · ToolGate · FactCache · new-names). 목록 원본은 `naming-census/renames*.txt`(세션 임시 폴더 — 갈래에 옮겨 둔다). 합친 뒤 빌드 · 검증, 이어서 **저장소 전체 N×M(루프 안 선형 찾기) 조사** — 에디터 임포터 · 쿠킹은 끝났고 에디터 패널 · 런타임이 남았다.
2. **아직 합치지 않은 갈래**(합치기 전에 `git cherry main <갈래>` 로 이미 들어간 커밋을 거른다): 남은 것 — settings-json 의 표 페이지: 손으로 맞춘 한 줄 객체 파일(약 25 개)은 쓰기가 모양을 되살리지 못해 읽기 전용(`LayoutNotPreserved`)으로 열린다. VS Code settings.json 편집(jsonc-parser modify)처럼 바뀐 글 범위만 고치는 쓰기로 바꿔 모두 고칠 수 있게. 표 페이지 Ctrl+Z(감사 E-13), 활성 게임이 아닌 `Config/Game/*.json` 이 범용 페이지로 열림. `integ2`(에셋 GUID 1단계 · 5-3 틱 const · W-R3 (a) 코덱 키 검사 · A1 포즈 노드 · R1 리다이렉트 표 팩 담기 · `setTransient`, 빌드 한 번도 안 함).
   - integ2: main 을 다시 합친다(새 `extern template class … SharedAssetCache<…>;` 줄은 공유 캐시 선언 매크로로) → Debug 전체 프리셋 빌드(App 만 지으면 게임 모듈 헤더 불일치로 기동 실패) → 컴파일 오류는 `.inl` include · `extern template` 로(`-Werror=undefined-func-template` 를 끄지 않는다) → 단위 · 영향 시나리오 → preflight. 남은 작업자 몫: GameObjectManager.inl 머리말, `EditorTransaction` 을 연 채 CommandStack 을 읽는 자리. 그 뒤 에셋 GUID 2 · 3 단계(GUID 로 참조 해석 · 경로 참조 이전 — 감사 A-2 · "다음 할 일" 16).
3. **감사 다음 할 일:** [CommercialGapAudit](CommercialGapAudit.md) `## 다음 할 일` 14 번부터. 감사 남은 몫: P-10, P-11 포커스, C-2(다리 원뿔 먼저), G-3 상점 판매 개체. 묶음마다 "확인" 의 테스트를 먼저 써서 지는 것을 본 뒤 고치고, 끝난 항목은 감사 문서에서 지운다. 작업자에게는 감사 문서 · NEXT 를 고치지 말게 하고 통합이 지운다.

## 1. 통합 빌드에서 함께 볼 것 (빌드 없이 넘어온 묶음)

- **5-4 리플렉션:** 묶음 TU(`ReflectionBundle.N.cpp` — 헤더 충돌 · Windows 매크로 오염), `build_time_report.py` 생성 비중(전 30.7 %, 목표 8 % 미만), `kMethods` · `kEvents` 상수 초기화, `.gen.cpp` 합계(전 7.86 MB), `_bDerivedFlagsBuilt` 단언, 단위 `PhysicsHookRegistryTest.*` · `ReflectionTypeRegistryTest.PropertyHandleGoesStaleWhenItsDeclaringTypeChanges` · `ReflectionTypeInfoTest.MethodTableBuildsMethodList` · `ReflectionPropertyRoleTest.DerivedFlagsAreBuiltByRegistrationNotByFirstQuery` · `ReflectionTypeInfoTest.PropertyDescTableSharesDefaultMetadata`, `ReflectionPipeline.LeafNameCollision`.
- **RHI 5-2**(Vulkan 기록 경로 락 제거 · 텍스처 전이 묶음 ABI v54 · 컴퓨트 큐 타임스탬프 ABI v55): RHI 단위(`RHITrackedStateTest`), lightculling · shadow_* · postprocess_* 시나리오 dx12 · vk, `D3D12.ComputeTimestampClock` 어긋남, Vulkan 비동기 컴퓨트 `GPU.<패스>` · Tracy 겹침.
- **실장치 · 백엔드 동작:** Vulkan AMD 브레드크럼 · 128 타임스탬프 슬롯, DX12 placed 텍스처(`D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES` 가 RT+SRV 를 받는지), Vulkan placed 텍스처(`RT.Frame.transientPlacements`, 검증 레이어 경고 없이 — lavapipe · Android 포함).
- **그 밖:** 게이트 껍데기(`<빌드>/gates`) · `editorextension` 의 RHI · Shader 게이트, 5-4 생성기 출력(`_pApplyDefault` · `_pEditCondition`, 파서 사례 `DefaultOnContainer` · `EditConditionUnknownName` · `RequiredAtRootWithoutParent`), `HapticEffectTest`, `editor/undocoverage`(컴포넌트 대상 명령은 대상을 주는 줄을 더한다), G1 `slice` 12 게임, T2 격리(`AutomationUserDataRootTest` · `engine.isolation` · `Scenario.editor.statedir.*` · `net-pair mmo`), N2 `server-image`, 지형 표면 변형(`game.empty.terraindeform` 골든 첫 기록, envshowcase · waterplay · surfacelibrary 골든 유지).

## 2. 알려진 실패 · 흔들림

- **`editor.tilemapdatalayers` 단독 실패:** 이름 칸을 누른 뒤 `<EditorText>` 가 "글 칸이 키보드 초점을 잡지 않았다"(화면에는 칸이 활성). 체인에서는 앞 단계가 가린다 — `EditorSelfTestInput::isKeyboardTargetSettled` 가 두 프레임 같은 대상을 못 보는 까닭을 `scenario-debug` "단독으로만 지면" 으로.
- **Linux:** editor 체인 sequencercuts · sequencerkeys 의 `SequencerUnboundCount`(Linux 에서만, `--gpu software chain editor` 로 가른다), netresourcedigest, 에디터 체인 단계 약 18(WSLg · Xvfb 입력 초점 의심), themepark 체인 멈춤, lavapipe `blendEnable` + `R16G16_SNORM`. 골든: 에디터 `.linux` 24 장(창 크기가 달라 눈으로), empty 의 boat · decals · distortion(없음) · realtimegi · surfaceshowcase 2 · tessellationstage · geometrystage(Windows 와 57 ~ 100 % 다름 — dzn 기능 차이인지 결함인지). 데스크톱 WebGPU 골든 0(`scenario_bisect.py --golden-coverage`).
- **`AudioDataVariantTest.GraphVariantSavesOnlyItsDifference`:** 전체 실행 한 번에 "base … could not be read" 로 졌다(단독 1800 회 재현 안 됨) — 다시 지면 그 로그의 앞 케이스와 스크래치 도메인 마운트 상태를 본다.
- 흔들리는 게임 골든: 다시 지면 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다.

## 3. 플랫폼 ([PlatformTargets](PlatformTargets.md) "남은 일")

- **Android:** `CookAssets` → `py -3 -m Scripts android-apk --game <게임>` → 실기기 기동 · 회전(pre-rotation) · APK 팩 읽기 · 터치. JNI 클립보드(`AndroidClipboard.cpp` 는 `isAvailable` false).
- **웹:** Web-Shipping 빌드(쿠킹이 단계 텍스처 16 계약을 처음 건다) → `py -3 -m Scripts web-check --game Empty`. 단위 `WebSocketProtocolTest` · `WebSocketAcceptTransportTest`. 입력 · 클립보드, `wss://`, 게임 세션(UDP)의 브라우저 길.

## 4. 실행 · 측정이 필요한 일

- **에디터 첫 패널 약 1 초:** `-gv_editorPanelTimes` · `-gv_traceSlowTickMs` 로 동기 준비를 찾아 `IEditorPanel::onPanelOpened` 의 비동기 잡으로(`ModulesModel::ensureLoaded` 후보).
- **검증 시간(지금 약 40 분, 11-12 의 53 분에서):** 벽시계를 정하는 셋 — shipping-check 9 분(CPU 단계 11.5 분 중, 배포 프리셋 빌드를 증분 · 앞 단계와 겹치기), Debug 에디터 체인 385 초(조각으로 나눠 `-j` 를 채운다), Release `Scenario.shooter3d` 381 초(고정 대기를 조건 대기로). 체인 하나를 진 단계부터 다시 도는 입구(지금은 이분 탐색 도구를 손으로).
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력), waterplay(강체 속도 · 잠듦 값으로 조건 대기).
- **측정([DesignBacklog](DesignBacklog.md) S8 표):** Tracy `GT.Frame` 자식 합 90 % 이상, 배치 크기 12 · 24 · 48, `load/*` × 워커 1 · 2 · 4 · 8 과 구조 지표, W25 GPU 비교(c05a3ee251 대 지금 — 노이즈 안이면 되돌림), W-S4 `-ftime-trace`, W-R1 RT 프레임 힙 할당, W-R2 PSO 조회, 5-2 `RT.Bind.*`, DX12 기록 경로 락 제거 전후 `bench.json` p50, 에디터 MVVM(Hierarchy 큐브 8000 · 인스펙터 값 행), 골든 여유(`scenario_bisect --golden-margins`).
- **Shooter3D 소총 자세 클립:** 지금은 리그 IK 견착(`ubc_player_body.rig.xml` TwoHandHold). 상용(Lyra · ALS)처럼 쥔 자세 클립 + 왼손 IK 로 간다 — CC0 출처 poly.pizza "SWAT"(Quaternius, `https://static.poly.pizza/713f6535-f4f3-4367-a4c6-ced126ae0936.glb`, `Idle_Gun` · `Gun_Shoot` · `Run_Shoot` 등)을 `models_raw/quaternius` 에 넣고 UBC 로 리타깃해 상체 층(Aim)으로, IK 견착은 손 자리 보정으로.

## 5. 설계 남은 일

- **렌더링:** TransientLeasePool 을 뷰 풀에, 같은 자리 후처리 머티리얼이 앞 결과를 이어 읽기, 박막 · 털 · 스타킹 · 비단의 디퍼드(G버퍼 칸 부족 — 지금은 포워드 전용), 몸 머티리얼이 `_skinLayerSurfaceMap` 을 읽기, VFX · 프로브 디스패치 전이를 묶음 API 로.
- **폴더 자리**(`Config/Rules/Layers/DevFolders.json`):
  - Graphics/Material/Dev → `DevTools/Material/`: DevTools 에 `sw_addReflectionStep(DevTools MODULE …)`, 셰이더 쿠커의 `MaterialGraphGeneratedPaths::refreshAll` 호출을 엔진 창구(쿠킹 전 셰이더 소스 생성기, 정적 등록)로 뒤집고 `SW_API` → `SW_DEVTOOLS_API`. 막힘: `DataInheritance.h` 가 등록 모듈 "Engine" 을 단언 — "호스트 라이브러리" 로 넓힌다(`registerHostLibraryTypes`).
  - GF GameData/Dev → 개발 전용 공유 모듈 `GameFrameworkDev`(언리얼 `DeveloperTool` 모듈 자리): 매니페스트에 개발 전용 표시, 배포 CMake 는 짓지 않고, 개발 호스트가 GameFramework 다음에 `loadSharedModule`.
- **RHI 5-2:** DX12 기록 경로 락(`resolveBufferAddress` · `resolveOfflineView` 의 `shared_mutex`, `_resourceStateMutex`)을 병렬 기록 구간 불변식(`setParallelRecording`)과 `RHIHandleTable` 텍스처 상태로. 측정 뒤에만: Vulkan 동적 오프셋 · 푸시 디스크립터, WebGPU 바인드 그룹 캐시 은퇴, RHI 스레드, 복사 큐, SM6.6 바인드리스, RT · 메시 셰이더, Vulkan 서브할당(감사 R-4).
- **5-3 병렬 오브젝트**(완전 ECS 는 기각, 이득이 흔들림 안이면 되돌림): 틱 본문 45 곳의 `getOwner()->getManager()` 를 `context.getManager()`(읽기) · `context.deferPostTick`(쓰기)으로 옮긴 뒤 쓰기 길을 닫고 `onSubTick` 도 같은 컨텍스트로. 같은 단계 서비스 갱신 병렬화(게임 스레드 전용 API 를 서명으로 먼저 가림). 캐릭터 컨트롤러 3D · 2D 입력을 `ComponentDataStore` 페이지로. 근접 기믹 · 감지 · 넷 관심 격자를 `SceneSpatialGrid` 로 옮길지 측정 뒤.
- **5-4 리플렉션:** W-R3 (a) 고정 배치형 코덱 본문을 필드 서술자 공용 코덱으로(생성 바이트 4 MB 미만은 그 뒤 측정), (e) `CultureInfo` 부모 병합 · 자식 요소를 리플렉션 컨테이너로(폴백을 없애면 `RequiredAtRoot`), `InspectorLayoutCache` 키를 `PropertyHandle` 로(핫 리로드 때 비우는지 먼저). W-R4 `ReflectVisit` 하나(상태 해시 동등성 판정과 같은 묶음). W-R6 밀집 타입 색인은 5-3 SoA 와 함께. N1 `PROPERTY( Replicated, Quantize = … )` 복제 스키마, 스키마 해시를 와이어 버전에.
- **상용 대비:** T3 샤드 시간 균형(외부 CI 러너는 사용자 결정), U1 방아쇠 진동 · Android Vibrator(백엔드가 생길 때), A1 Mirror · Montage 를 `AnimationPoseNodeRegistry` 에, R1 배포 팩이 도메인 `redirects.xml` 을 담는지(쿠킹 설정에 이름이 없어 Shipping 에서 빠질 수 있다).

## 6. 리소스 · 그 뒤

- **Sprite Fusion 타일셋**(상세 페이지마다 CC0): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer 는 Kenney 원본에서. **VFX 25 종**(MIT, credits 에 고지): 캔버스 TypeScript 를 브라우저 MCP 로 재생해 시트로 굽는다. **PixelFX Piper**: 이용 조건 확인 뒤 시트로 내보낸다.
- Empty 게임을 TestBed 로 전수 개명(옛 이름 린트로 막기). `new-online-kit` 로 키트 하나 지어 보기. `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋(감사 K-7)이 빠지지 않는지. 단위 테스트 빈 곳(`Scripts/dev/test_coverage.py` "없음"), `AccountTicketConnect` 만료 · 재시도.

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점 — `Config/Server/Online/Providers/`, `Scripts/net/online_local_run.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스:** 무료 · 공개 라이선스만(CC0 우선, MIT 등은 고지와 함께) — 묶음 페이지 라이선스를 확인해 credits 에 URL · 날짜. 에이전트가 직접 그리지 않는다. 유료 생성 API(Agent Sprite Forge)는 들이지 않는다. Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌어 새 묶음은 그 페이지가 CC0 일 때만. 원본은 Git LFS, 받은 팩은 `/AssetLibrary/`(무시), 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/`.
- 범용 스크립팅 언어는 들이지 않는다([판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤(감사 C-6), GC 는 권하지 않는다.
- 웹은 wasm32(memory64 는 Safari 미지원). 교차 빌드는 호스트 빌드(`SW_HOST_BUILD_DIR`, 기본 `build/Ninja-Release`)의 ReflectionParser · App 을 쓴다. 교차 프리셋은 vcpkg 설치 폴더를 따로 쓴다.
- 최상위 `Mods/` 는 `Test/Mods/`. App 의 `Dev/` 는 남긴다.
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.
