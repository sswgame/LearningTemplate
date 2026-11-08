# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 A → 0 → 2 → 5. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 `bash Scripts/dev/VerifyBundle.sh` 한 번 — Windows Debug · Release · 서버 · 배포 구성이 모두 통과한 뒤에만 Linux(`Scripts/dev/LinuxScenarios.py all`)가 돈다. 끝 줄 `VERIFIED=<sha>` 면 push.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Windows 전용 서버(`Ninja-Debug-Server` — 빌드 · `Server.exe --test`)도 묶음 끝에 함께 돌린다. 키트는 짓지 않는다(`SW_BUILD_UNUSED_KITS` 는 Release 만) — 서버 키트 테스트는 `Ninja-Release` 의 `App --test` 가 돌린다.
- Linux 는 WSL 클론(`~/LT-linux`)에서 돈다 — 입구는 `py -3 Scripts/dev/LinuxScenarios.py`(명령은 파일 머리말, 절차는 `linux-verify` 스킬). 매니페스트에서 포트가 빠지면(2026-11-05 `vulkan`) 공유 설치본 보호 장치가 설치를 건너뛰어 오버레이 패치(imgui-node-editor 의 `NavigateToRect` · `GetGroupSize`)가 들어가지 않는다 — 혼자 쓰는 클론이라 한 번 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 구성한다. 시나리오는 Linux-Release 를 Xvfb(`apt-get download` + `dpkg -x`, 창 관리자 없음 · 사용자 화면을 건드리지 않음, `-screen 0 2560x1440x24`) 안에서 돌린다 — Vulkan 은 `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`(lavapipe), GL 은 `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`(Mesa 26 은 4.5 문맥에 `ARB_gl_spirv` 를 내준다), 소프트웨어라 느리니 `-gv_scenarioWallScale=4`. `Scenario.<팩>` 명령에서 `-scenario-backend-split` 을 빼고 `-scenario-backends=vk` 로 돌린다. 임포트 결과는 Windows 에서 복사한다(Debug 임포트는 너무 느리다). 골든은 `<시나리오>.<이름>.<백엔드>.linux.ppm` 으로 따로 둔다. Xvfb 에는 창 관리자가 없어 `engine/automation/windowpresent` 의 최소화 단계는 건너뛴다. 메시 원본은 Git LFS 라 클론에 `git-lfs` 와 `.git/lfs/objects` 복사가 있어야 임포트된다. Xvfb 의 `xkbcomp` 경로는 실행 파일에 박혀 있다. 고정 화면 글꼴: 카탈로그가 찾는 파일을 `~/.local/share/fonts` 에 둔다(Noto CJK, LiberationSans).
  전용 서버 Debug 도 클론에서 함께 짓는다 — 렌더러를 빼는 구성이라 클라이언트 빌드가 못 잡는 층 위반이 여기서만 드러난다: `cmake -S . -B build/Linux-Server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSW_TARGET_TYPE=Server` 에 Linux-Debug 캐시의 컴파일러 · 툴체인 · `VCPKG_INSTALLED_DIR` 을 **절대 경로로** 넘긴다(상대 경로면 컴파일러 확인에서 진다), 실행은 `Bin/Server -gv_serverExitAfterTicks=60` 과 `Bin/Server --test`.
  Wayland 창 층(`SW_ENABLE_WAYLAND`)은 기본 클론 빌드에 없다 — `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(`apt-get download` + `dpkg -x`, .pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON` 로 짓는다. 실행은 `SW_WINDOW_BACKEND=wayland`.

## 남은 일

### A. 지금 할 일(2026-11-07 인계 — 계정을 옮겨도 이 절부터)

설계 정본: [ResourceStructure](ResourceStructure.md). 사용자가 "**Linux 는 큰 작업이 모두 끝난 뒤**" 로 정했다 — 그때까지 `bash Scripts/dev/VerifyBundle.sh --no-linux`(지면 고친 뒤 `--from release|server|shipping` 으로 그 단계부터). 설계는 앞으로의 기능 확장 기준이고, 공유할 수 있는 것은 한 벌 · 변형은 차이만이다. 사용자 지시(2026-11-07): Source 전체 리팩터링(성능 · 공통 · 확장 · 층)과 미완성 기능을 남김없이 끝낸다.

**상태:** `8738843639` 까지 push. 본 트리는 R1 · R3 · R5 일부와 통합 고침(복제 서버 경합 · 서버 리플렉션 코드젠 · 골든)을 더해 서버 단계부터 다시 검증 중이다. 규칙 · 범위는 `.claude/handoff/brief_round6.md` · `brief_round7.md`(Git 밖), 조사 문서는 `.claude/handoff/surveys/`. 합칠 때마다 빌드 · 시나리오 · 골든(그림을 눈으로 본 뒤)을 맞춘다.

**작업자가 못 끝낸 것도 한다(사용자 지시 2026-11-07).** 보고의 "옮기지 못한 목록" 은 통합 때 그 자리에서 처리하거나 다음 작업자 범위에 넣는다 — 인계에 쌓아 두지 않는다.

**0. 2026-11-08 인계(여기부터).** 테스트 · 개발 장치 분리(W1 ~ W5 · R1 · R2), 로딩(지형 · 수면 한 번 짓기, 첫 그림까지 스플래시), GL 물(거스트너 0/0), launch-args 를 본 트리에 합쳤다 — 묶음 검증 중(`bash Scripts/dev/VerifyBundle.sh --show`). 통과하면 push 하고 아래 둘을 다음 묶음으로 한다.
- **동기 대기 줄이기(사용자 지시 2026-11-08 "모두 확인해서 개선").** 전수 조사(Tests 제외): `.wait()` 8 곳, `executeAndWait` 3 곳, GPU 유휴 대기 46 곳(22 파일), sleep 32 곳(27 파일), 동기 로드 입구. 판정 — 게임 · 렌더 스레드의 런타임 경로에서 기다리면 비동기 핸들 · 다음 프레임 완료 · 펜스 지연 해제로 바꾼다. 기동 · 종료 · 백엔드 교체 · 창 크기 바꾸기의 대기와 결정적 실행 정책(`DeterministicRun`)이 켠 대기는 둔다(상용 엔진도 같은 자리에서 비운다).
  - 후보(런타임): `TextureCache::resizeRenderTarget` 의 GPU 유휴 대기(펜스로 옛 대상 지연 해제), `SceneCapture` 의 `executeAndWait` 둘(비동기 되읽기), `MeshCache::acquire` · `AssetRefCache<Texture2D>::acquire` · `Mesh::acquireRigidSkinSource` · `NavGeometryCollector::collectGeometry` 의 동기 로드(요청 핸들 · 다음 프레임), `PrefabCache::loadPrefab`(중첩 프리팹 — 18 호출, 에디터 포함), `ShaderCache::getOrCompile` · `RHIShaderRequest::compile` · `LiveShaderBytecodeProvider::getReflection`(PSO 빌더의 비동기 길과 대조), `RHITransferUtil::backoff`(렌더 스레드 잠자기), `RHIBackendSwitcher::onBeginFrameOnRenderThread` 의 sleep, `AccountTicketConnector::acquire` · `BlockingHTTPClient::send` · `LocalStoreChannel::flush` 가 게임 스레드에서 불리는지, `FrameRenderer::onShaderRecompiled` · `LiveShaderSubsystem` 의 유휴 대기(개발 — 펜스 지연 해제로 같은 길), `WebGPURHIDevice::waitIdleInternal` 의 sleep 폴링.
  - 시간 의존 판정도 같이(사용자 지적): 파일 쓰기 시각으로 판정하는 6 파일(자기 쓰기는 쓰기 경로가 캐시를 바로 무효화 — Core 쓰기 관찰자, 바깥 도구 쓰기만 시간 창), 테스트의 sleep 14 파일(시계 주입 · 이벤트), 시나리오 `timeoutWallSeconds` 120 곳(프레임 · 조건 대기로).
- **상태와 로직 합치기(사용자 지시 2026-11-08).** .cpp 안 상태 타입의 public 칸을 다른 `…Internal` 정적 함수가 차례로 바꾸는 곳 — 로직을 주인 타입의 멤버로 옮기고 칸을 숨긴다(첫 예 `EditorWindowTitleState::rebuildIfChanged`). 후보: TerrainBrushTool `State`, EditorColliderHandleTool `State`, MeshModifiersVisualizer `DragState` · `Handle`, EditorSelfTest `RunState` · ContentBrowserProbe · TypingProbe, ResourceComposition `State`, AssetTypeRegistry `Table`, ReflectedValue `SourceValue`, ViewportInputOverlay `DrawFrame`, Iocp · Epoll `Connection`. 파서 · 팩토리가 한 번 채워 돌려주는 데이터 기록(`ProfileRecord` · `RunRecord` · `Header` …)은 둔다.
  - 감춘 싱글턴(사용자 지적 2026-11-08): 함수 정적 가변 변수 55 곳(Tests 제외) — 정적 등록 목록 29 곳은 둔다(다른 TU · DLL 의 등록자가 범위보다 먼저 쓴다 — 언리얼 compiled-in 등록과 같은 자리). 런타임 상태 · 정책 · 신호 26 곳은 엔진 · 에디터 서브시스템으로: `DeterministicRun` 정책 · `RenderDiagnostics` 제공자 칸(이번 묶음 R1 · R2 가 만든 것), ResourceComposition · ResourceRemap · ResourceTags · ActionRegistry `getState`, AssetTypeRegistry `getTable`, FrameFog `lastMainVolumeState`, CharacterController `getEnabled`, EditorFontSetup `state`, 각종 `slot` 창구, EditorExtension 의 제거 신호(`getRemovedSignal` → 목록의 세대 번호를 받는 쪽이 비교).
  - 정적 등록 목록은 한 틀로(사용자 지시 2026-11-08): 20 곳은 이미 `RegistrationList<T>`(Core/String/RegistrationList.h). 손으로 짠 것 — `ServiceFactoryList` 다섯(OnlineServerKit · OnlineClientRegistry · OnlineProtocolRegistry · OnlinePushRelay · ServerServiceRegistry), CacheStats `Registry`, UndoStep `getTable`, EditorExtensionRegistry(목록의 목록 + 전역 신호) — 를 `RegistrationList` 위로 옮긴다. 하나만 받는 칸(각종 `slot` · `SW_RENDER_DIAGNOSTICS_PROVIDER` — 둘째 등록 거절)은 `RegistrationSlot<T>` 틀 하나로. 등록자 구조체 · 매크로를 기능마다 손으로 쓰지 않게 공통 정의 매크로를 둔다. 바뀜 알림은 목록의 세대 번호(전역 신호 대신). 함수 정적 가변 변수는 이 두 틀만 허용하는 린트로 막는다(나머지는 서브시스템).
  - **DevTools 서비스를 에디터에 보이기:** 호스트가 손 목록(`EditorModuleManager::bindDevToolServices` — DebugHUD · ConfigReload · ReloadTracker · StartupCapture)으로 빌려 건다. 목록에서 빠지면 에디터가 조용히 못 찾는다(ReloadTracker 가 빠져 assetcascade 가 체인에서만 통과했다). DevTools 서브시스템이 스스로 "모듈에 보임" 을 등록하게(모듈 공개 서브시스템 `SW_MODULE_SUBSYSTEM` · `ModuleRegistrationHost` 의 마운트 창구와 같은 모양) 바꾸고 손 목록을 없앤다.
  - **폴더 · 파일 자리(사용자 지시 2026-11-08 — 전수 조사 끝, 이동 남음):** 기준 [FolderLayout](agents/FolderLayout.md), 목록 [FolderMoves](FolderMoves.md)(약 80 파일, include 약 3,500 줄 — Core String 재분할부터, 합칠 파일 14 묶음, 약어 표기 FourCC 등, App · Server 의 개발 기능 → DevTools, Engine/Profiling 의 보고 · 내보내기 → DevTools, Rendering · 2D → Engine/Visual). 다섯 갈래(A ~ E)를 합친 뒤 한 번에 옮기고 린트(CoreTiers 를 모든 티어로, Util 폴더 금지, 약어 검사)로 막는다.
  - Core/Container 정리(사용자 지적 2026-11-08 "기능 확장으로 될 것들을 컨테이너로 갈라 놨다"): 42 개 중 (1) 컨테이너가 아닌 것 — StringUtil · UTF8Util · formatString(String/), VarIntUtil · LittleEndianUtil(바이트 직렬화), CompareUtil · VectorUtil · UnionFindUtil · DependencyGraph(알고리즘) — 을 제자리로. (2) 같은 일을 하는 여러 타입 — 슬롯 · id 할당 IndexFreeList · IDAllocator · SlotHandle/SlotHandleTable · sparse_set(Core 만 씀) — 을 한 핸들 표 + 재사용 정책으로. (3) 기본 컨테이너에 기능 정책을 얹은 것 — KeyedPendingList(펜스 · 프레임 지연 해제 → 프레임 은퇴 서비스), SharedVectorPool(렌더 발행), ConsumerGate · EventBuffer(알림 전달), IndexedPtrList(등록 목록), RingBuffer — 은 기본 컨테이너 위 어댑터로 줄이거나 그 기능 모듈로. (4) 쓰는 곳 하나인 안정 주소 변형(PagedArray · stable_unordered_map)은 필요를 따져 합친다. 언리얼(TArray · TMap · TSet · TSparseArray · TChunkedArray · TCircularBuffer · TQueue)과 대조해 일반 컨테이너는 적게, 기능 동작은 기능 쪽에 둔다.
  - 같은 묶음: `…Internal` 구조체 · 익명 이름공간의 자유 함수 중 본문이 한 타입의 칸 · 멤버만 다루는 것은 그 타입의 static · 멤버로 옮긴다(사용자 지시 2026-11-08). 여러 주인을 차례로 부르는 순서표(예: `EditorFrameStepsInternal` 의 프레임 단계)는 남기되 단계마다 주인 호출 한두 줄만 둔다 — `runWindowTitle` 의 입력 모으기는 `EditorWindowTitleState` 로.
- Empty 체인은 navasync 까지 고쳐 돌렸다 — 그 뒤 단계(surfacewet · surfacefootprint 등)는 이번 판에서 아직 돌지 않았다. 검증이 함께 돈다.
- 작업자 보고의 남은 범위(통합 때 하거나 다음 작업자 범위): MVVM 남은 View 66 개(`Config/Dev/EditorViewIncludes.json` 목록에 더해 가며, 공용 GUI 헤더가 `Workspace/` 를 끌어오지 않게 한 뒤 `#error` 껍데기), 매 프레임 통째로 미루는 타입(Surface 셋 · Zone 둘 · JumpEscape)을 씬 서비스로, Voxel · AutoAim 조종자를 감각 + 행동 트리로, CLDR 복수형 규칙 글을 plurals.xml 과 대조, `find*Index` 반환형을 optional 로.
- 빌드 속도: 유니티 배치 크기 측정(Release 깨끗한 빌드 12 · 24 · 48)이 남았다. 재구성은 `TargetLayers` 껍데기를 바뀐 것만 쓰게 해 해결(46 초 중 31 초였다). Debug 링크는 ghash(-7%).
- 그 뒤: Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기), S8 측정 6 건, dzn SPIR-V→DXIL 충돌, 웹 · Android 실제 빌드.

**1. 통합 대기(작업자가 끝낸 커밋 — 볼 값은 각 보고).** 6 차 R2 · R3 · R5 · R6 · R7 · R8, 7 차 S1 · S2. 합치면서 함께 할 것:
- R2: 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 본다.
- R7: 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 본다.
- R8: Dawn · Tint 를 `ThirdParty/dawn`(vcpkg `dawn[vulkan,d3d12,tint-tools]`)으로, 프리셋은 `Tools/emsdk` · `Tools/Android`(설치 `py -3 Scripts/setup/PlatformSdk.py all`)를 가리키게. 웹 · Android 를 실제로 짓고 고친다. WebGPU 깊이 복사 패스 · bind group 캐시 · 타임스탬프, 브라우저 게임패드 · 포인터 잠금, Android 패드 · intent 명령줄 · pre-rotation.

**1b. 7 차 작업자 범위(2026-11-07 감사 — 본문 · 판단은 [DesignBacklog](DesignBacklog.md) 의 "통합 담당 판단" 절이 정본).**
- S3 에디터 작업 안전: 자동 저장 · 복구, 이동 트랜잭션 · 폴더 이동, 저장 검색 · 통합 검색, Map Check Fix, Assert 설정, `CallInEditor`, 소스 관리 변경 목록, 배치 고스트 · 표면.
- S4 시각 저작: 본 마스크 · 소켓 기즈모 · 타일셋 편집기 · 스프라이트 슬라이스 · 안개 도구 · G-buffer 보기 · 명세 · Ability 카탈로그 편집기.
- S5 연출: 시퀀서 바인딩 · 컷 차선 · 정지 계약 · 트랙 · 컷씬 디버거 · 관통 킬캠 · 잔상 · VFX 미리보기.
- S6 진단: 버그 리포트 입력 · 리로드 집계 · 성능 산출물 · 게임플레이 디버거들 · 공간 효과 · 이동 제약.
- S7 표면: 공통 표면 질의 · 날씨 · 침수 → `Wet` · 발자국 · 눈 흔적 · 모래 함몰.
- S8 측정 · 성능 후보 · 매직 리터럴 집계 · 장르 키트 시연(K1) · 장르 완결 루프 시나리오.
- S9 `GF_Housing` · S10 `GF_ModSupport`(돌고 있다).
- 2 차 묶음: 오픈월드 싱글 · 액션 MMO 수직 슬라이스 · 반사 RPC 메타데이터의 실제 경로.

**2. 사용자에게 받을 것.** 영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 잇는다 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

**3. Linux.** 큰 일이 끝난 뒤 `LinuxScenarios.py all`, `.linux` 골든, 에디터 체인 18 단계(아래 5 절). 엔진은 Vulkan 1.2 로 낮췄다(R4) — dzn(`~/lt-deps/dzn`, GPU Vulkan)과 Mesa d3d12(GPU GL)로도 시나리오를 돌린다. 지금 `LinuxScenarios.py` 는 lavapipe · llvmpipe(소프트웨어)로 고정이다 — 드라이버를 고르는 칸을 더한다.

**결정:** 범용 스크립팅 언어는 현재 도입하지 않는다. 핫 리로드와 기존 데이터 기반 기능이 다루지 못하는 실제 요구(예: C++ 빌드 없이 디자이너가 새 동작을 작성)가 확인될 때만 재검토한다([핫 리로드와 스크립팅 언어 판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않기로 했다. 메모리 풀은 측정 뒤에, GC 는 권하지 않는다.

**줄끝:** `.gitattributes` 가 `* text=auto eol=lf` 다.

사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset 프리셋] [--backend 백엔드] [--stages a,b] [체인…]`.

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품, HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립), MeadowVillage 밭 칸은 했다. 새 리소스는 묶음 페이지가 CC0 인 것만 들인다(`Content/common/credits.md` 머리).

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 폴더 집계(`py -3 Scripts/dev/TestCoverage.py`)는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 실제로 비었는지는 클래스 이름을 테스트에서 찾아 본다. 2026-11-05 에 채운 것: 파괴 상태 · 그래프, 외형 해석, 리그 IK, 턴 중계, RESP 저장소, 메시 쪼개기, 시퀀서 · 단축키 · 전역 변수 · 모듈 · 환경설정 · 테스트 실행기 · 문서 패널 · 테마 값 · Sync 탭 Model, UI 레이아웃 · 포커스 · 이벤트, 날씨 · 시계, 조종 시스템 · 의도 기록, 차량 좌석, 행동 트리, 기믹 회로, 어빌리티, 모듈 카탈로그, 프로파일러 · 타일맵 · 데이터 테이블 · 카탈로그 · 콘텐츠 브라우저 · UI 미리보기 Model, 인터랙터, 프리팹 덮어쓰기, 팩 리더, 크래시 보고, 립싱크, 카메라 모드. `UIPreviewModel` 은 화면이 있어야 해 시나리오 `editor/uipreview` · `uipreviewdesigner` 가 맡는다. 장르 키트 Rule 은 모두 채웠다 — 게임이 싣지 않는 키트는 Release `UnitTests`, 게임이 싣는 키트는 `UnitTests.<게임>`, 온라인 키트는 `UnitTests.OnlineKits` 가 돈다(에디터 케이스는 `Scenario.editor` 의 `editor.unitTests`). 크게 남은 것: 화면 없이 도는 패널 Model 밖의 큰 파일(`Editor/Common` · `Editor/Panels` 의 그리기 코드는 시나리오 몫이다). 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 계정 로그인 저장(가짜 암호 — 연동 · 마지막 로그인 수단 · 탈퇴 유예 포함)과 서버 목록 `ServerListModel`(실패 길 · 루프백 서버로 응답 길)도 채웠다. 2026-11-07: 경제 클라이언트 · 지갑 거울 · 스토어 결제 흐름, 푸시 제공자(FCM · APNs) · 발송기 · 제공자 설정, 티켓 주 키 · 외부 로그인 제공자 설정을 채웠다. 2026-11-06 서비스 층도 채웠다 — 순위표 · 길드 · 매칭 대기열 · 파티 로비 · 채팅 · 친구 · 경제 영수증 · 거래 · 로그인 · 우편함 · 라이브 이벤트 · 서버 디렉터리 · GM, 그리고 외형 선택 · 퀘스트 일지 · 예약 작업 · 보존 쓸기 · 월드 플래그 · 록온 · 대화 러너 · UI 화면 서비스 · UI 애니메이션. 네트워크 하네스는 있다 — Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestFixture` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다). 하네스는 있다(`AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고). 2026-11-06 에 더 채운 것: 모듈 의존 그래프 · 언로드 큐 · 섀도 복사본 거절, UI 문서, 가상 입력, 개발 명령, 스프라이트 클립, 캐릭터 데이터 읽개, UI 입력 소비 · 입력 방식, 포인터 잡기 취소, 게임 흐름의 화면 없는 규칙(팁 · UI 없는 로딩 화면 · 타이틀 없는 게임). 별도 프로그램인 `Programs/ReflectionParser`는 fixture 기반 통합 테스트 8개와 CLI 인자 실패 테스트 2개를 둔다(아래 Reflection 조사 참조). 아직 단위 테스트가 없는 곳은 섀도 복사본의 성공 길(실제 모듈 바이너리가 있어야 해 핫 리로드 시나리오가 맡는다), 화면이 뜨는 게임 흐름(시나리오 `loadingscreen` 이 맡는다). 집계의 나머지 "없음" 폴더는 위 안내대로 클래스 이름으로 하나씩 본다 |

### 5. 그 뒤

- Linux 에디터 체인 나머지(WSL lavapipe, 140 단계 중 약 18 단계): 2026-11-06 처음 끝까지 돌았다. 원인을 못 가린 것 — `audiomixer`(M 단추를 눌러도 음소거 수가 그대로), `graphaddnode` · `graphviewtools`(노드 추가 · 정렬이 안 먹는다), `viewportselect`(Ctrl 클릭 선택), `assetmanage`(휴지통 `Asset.Recycled`), `meshmodifiers`, `idle`(창 초점 없는 프레임 상태), `previewswap` · `flowboundaries`(GL 없음으로 보인다), `selftests` 의 `dpi.monitorScaleFollows`(Xvfb 에서만). 믹서를 단독으로 돌리면 팔레트 입력부터 먹지 않는다 — WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보이니, 창 관리자와 초점이 있는 화면에서 먼저 다시 돌린다. 그래픽 골든(`screen` · `viewmodes` · `emptyscene` · `loadingscreen` · `sceneviewgameview` · `screenrectview` · `flowterrain` · `terrainbrush`)은 크기부터 다르다 — 골든을 만든 Windows 모니터 DPI 에 묶인 것으로 보이며, 어느 PC 에서나 같은 크기가 되게 하거나 Linux 골든을 따로 둔다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes`) — 포맷 기능 확인 없이 블렌드를 켠다. 시나리오 게임 체인은 Debug 텍스처 임포트가 너무 느려 돌려 보지 못했다.

