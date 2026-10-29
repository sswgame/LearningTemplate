# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 다음 세션은 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: I 절의 1 · 2 · 5, J 절 대부분, J7, 시나리오 체인(3 절), **에셋 도착 결정성**(셰이더 캐시가 빈 첫 실행도 네 백엔드 통과), **태스크 대기 · 중단 신호 일원화**(`TaskHandleTracker` → `TaskManager` 단계, 흩어진 정지 플래그 → `StopSource`/`StopToken`)가 끝났다. **바로 다음**: K 절(일원화 감사 2026-10-30)의 남은 항목을 위에서부터 — 태스크(전용 스레드 · 수동 카운터를 단계 · 전용 워커 줄로) → 등록부(정적 벡터 → `RegistrationList`) → 작은 유틸 중복 · STL 대신 Core → 서브시스템 확장(E1–E5) · 재분류 → 감사 SOLID 큰 묶음. 그다음 큰 묶음 넷 — I 절 4(테스트 · 자동화 실행기를 Engine.dll 밖으로), J6(쿠커 단위), J8(리플렉션 에디터 메타 사이드 테이블), 전역 상태 → 서브시스템의 `ScopeOwned` 캐시. 원칙: 게이트는 최후의 수단, `#if` 구역 대신 빌드 단위 · 확장점 · 개발 전용 이름 규칙(`SW_DEV_ONLY_SOURCE_PATTERNS`).

## 1. 지금 상태 (2026-10-29)

- **빌드**: `Ninja-Debug`(활성 게임 Empty) · `Ninja-Release`(전체) 오류 0. **테스트**: `ctest`(단위 `App --test` + 시나리오 엔진 3 · Empty 16) 통과 — 형식과 쓰는 법은 `Test/README.md`.
- **콘텐츠**: 규칙은 **`Content` 에는 에셋(다른 데이터가 가리키는 id)만, 파생물은 `Saved`(개발) · `Cooked`(배포)**. 임포트 산출물(DDS · 메시 · 높이장)은 머티리얼 · 씬이 가리키는 에셋이라 `Content` 에 두되 크고 다시 만들 수 있어 커밋하지 않는다(.gitignore) — 새 체크아웃은 Release App 으로 `--import-textures` · `--import-models` · `--import-heightfields` 를 돈다(Debug 의 텍스처 압축은 장당 20 분). 셰이더는 굽지 않아도 된다 — 개발 실행은 쓰일 때 컴파일해 `Bin/Saved` 캐시에 남기고, 쿠킹본(`--cook-shaders -cooked-dir=…`)은 팩을 만들 때만 스테이징에 생긴다. 엔진 기본 텍스처 둘(`missing` · `loading`)만 원본으로 둔다. 임포트를 사람마다 치르는 부담이 커지면 공유 캐시(네트워크 DDC)나 Git LFS 로 푼다(아직 아님).
- **엔진 층은 빌드 형식이다(B)**: 층 표 `Scripts/generate/rules/EngineLayers.toml` → configure 가 파일마다 층을 정하고(`Scripts/generate/EngineLayers.py`), Engine 의 .cpp 는 층마다 OBJECT 부분 `EngineLayer<N>`(오브젝트는 Engine DLL 안)이 짓는다. 부분 N 의 include 자리 맨 앞에 위 층 헤더마다 `#error` 껍데기가 있어 위 층 include(전이 포함)가 컴파일에서 멈춘다. 린트 게이트 `engine-layers` 와 기준선은 지웠다. 표에 없는 파일이 생기면 configure 가 멈춘다.
- **Core 티어 표** `Scripts/generate/rules/CoreLayers.toml`, **에디터 층 표** `Scripts/generate/rules/EditorLayers.toml` — 폴더를 옮기면 세 표를 먼저 본다(`move-source` 는 표를 모른다).
- **도구**: `move-source`(이동 + 참조 · include 순서 · clang-format — CMake 소스 목록 · 문서 · 표는 고치지 않는다, 옮긴 뒤 `git grep`), `rename-symbol`(`--regex` 로 이름 일부. 새 이름이 파일 이름과 같으면 충돌로 오인한다 — 그때는 그 파일만 `sed`), 구조 감사 `PYTHONIOENCODING=utf-8 py -3 Scripts/dev/StructureAudit.py`.
- **조심할 것**: Windows git 은 대소문자만 다른 이름 바꾸기를 못 본다. 생성물도 같다 — 이름의 대소문자만 바뀐 생성 파일은 옛 철자로 남으므로(`EngineAbiStamp.gen.h`) 지우고 다시 짓는다. `py -3 -m Scripts format` 은 셰이더까지 훑는다 — 커밋에 상관없는 파일이 섞이지 않았는지 `git show --stat` 로 본다.

## 2. 정한 규칙

| 규칙 | 근거 |
|---|---|
| Asset = 콘텐츠(`Engine/Asset`, 데이터 폴더 `Content/`), Resource = GPU · RHI 자원만 | 언리얼 Content · AssetManager · FRenderResource |
| Cook = 원본을 런타임 바이너리로 굽는 일(업계 공용어 — 언리얼 · 프로스트바이트 · id), Pawn · Possess = 조종자가 쥐는 몸 · 쥐기(게임 프레임워크 공용어). 한 엔진 · 게임만의 고유 이름 · 브랜드 · 약어는 내용으로 바꾼다(`BugIt` → `BugReport`, 7 절 H) | 이름은 내용으로 |
| Pack(`.pack`) · Watch(자동화 감시 값, Probe 는 라이트 · 리플렉션 프로브만) · Scene(씬 컨테이너, World 는 월드 공간 뜻만) | 같은 단어 한 뜻 |
| Module 은 동적으로 올리는 코드 단위만. 렌더러 확장점은 `RenderExtension`, 모듈 로더는 `IModuleLoader` · `ModuleLoadStage` · `ModuleUnloadStage`. 모듈 문맥의 Image → Library | 언리얼 SceneViewExtension |
| Host = 엔진을 돌리는 프로세스(App · Server · 테스트 하네스)와 그 쪽(`HostService` — 엔진 쪽 짝은 `EngineServices`). 네트워크 끝점 `NetEndpoint*`, 에디터 패널 진입점 `IEditorPanelService`, 온라인 키트 서버 서비스 `IOnlineServerService`(짝 `IOnlineClientService`) | 같은 단어 한 뜻 |
| Runtime = 모든 호스트에서 도는 것(키트 `Runtime/`) · 실행 중. 모듈 C-ABI 계약은 `Source/ModuleAPI` | 언리얼 모듈 형식 Runtime · ServerOnly |
| Automation = 시나리오 자동 테스트(`Engine/Automation`). UI 의 시나리오 단계는 `UI/Scenario/UIScenarioSteps` — "UI Automation" 은 Windows UIA 만 | 언리얼 Automation System |
| 정적 함수만 있는 타입은 대상을 인자로 받는 상태 없는 라이브러리면 도메인 이름 그대로. 고칠 것은 서브시스템 상태 위의 정적 파사드뿐(`EditorPlaySession` 이 서브시스템) | 언리얼 FJsonSerializer · FPaths |
| 낮은 층 · 높은 층이 서로 알아야 하면 위가 아래에 건다: 팩 매니저(엔진 범위에 건 `PackManager`), 메시 무효화(엔진 범위의 `IMeshDerivedCache`), 서브시스템 순서(위 층의 의존 칸) | 언리얼 FPlatformFileManager · 모듈 등록 |
| 키트 하나의 에디터 확장은 그 키트 폴더의 `Editor/`(런타임 · 서버로 나뉜 키트는 `<기능>/Editor/`). `Plugin/Editor` 에는 GameFramework 자체와 여러 키트를 잇는 확장만 | 언리얼 플러그인의 <X>Editor 모듈 |
| 폴더 · 파일 이름은 내용으로: `Engine/FileSystem`(층 1 콘텐츠 파일 시스템) · `Engine/Foundation` · `Core/{Foundation,Hash}` · `Engine/2D`(층 6, 언리얼 Paper2D) · `Graphics/LOD` · `RHI/{Backend,Transfer,Diagnostics}` · 에셋 파일 `*Asset` · XML 정의 `*Def` | |
| 둔 것: `Editor*` · `Automation*` 접두어(타입 이름은 전역 유일), App 폴더(실행 파일 호스트 부품 — 언리얼 Launch), `*Info` 정적 사실 표(언리얼 FPixelFormatInfo), `Core/Container` 등 폴더 이름과 같은 접두어 묶음(언리얼 Containers 처럼 평평하게), `IWindowTitleTarget`(창 제목을 거는 길을 하나로 막는 이음매 — `MainWindowSubsystem` 이 비공개 상속, 테스트가 가짜를 넣는다) | |

## 3. 테스트 · 시나리오

`Test/README.md` 가 정본이다(쓰는 법 · 옛 문제와 고친 것 표). 요약: 단위 테스트는 기능 옆 `Tests/` 의 `SW_TEST_CASE` 를 `App --test` 한 번이 엔진 위에서 섞어 돌린다(로그 게이트 · 단언 0 건 실패 · 스냅숏 · 케이스별 쓰기 폴더). 시나리오는 적은 순서대로 도는 단계(`<Wait>` · `<WaitUntil>`), 백엔드별 골든 이미지(`-scenario-update-golden`), 시나리오 안 `<AllowLog>`, 실행별 산출물 폴더 · 사용자 설정이다.

- **백엔드당 프로세스 하나**: CTest 항목 `Scenario.<게임>` 하나가 시나리오 모두를 체인(`-scenario=a,b,c -scenario-keep-going`)으로 돌리고, `-scenario-backends=all` 이 백엔드마다 자식 App 하나를 띄운다(창은 2×2 바둑판 `-WinX/-WinY`). 단계마다 세션이 빈 씬으로 앞 씬을 비우고 → 전역 변수를 세션 시작 값으로 되돌리고 → `<Launch>` 값을 건 뒤 → 게임을 다시 세운다(`AutomationSession::startStage`). 렌더 파이프라인 변수는 실행 중에 다시 고른다(`FrameRendererSubsystem`). 둘째 단계부터는 게임 인스턴스를 상태 복원 없이 다시 세운다(`IGameRestarter` — App 이 `GameModuleManager` 를 호스트 범위에 건다. 게임을 내리고 → 활성 씬을 닫고 → 월드를 플레이 전으로 두고 → 새로 만든다, 언리얼 PIE 의 새 GameInstance). 그래서 단계마다 게임의 기동 흐름(첫 씬 · 벤치)이 첫 단계와 같은 길로 돌고, 시나리오 모두가 체인 하나다(벤치 · kaykit 포함 22 단계). 전체 ctest 85 초(처음 272 초), 동시 App 최대 5(부모 + 넷). 단계별 결과는 `Testing/Logs/Scenario.<게임>.<백엔드>.log` 의 `chain stage i/n`.
- **에셋 도착**: 시나리오 세션은 `gv_finishAssetWorkEachFrame=1` 도 건다 — 프레임 경계에서 렌더 스레드가 받은 패킷을 다 끝낸 뒤 에셋 로드 단계 · 스트리밍 큐 태스크 · 비동기 IO 가 함께 빌 때까지 기다리고(`AssetManager::finishPendingAssetWork`, 언리얼 FlushAsyncLoading), 렌더 스레드는 텍스처 업로드를 상한 없이 내고 끝남까지 기다리며(`TextureCache::finishPendingLoads`) 머티리얼 초기화 · 밉 스트리밍 상한을 풀고, 머티리얼 PSO 변형을 빌더 스레드 대신 그 프레임에 만든다. 그래서 요청한 에셋 · PSO 는 늘 같은 프레임에 보인다 — 예전에는 셰이더 캐시가 빈 첫 실행에서 디더 · runner 가 달랐다. 확인은 `Bin/Saved/ShaderCompileCache` · `ShaderCache` 를 지운 ctest.
- **결정적 실행**: 시나리오 세션은 `gv_animationWaitForClips=1` 을 건다 — 애니메이터가 시작 때 비동기로 읽는 클립을 `onBeginPlay` 에서 기다린다(언리얼 자동화의 FlushAsyncLoading). 끄면 첫 포즈가 나오는 프레임이 디스크 지연에 달려 래그돌 · 물리 이력이 갈렸다. 물리는 구성마다 같은 비트다: Jolt 는 `CROSS_PLATFORM_DETERMINISTIC=ON`(`ThirdParty/joltphysics/vcpkg-port`), `Engine/Physics` 의 TU 는 FMA 축약을 끈다(`sw_markDeterministicSources` — 파괴와 같은 규칙). 렌더는 대상이 아니다(디버그 셰이더 코드젠 · 렌더 수학의 FMA 로 전체 해상도 바이트는 다르고, 골든 비교 크기에서는 같다). 남은 문턱 안 차이(vfxgpulights 0.14 % · kaykitrig vk 0.28 %)는 그 렌더 차이다.
- **셰이더**: 소스 정책(개발 실행)은 쿠킹본을 읽지 않는다 — 로컬 캐시(`Bin/Saved/ShaderCache`) → 실시간 컴파일, 컴파일 결과 캐시는 `Bin/Saved/ShaderCompileCache`(언리얼 DDC). 리플렉션도 그 바이트코드에서 실행 중에 만든다. 실시간 컴파일의 기본은 쿠킹과 같은 최적화 코드젠이고 디버그 코드젠은 `-gv_shaderDebugCodegen=1`(RenderDoc, 언리얼 `r.Shaders.Optimize=0`). 쿠킹본 정책(배포 · `-cooked-content`)은 팩의 쿠킹본만 읽는다. 굽기는 `<쿠킹 폴더>/<도메인>/shaders/bin/<백엔드>` 에 쓰고(씬 · 프리팹과 같은 스테이징), 팩 굽기(`--cook-all` · `--cook-packs`)가 늘 바뀐 것만 다시 굽고 판정한 뒤 담는다. 소스 트리의 `shaders/bin` 은 낡은 잔재다(쿠킹본 접미사가 `Cooked` 역할이라 팩이 경고하고 뺀다).
- **시작 시점의 결정성**: 로딩 화면은 씬 로드 받아들임 알림(`SceneManager::onSceneLoadRequested`, 요청마다 `SceneLoadKind` 필수 — 편집기 · 진단은 `Tool`)에 게임이 붙어 띄우고(언리얼 PreLoadMap), 떠 있는 동안 게임 시간을 멈춘다(`LoadingScreen` 형식). 그래서 시나리오의 첫 프레임은 늘 월드의 첫 틱이다 — 예전에는 로드 시간 · 최소 표시 시간만큼 월드가 로딩 화면 뒤에서 돌았다(cloth 골든을 다시 떴다).
- **종료 코드**: 표 하나 `Config/Engine/ProcessExitCodes.json` → C++ `ProcessExitCode` · CMake `SW_EXIT_CODE_<이름>`.
- **남은 일**: 창 바둑판 간격은 창 틀 두께를 Win32 대략 값으로 둔다(`AutomationBackendMatrix` 의 shortcut) — 모니터가 2.6K×1.5K 보다 작으면 겹친다. 플랫폼 작업 영역 · 창 틀 크기 API 가 생기면 그 값으로. (끝난 것: 흉내 슬롯 겹침, 기동 흐름 재시작 훅, kaykitcharacters 구도 — 한 줄 폭 맞춤이 의도이고 바닥은 시나리오가 `gv_stressTestGround` 를 켜지 않아 없다, 결과 이름은 리플렉션 열거형 — 행 감시는 시작할 때 복사한 이름을 쓴다)
- **겹치는 쓰기를 막은 곳**: 로그 파일은 싱크가 안다(`ILogSink::getLogFilePath` — BugReport · 콘솔 패널이 폴더의 "가장 새 파일" 을 고르지 않는다), 같은 초의 폴더 · 사진 이름은 `FileUtil::reserveUniquePath`(만들기가 곧 자리 잡기), 개발 TLS 인증서는 한 PEM(`devserver.pem`).

## 4. 남은 일

| 일 | 내용 |
|---|---|
| 테스트 늘리기 | 옛 `Test/`(3f70e665f^ 에 약 950 파일)에서 옮길 것을 기능 옆 `Tests/` 로 — 매크로 이름이 같아 순수 함수 테스트는 include 와 위치만 바꾸면 된다. 엔진 서비스가 이미 서 있으므로 옛 하네스는 필요 없다 |
| 게임 시나리오 | Empty 외 게임의 옛 시나리오(27b17ee91^)를 옮긴다 — 변환은 `<At>` → 순차(`<Wait>`), `probe` → `watch`, 지표 문턱 → 골든, 씬 `WorldSettings` → `SceneSettings`. 활성 게임을 바꿔(`SW_ACTIVE_GAME`) 골든을 뜬다. 에디터 시나리오도 같다(에디터 실행) |
| `GameFramework/Base` · `Plugin/Editor/Base` | 이름이 모호하다(GameFramework 의 기반 라이브러리 · 그 에디터 확장). 바꾸면 모듈 이름(`GF_EditorPlugin_Base`) · 매니페스트가 따라간다 |

## 7. 다음 설계 (D · E · F)

### D. 빌드 속도 마무리

- **끝남**: Release Engine.dll 링크가 75 초(Debug 4.7 초)였다 — 원인은 개발 Release 에도 걸린 전역 ThinLTO. LTO 를 `SW_SHIPPING_BUILD` 만으로 옮겼다(`cmake/Engine/BuildLayout.cmake`, 언리얼도 Shipping 만). 잰 값: Engine.dll 링크 **75 → 1.0 초**, 엔진 .cpp 하나 고친 증분 빌드 **3.0 초**, Release 전체 310 초, ctest 20 항목(76 실행) 통과. 개발 Release 의 성능 수치는 이제 LTO 없는 값이다 — 배포 성능은 `Ninja-Shipping` 으로 잰다.
- B("Engine 링크 분할")는 층 검사를 빌드 형식으로 만든 데서 멈췄지만, 링크가 1 초라 Engine 을 DLL 여럿으로 나눌 까닭이 없다 — 하지 않는다.
- 가장 무거운 컴파일: 편집기 `MaterialPanel` · `PrefabPanel`(각 50 초대, Release) — 무엇이 무거운지(템플릿 · ImGui 헤더 · 단일 거대 함수) `-ftime-trace` 로 보고 나눈다.
- `.ninja_log` 에 지운 옛 테스트 실행 파일(TestBin/*)이 남았다 — 빌드 폴더의 옛 산출물은 다시 구성할 때 지운다.

### E. 테스트 틀 고도화 (안정성 · 속도 · 더하기 · 빼기)

**끝남(2026-10-29)**: 아래 표의 앞 세 줄 — 케이스 시한 감시 · 크래시 문맥, 옵션(`._tags` · `._disabledReason` · `._timeoutSeconds`) · `SW_TEST_CASE_F` · `SW_TEST_CASE_EACH`, `--test-shard=i/n` + CTest `SW_UNIT_TEST_SHARDS`(지금 1). 넷째 줄(옛 케이스 이식)은 기계 이식을 한 번 했다(2026-10-29): 옛 include 를 지금 트리에서 파일 이름으로 다시 찾고 옛 단언 이름을 바꾼 뒤, 지금 코드에서 컴파일(Debug · Release)되고 통과하는 파일만 남겼다 — Core 55 · Engine 41 파일, 케이스 29 → 441. 빠진 것(Core 56 · Engine 약 520 · Reflection 14 전부)은 지워진 하네스(TestBench · 자식 프로세스 · 픽스처 파일) · 바뀐 API · 다른 이미지(GameFramework) 타입에 기대 손으로 다시 써야 한다. 이식 스크립트는 세션 스크래치에 있었다 — 다시 하려면 `port_tests` 방식(파일 이름으로 include 풀기 → 티어가 가장 높은 폴더의 Tests/ → 빌드 -k 0 로 안 되는 파일 빼기 → 실행해 실패 파일 빼기)을 따른다. 쓰는 법은 `Test/README.md`.

| 문제(지금) | 고칠 모양 | 근거 |
|---|---|---|
| 케이스가 멈추면 CTest 600 초까지 아무 보고 없음 · 크래시면 보고 없음 | 케이스 시한 감시 스레드(기본 10 초, 케이스가 `.timeoutSeconds` 로 바꿈) — 넘기면 지금 케이스 이름으로 보고를 쓰고 끝낸다. 크래시 문맥에 지금 케이스(`CrashHandler::setContextValue`). 보고를 케이스마다 덧쓰기(JSON Lines)로 — 죽어도 앞 케이스 결과가 남는다 | 시나리오 HangWatch 와 같은 방식, GoogleTest 는 없고 Catch2 · Unreal 은 있음 |
| 픽스처 · 매개변수 · 태그 · 비활성이 없음(옛 틀에는 있었다) | 케이스 선언에 지정 초기화 옵션: `SW_TEST_CASE( Suite, Name, { .tags = "slow", .disabledReason = "…", .timeoutSeconds = 30 } )`. 픽스처는 `SW_TEST_FIXTURE( Suite, Type )` 로 케이스마다 새 객체. 매개변수는 `SW_TEST_CASE_EACH( Suite, Name, 배열 )` — 원소마다 케이스 이름 `Name/<i>` | GoogleTest TEST_F · TEST_P, Catch2 태그 |
| 케이스가 늘면 한 프로세스 순차 | 샤딩 `--test-shard=i/n`(이름 해시로 고르게) — CTest 가 `UnitTests.<i>` 를 코어 수만큼 등록 | GoogleTest `GTEST_SHARD_INDEX`, 언리얼 워커 |
| 옛 케이스 약 5,100(Core 879 · Engine 4,269)이 남음 | 매크로가 같은 순수 함수 케이스는 기계 이식 스크립트(`Scripts/dev`)로 위치 · include 만 바꿔 옮긴다. 옛 하네스(TestBench · 자식 프로세스 · 루프백 클러스터)에 기대는 것은 시나리오나 새 픽스처로 다시 쓴다 | |

### F. 시나리오 범용화와 게임 쪽 확장

지금 결합(조사): 결과 = 프로세스 종료 · 입력 기본 Exclusive(OS 입력 막음) · `gv_fixedFrameDelta` 강제 · 산출물 기본 · 명령줄로만 시작 · 행 감시가 `_Exit`. 단계는 어느 모듈이든 등록할 수 있다(`SW_AUTOMATION_STEP`, 키트 · 게임 등록은 아직 0). 겹치는 기능: 시퀀서(`Engine/Sequencer` — 판정 · 입력 없음), 입력 녹화 `InputReplay`(`.swreplay`, 시나리오와 같은 `IVirtualInputSource`), `ScenarioRecorder`(녹화 → 시나리오 XML).

**나눌 층**: 재생기(`ScenarioPlayer` — 단계 · 타임라인 · 가상 입력, 배포에도 있음, 게임 코드가 `play(asset, options)` 로 시작하고 끝 델리게이트를 받는다)와 그 위의 판정(`AutomationJudge` · 로그 게이트 · 골든 · 종료 코드 — 테스트 실행만). 시나리오 파일의 테스트 전용 칸(`AutomationTestOptions`)은 이미 따로 있다.

| 쓰임 | 빌려 올 곳 | 필요한 것 |
|---|---|---|
| 어트랙트 · 데모 모드(타이틀에서 대기하면 자동 플레이, 입력이 오면 멈춤) | 아케이드 · 격투 게임의 데모 루프, 언리얼 Lyra 의 어트랙트 | 입력 `Mixed` + "실제 입력이 오면 중단" 옵션, 반복 재생, 씬 불러오기 단계 |
| 내장 벤치마크(timedemo) | Source · id Tech `timedemo`, 사이버펑크 · 툼레이더 내장 벤치마크 | 고정 카메라 경로(시퀀서 트랙 재생 단계) + 프레임 통계 수집 → 결과 화면 · JSON. 행렬처럼 프리셋마다 돌리기 |
| 튜토리얼 · 온보딩(플레이어가 X 를 하면 다음) | 하스스톤 · 대부분의 튜토리얼 스크립트 | `WaitUntil` 의 게임 조건(액션 · 감시 값) + UI 강조 · 안내 단계, 입력은 막지 않음 |
| 고스트 · 리플레이(타임 어택 고스트, 킬캠) | 마리오 카트 고스트, 스타크래프트의 입력 기반 결정적 리플레이 | `InputReplay` 를 액션(의도) 단위로 녹화 · 재생, 결정성(고정 델타 · 난수 씨앗) 단계 |
| 자동 플레이 봇 · 소크 테스트(밤새 돌리기) | 씨 오브 시브즈 자동 테스트, EA · 유비 봇 플레이 테스트, 언리얼 Gauntlet | 반복 · 무작위 분기(씨앗 고정) · 메모리 · 프레임 상한 단언, 행렬처럼 여러 프로세스 |
| 컷신 · 이벤트 연출에서 게임 행동 끼우기 | 언리얼 Sequencer 이벤트 트랙 · 유니티 Timeline Signal | 시퀀서 이벤트 트랙이 시나리오(또는 단계 묶음)를 부르고, 시나리오가 시퀀스를 재생 · 기다림(`PlaySequence` · `WaitSequence`) |
| 개발 매크로(autoexec · 테스트 맵 세팅) | 소스 엔진 `.cfg` · 언리얼 `exec` | 명령 줄만 있는 시나리오를 콘솔에서 `scenario run <경로>` 로 |

**(1) 끝남(2026-10-29)**: `ScenarioPlayback::play( 경로, { _bLoop, _bStopOnHumanInput, _bFixedDelta, _onFinished } )` · 콘솔 `scenario.play <경로> [loop] [demo] [fixed]` · `scenario.stop`. 재생기는 App 의 `AutomationSession`(같은 실행기 — 판정이 끝나도 프로세스를 끝내지 않고 행 감시 · 보고가 없다), 사람 입력은 창 메시지 입구에서 센다(`InputManager::getHumanInputSerial` — 가상 입력은 세지 않는다). 확인: 기동 명령으로 재생 · 반복.
**(2) · (3) 일부 끝남**: 단계 `<LoadScene path timeoutFrames>`(씬을 열고 설 때까지 붙잡는다) · `<PlaySequence object>` · `<WaitSequence object timeoutFrames>`(시퀀서 연동). 시나리오 `game.empty.loadscene` 이 지킨다. **(4) 끝남**: `<BeginFrameStats/>` … `<EndFrameStats file="bench.json"/>` — 벽시계 프레임 시간의 수 · 평균 · 최소 · 최대 · p50 · p95 · p99 · fps 를 산출물 폴더에 쓴다(시나리오 `game.empty.benchmark`). 남은 것: 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, (5) 액션 단위 녹화 · 리플레이(고스트 · 킬캠 — `InputReplay` 를 의도 단위로).

순서 제안: (1) 재생기 · 판정 분리와 게임 API(`ScenarioPlayer`), (2) 중단 조건 · 반복 · 씬 불러오기 단계, (3) 시퀀서 연동 단계, (4) 벤치마크 모드, (5) 액션 단위 녹화 · 리플레이.

### G. 리로드를 떼었다 붙이는 단위로

조사(2026-10-29):
- 코드 핫 리로드 본체는 이미 한 폴더다 — `ModuleManager/LiveReload/`(14 파일 약 3,000 줄, Shipping 은 폴더째 뺀다) + `ModuleReloadDriver` · `ModuleCompiler` 알림.
- **흩어진 것은 "모듈 언로드 때 그 모듈 코드의 콜백을 뗀다(CodeWithin)" 계약**이다 — 27 파일 · 등록 13 곳: `RegistrationList` 그림자(같은 이름 대체), `ModuleLibraryUtil` 언로드 처리기, `Delegate` · `EventDispatcher` · `ILogSink::removeListenerCodeWithin`(순수 가상), 창 · 입력 · UI · `CommandStack` · `AssetManager` · 사용자 설정 · GameFramework(어빌리티 · 저장소 드라이버), 리플렉션 · 전역 변수 · 에셋 종류의 `unregister*ByModule`. 정적 링크인 Shipping 에서는 한 번도 불리지 않는데 하나도 빠지지 않는다. 이 장치는 핫 리로드만이 아니라 Dev 의 일반 언로드(에디터 모듈 내리기 · RHI 백엔드 교체)도 쓴다 — 경계 이름은 "동적 모듈 언로드".
- 에셋 리로드: 감시(`IFileWatcher` — Core, Shipping 에도 빌드됨) → 편집기 `AssetHotReload` · `ConfigHotReload`(EditorSubsystem — 편집기 없는 Dev 실행은 리로드가 없다) → `IAssetCache::reload`(순수 가상, 모든 캐시가 구현 · Shipping 에도 있다) → 리플렉션으로 사용자에게 알림. 리로드 전용 어댑터(`AudioReloadCache` · `LocalizationReloadCache`)가 Shipping 에도 등록된다. 게임 쪽 두 번째 감시 경로 `AppearanceDataWatcher`(Shooter3D 가 가드 없이 시작). 셰이더는 `ShaderRecompiler`(Ctrl+F8 수동, Shipping 제외)와 늘 서는 빈 `LiveShaderSubsystem`.

**진행(2026-10-29)**: 끝난 것 —
- 1: 스위치 `SW_WITH_DYNAMIC_MODULES`(`Core/Foundation/DynamicModules.h`, 배포 0)가 언로드 장치 · RHI DLL 로드 · 등록표 리로드 대체(`RegistrationReloadScope::isActive` 가 constexpr false)를 뺀다. 주소 범위 처리기(`SW_MODULE_UNLOAD_HANDLER`)는 J3 에서 통째로 지웠다 — 소유(구독 · 모듈 등록자)와 타입의 모듈 이름으로 바꿨다.
- 2 의 일부: `DevTools/AssetReload/`(엔진 밖 개발 단위, I 절) — `FileWatchDispatcher` · `ConfigReloadSubsystem`(편집기 없는 개발 App · 서버도 설정을 다시 읽는다, 편집기는 `EditorToolDefaultsReload` 처리기만). 게임 쪽 `AppearanceDataWatcher` 는 그 분배기의 클라이언트. Core 의 OS 감시기 · 리로드 어댑터 캐시(오디오 · 현지화)는 배포 빌드에서 빠진다.
- 3: `LiveShaderSubsystem` 은 배포 빌드에서 짓지도 세우지도 않는다.
- Shipping 빌드 · 쿠킹 · 실행(시나리오 sceneready PASS) 확인.
남은 것은 I 절로 옮겼다 — 에셋 핫 리로드를 엔진으로 넣지 않는다(편집 기능은 편집기, 개발 기능은 엔진 밖 `DevTools`). 위 2 의 `Engine/AssetReload` 는 `DevTools` 로 나갔다.
덧붙여 Shipping 쿠킹을 막던 둘을 고쳤다: 원본 없이 지운 손작업 콘텐츠 13 개(65225afc1)를 되살렸고, 쿠커가 `Content/scratch/` 를 훑지 않는다(`AssetPaths::collectContentFiles`).

정한 모양(언리얼 ModuleManager · HotReload · DirectoryWatcher 모듈과 같은 나눔):
1. **동적 모듈 언로드 장치 하나**(`Core/Module/ModuleUnload`): 처리기 목록 · 주소 범위 헬퍼를 모으고, 각 레지스트리는 `SW_MODULE_UNLOAD_HANDLER( ... )` 한 줄로만 참여한다. `SW_WITH_MODULE_UNLOAD`(Shipping 0) 이면 매크로가 비고 CodeWithin 함수 · 그림자 목록도 빠진다. `ILogSink` 의 순수 가상은 언로드 처리기 등록으로 바꿔 인터페이스에서 뺀다.
2. **에셋 리로드 서브시스템**(`Engine/AssetReload`, Dev 만 짓는 폴더): 파일 감시(`IFileWatcher` 를 Core 에서 옮김) · 경로 표 · 설정 · 오디오 · 현지화 어댑터 · `AppearanceDataWatcher` 흡수. 편집기 없는 Dev 게임에서도 선다. 캐시 쪽 계약은 `IReloadableAssetCache` 로 떼어 Shipping 의 `IAssetCache` 에 reload 가 없게 한다(텍스처의 리로드 세대는 비동기 로드와 섞여 있어 마지막에).
3. **`LiveShaderSubsystem`** 은 Dev 에서만 더한다(빈 껍데기를 Shipping 에 세우지 않는다).
4. 확인: Shipping 빌드에서 `CodeWithin` · `reload` 기호가 남지 않는지 `llvm-nm` 으로 본다(게이트로 둘지는 그때 정한다).

### I. 편집기 · 개발 기능을 엔진에서 떼기 (2026-10-29)

원칙: Engine · Core 에는 편집기 · 개발 도구만 쓰는 함수가 없다. `#if WITH_EDITOR` 구역도, 게이트도 두지 않는다(게이트는 최후의 수단) — **빌드 단위**로 가른다(언리얼 Runtime · Developer · Editor 모듈).

| 단위 | 담는 것 | 짓는 빌드 |
|---|---|---|
| `Core` · `Engine` | 런타임, 읽기 API, 읽기 · 쓰기 짝 직렬화, 확장점(이벤트 · 등록표 · 감쌀 수 있는 인터페이스) | 모두 |
| `DevTools`(`Source/DevTools`, 공유 라이브러리 `SW_DEVTOOLS_API`) | 개발 전용 — 설정 · 셰이더 리로드, 파일 감시 분배기, 개발 콘솔(UI 오버레이) · 디버그 HUD, 서버 포즈 고스트, 프레임 지연. 호스트가 `Engine::setStartupExtension` 으로 기동 단계에 서브시스템을 더하고 `Engine::registerFrameHooks` 로 프레임 시점에 붙는다(상속 없음) | 개발 App · Server · 에디터 · GameFramework |
| `EditorModule` · 편집기 확장 | 편집 기능 전부(저장 · 프리팹 작업 · 굽기 · 패널). 데이터 사본을 고쳐 엔진 직렬화로 쓰고 런타임이 다시 읽는다 | 개발 App |

**끝남**: 현지화 수집 · 번역 메모리, 의상 아틀라스, 리타깃 점검 · 기준 포즈 덮어쓰기, 문자열 변경 구간, 편집 조건식, 리그 되쓰기, 지형 저장, 프리팹 인스턴스 되돌리기 · 갱신, 에셋 파일 지우기를 편집기로. 리타깃 `*ForEdit` → 일반 설정자.
파일 대화상자는 편집기(`Editor/Common/Backend/FileDialog`)로 — 스레드를 들고 있다가 닫히면 합류시키고, 그 전까지 `LiveReloadManager::addReloadHold`(편집기 API `isReloadHeld`)가 리로드를 대기열에 잡아 둔다(버리지 않는다).

**`EditorFoundation` 없앰(2026-10-29)**: 되돌리기는 데이터 단계(`UndoStep` — 함수 이름 해시 + 인자 바이트, `SW_UNDO_STEP( 함수, 인자... )`, 처리기는 처음 쓰일 때 스스로 등록)이고,
리로드 · 백엔드 교체 사이 상태는 호스트의 맡김 칸(`ModuleAPI/Service/IModuleStateStore` — App 의 `ModuleStateStore`, 호스트 범위)으로 넘긴다. 편집기 쪽 주인: `EditorUndoHistory`(메인 스택 · 씬 변경 구독) ·
`ObjectCopyBuffer`(정적 전역 → 편집기 서브시스템) · `ImGuiEditor` 세션(선택 · 미저장). 패널 인스턴스에 묶인 명령(UI 미리보기 · 열린 문서)만 코드 단계(`UndoStep::makeTransient`)이고 맡길 때 버린다.
키트 · 게임 모듈도 같은 칸으로 리로드 사이 상태를 넘길 수 있다.

**남은 것**(순서대로):
1. `DevTools` 단위 — 리로드 · 감시 분배기 · 콘솔 · HUD · 서버 포즈 고스트 · 프레임 지연은 옮겼다(엔진은 기동 확장 델리게이트와 프레임 훅만 안다). 기능별 개발 명령은 기능 옆 `*DevCommands.cpp` 이고 배포 구성은 그 이름을 짓지 않는다(`SW_DEV_ONLY_SOURCE_PATTERNS`, `Tests/` 와 같은 규칙). 회선 흉내는 세션의 샤드 전송 데코레이터(`GameNetSession::setTransportDecorator`)에 `NetLineEmulationSubsystem` 이 걸고(J9), 장치 손실 주입은 렌더 스레드의 프레임 시작 훅(`BeginFrameHookDelegate`)에 App 의 백엔드 교체기가 건다. OS 파일 감시기(`DevTools/FileWatch`) · RenderDoc 연결(기동 확장에서 RHI 앞에 올린다) · 리로드 어댑터 캐시(오디오 · 현지화)도 옮겼다. DevTools 헤더는 DevTools 를 링크한 타깃만 include 할 수 있다(`DevToolsAPI.h` 가 컴파일에서 멈춘다). 남은 것: Core/Console 등록표의 파일 감싸기 — 자동화 실행기(4)가 시나리오 `<Command>` 로 개발 명령을 부르므로 4 와 같이 배포 구성에서 폴더째 뺀다.
2. ~~에셋 리로드~~ — `IAssetCache::reload` 가상 칸을 지웠다. 캐시는 등록할 때 리로드 함수를 같이 넘기고(`SW_ASSET_CACHE_RELOAD_FUNCTION`, 배포는 `SW_DEV_ONLY_FUNCTION` 이 nullptr), 템플릿이 아닌 캐시 · 오디오 · 현지화 · 설정의 리로드 본문은 개발 전용 `*Reload.cpp`(배포 구성은 짓지 않는다)다. 데이터 폴더 감시는 엔진 확장점 `IContentFolderWatch`(개발 도구가 건다). 남은 런타임 흔적: 텍스처 리로드 세대 칸(배포에서는 늘 0).
3. 편집기 `AssetHotReload` 는 편집기에 두고 `DevTools` 의 감시 위에 선다.
4. 테스트 · 자동화도 같은 기준("배포한 게임이 쓰는가") — 테스트 틀(`Core/Testing`)과 기능 옆 `Tests/` 는 소스 자리는 두고 Engine.dll 이 아니라 테스트 단위(Core · Engine 테스트 DLL, `App --test` 가 올린다)가 짓는다.
   자동화의 판정 · 골든 · 보고 · 백엔드 행렬 · 종료 코드(`Engine/Automation/Runner`)는 엔진 밖 개발 단위로. 시나리오 재생기 · 단계 · 프레임 통계(내장 벤치마크 · 어트랙트 모드)는 배포 게임이 쓰므로 Engine 에 남는다.
5. ~~굽기~~ — `LightProbeBaker` · `ReflectionProbeBaker` 는 편집기(`Editor/Common/Commands/Scene`)로 옮겼다(테스트도 편집기 쪽 `Tests/`).

### J. 구조로 풀 수 있는데 우회로 막는 자리 (감사 2026-10-29)

원칙(I 와 같다): 게이트 · `#if` · 주소 훑기 · 주석 규칙 대신 빌드 단위 · 확장점 · 데이터 · 형식. 값이 큰 순서:

| # | 지금 | 고칠 모양 | 크기 |
|---|---|---|---|
| 1 | 안 쓰는 테스트 훅 `FrameRenderer::setDebugOverrides`(`FrameDebugOverrides`) · `Memory::injectAllocationFailures` | 지운다 | 5 파일 |
| 2 | `EventDispatcher` 채널 표가 처음 만든 모듈의 템플릿 썽크를 가리켜 그 DLL 을 매핑한 채 둔다(`outKeepLibraryMapped` · `DeferredLibraryUnloadQueue::_bKeepMapped`) | 채널 항목을 타입 지운 `MulticastDelegate<void(const IEvent&)>` + Core 의 비템플릿 broadcast 로 — keep-mapped 경로 전체가 빠진다 | 4–5 |
| 3 | 모듈 언로드 때 레지스트리마다 주소 범위를 훑어 뗀다(`SW_MODULE_UNLOAD_HANDLER` · `isCodeWithin` · `removeCodeWithin` 9 종 — AssetManager 는 vtable 주소까지) | 모듈의 모든 등록을 모듈이 소유한 `Subscription` 묶음(ModuleScope)에 담고 언로드 = 묶음 파괴. `Subscription` 을 안 돌려주는 등록(`UIBindingConverterRegistry` · `CompressionCodecRegistry` · InputManager 슬롯 · IWindow 처리기)만 고친다 | ~20 |
| 4 | 등록 해제를 주석 규칙에 맡김(`CompressionCodecRegistry::unregisterCodec`, `CrashContext::setCrashExtraTextWriter`, `ServerServiceManager::stopAll`, `AssetManager` 종료 경고) | 모두 `Subscription` 을 돌려주고 3 의 ModuleScope 가 소유 | 6–8 |
| 5 | 개발 명령 · 디버그 HUD · 개발 콘솔이 Engine/Core 안에서 `#if SW_DEV_COMMANDS_ENABLED` 로 파일째 감싸짐(~28 파일) · 엔진 루프가 개발 도구를 이름으로 부른다(`Engine.cpp` `#if` 13 곳) | `DevTools` 단위(I 절)로 옮기고 `IEngineFrameObserver` · 서브시스템 등록으로 붙는다 | ~40 |
| 6 | 쿠커(PackCooker · SceneCooker · ShaderCooker …)가 Engine.dll 에 있고 런타임 읽기마다 `CookJob::admitRead`, `HeadlessSubsystem` 의 하드코딩 분기 | 쿠킹 단위로 분리(실시간 셰이더 컴파일 — DXC 호출 · 컴파일 캐시 · 디버그 코드젠 · 핫 리로드 재컴파일 — 도 개발 단위다: 배포는 쿠킹본만 읽는다. `ShaderCache` 에는 "없으면 만들어 줄 쪽" 확장점만 두고 개발 단위가 건다, 언리얼의 Developer 모듈 ShaderCompiler), 헤드리스 작업은 이름 등록 commandlet, `AssetIO` 에는 읽기 관찰자 자리 하나 | ~25 |
| 7 ✓ | "쿠킹본만 읽기" 를 `#if SW_SHIPPING` 으로 파일마다(SceneDocument · PrefabAsset · ShaderCache · ConfigManager …) | 호스트 조립 때 고르는 읽기 정책 하나(`ContentSource`), 소스 XML 리더는 쿠킹 단위로 | 8–10 |
| 8 | 리플렉션 메타데이터의 에디터 칸(category · tooltip · `_bCallInEditor` …)이 `#if !SW_SHIPPING` | 생성기가 에디터 메타를 사이드 테이블(`*.editor.gen.cpp`)로 내고 편집기가 짓는다 | 생성기 + 6 |
| 9 | 회선 에뮬레이션 데코레이터를 `GameNetSession` 이 `#if` 로 붙임 | 세션에 transport 데코레이터 등록 자리, `DevTools` 가 등록 | 3 |
| 10 | `RHIBackendRegistry` 가 백엔드 헤더 넷을 include 하고 DLL 로드까지 | 백엔드가 스스로 등록, DLL 로드는 ModuleManager | 4 |
| 11 | 모듈 언로드 정리 함수가 런타임 타입에 `#if`(`GameObjectManager::destroyComponentsOfModule` 등) | 범용 API + 타입의 모듈 태그(데이터), 절차는 ModuleManager | 6 |
| 12 | `GameObject::_bHiddenInEditor` 를 렌더러가 읽음 | 편집기 숨김 집합을 뷰의 제외 필터로 | 4 + 편집기 |
| 13 | `IEngineFrameObserver` 에 자동화 전용 칸(실행 상태 · 사진 경로) | 자동화 관찰자가 자기 서비스로 | 4 |
| 14 | UI 데모 · 스트레스 화면이 Engine.dll | Games/Empty 또는 DevTools | 4 |
| 15 | 그 밖: 의사 현지화 `#if`(쿠킹이 뺀다), AbilityComponent 의 vtable 훑기(타입 id 로 다시 만들기), 린트 include 규칙 1(pch 강제 포함과 중복), 롤백 결정성 · 리플렉션 후보 CMake 게이트(타깃 종류 · 생성 대상으로), 순서 규칙 주석(API 모양으로) | | 각 1–3 |

**끝남(2026-10-29)**: 1 · 2 · 3 · 4 · 5(남은 것은 I 절 1) · 9 · 10(배포는 정적 자기 등록. 개발의 DLL 로드는 동적 모듈 스위치 `SW_WITH_DYNAMIC_MODULES` 한 곳 안에 두었다 — 언리얼 `FModuleManager::LoadModule` 도 Core 다) · 11 · 12 · 13 · 14 · 15 의 어빌리티 vtable 훑기 · 7.
7: 콘텐츠 정책은 팩 매니저의 값 하나 `ContentSource`(`Cooked` 배포 · `-cooked-content` / `Source` 개발 실행 · 쿠킹)이고 읽는 곳은 `AssetIO::getContentSource` · `readsSource` 를 묻는다(씬 · 프리팹 · 셰이더 · 셰이더 리플렉션 · `.meta`).
"쿠킹본이 있으면 쿠킹본, 없으면 소스" 는 두지 않는다 — 개발 씬이 소스와 어긋난 `.bin` 을 모르고 읽던 폴백이 그것이었다. 셰이더 쿠킹본은 소스 내용 해시로 확인하는 캐시라 소스 실행도 쓴다(언리얼 DDC).
남은 것: 설정의 베이크 JSON(`ShippingGameDefaults.h` — 배포 구성만 생성하는 산출물이라 정책이 아니라 빌드 산출물), 개발 전용 점검(`EngineDefaultAssets` · `PackEngineSettings`), 소스 리더를 쿠킹 단위로(J6).
12 는 층 문제(엔진에 편집기 이름 · 뜻)만 풀었다(`setRenderHidden`) — 편집기 뷰에서만 숨기기는 층이 아니라 기능이고, 렌더러에 뷰 마스크가 없어(GPU 인스턴스 예비 칸 · 컬링 셰이더 · 그림자 · CPU 경로)
필요해질 때 기능으로 더한다(편집기 플레이는 씬 사본이 아니라 제자리라 플레이 중에도 숨긴 채다).
13: 패킷 사진은 우편함의 "다음 패킷에 찍기" 칸(`ScreenshotRequestQueue::putForNextPacket`), 창 제목은 자동화 세션이 든다 — 관찰자 인터페이스에 자동화 칸이 없다.
14: UI 데모 · 스트레스 화면은 `Games/Empty`. 9 는 개발 명령(`net.emulation`)과 묶여 `DevTools` 단위와 함께 한다(I 절).
3 은 이렇게 끝냈다 —
등록은 구독(`Subscription`)이 해제를 보장하고, 엔진 밖 레지스트리(편집기 확장 · 에셋 동작 · 메시 모디파이어)의 정적 등록자는 `ModuleObjectRegistrar`(타입 등록자와 같은 머리)로
모듈을 올릴 때 붙고 내리기 전에 떨어진다(`engine::unregisterModuleTypes`) — 새 이미지를 먼저 올려도 옛 이미지의 같은 id 와 부딪히지 않고, 떨어질 때 목록이 알려 편집기가 그 줄의 인스턴스를 거둔다.
모듈 타입의 인스턴스를 든 쪽은 **타입의 모듈 이름**(데이터)으로 고른다: UI 화면 · 위젯(`getTypeInfo`) — `TypeRegistry::subscribeModuleUnloading`, 컴포넌트가 든 남의 모듈 객체(어빌리티 · 세트 · 실행 계산) — `Component::onModuleUnloading`,
편집기 기본값 — 같은 알림. `SW_MODULE_UNLOAD_HANDLER` · `releaseModuleCode` · `isAddressWithin` · `findVtableAddress` · `Delegate::isCodeWithin` · 라이브러리 범위 조회는 지웠다.
엔진 밖 모듈의 화면 · 어빌리티 · 세트 · 실행 계산 클래스는 `REFLECT( Abstract )` 와 `getTypeInfo` 재정의가 필요하다(카탈로그 등록 템플릿이 `HasOwnReflectBody` 로 멈춘다 — 재정의는 위젯과 같은 규칙).

**전역 상태 → 서브시스템**(조사 2026-10-29): 정적 등록표(모듈이 정적 초기화 때 채우는 표 — `AssetTypeRegistry` · `AutomationStepRegistry` · `UndoStepRegistry` …)는 범위보다 먼저 채워지므로 정적이 맞다. 바꿀 것은 **프로세스 상태를 정적 칸에 둔 곳**이다 —
`ScopeOwned` 전역 캐시 ~25 곳(MeshCache · SceneTransformStorage · ShaderReflectionLibrary · PhysicsMeshShapeCache · StringTable · ContentHash · AssetPaths 해석 캐시 · SystemFontLocator · GameDataCache …), `AutomationSceneSteps` 의 대기 목록(실행기 상태로, I 절 4 와 함께).
`GameTimeScale` 의 정지 요청 · 프레임 시간은 서비스 서브시스템이 됐다. `MeshCache` 무효화 훅 목록은 엔진 범위의 `IMeshDerivedCache`(물리 셰이프 표 서브시스템 `PhysicsMeshShapeTable`)로, `TickBatchRegistry` 는 서비스 범위 서브시스템 + E1 사슬의 등록자(`ModuleObjectRegistrar`)로 옮겼다 — 정적 초기화 때는 링크 때 목록에만 달고 모듈을 올리는 길이 붙인다.
편집기의 포커스 스택 · 선택은 에디터 서브시스템 `EditorFocus`, 뷰포트 입력 오버레이 설정은 `ViewportInputOverlaySettings` 로 옮겼다. `ScenarioPlayback::s_pPlayer` 는 호스트 범위의 `IScenarioPlayer` 로 바꿨다. **두는 것**(이유): `ProfilerBackend::s_pActive`(계측 지점마다 읽는 뜨거운 경로 · 엔진 뒤에 닫히는 구간도 부른다 — 수명은 `ProfilerBackendSubsystem`), Jolt `s_pFactory` · RenderDoc API(바깥 라이브러리가 프로세스 전역을 요구한다). 되돌리기 스택의 전역 둘(`s_pExecuting` · 살아 있는 스택 목록)은 지웠다 — 처리기는 실행 문맥(`UndoStepContext`, 필요한 처리기만 첫 인자로 받는다)을, 스택은 코드 단계를 받은 주인의 사라짐을 구독한다. 에디터 쪽 `Tests/` 는 에디터 자체 테스트 `editor.unitTests` 가 돌린다(`App --test` 는 에디터 모듈이 오르기 전이다). 서브시스템이면 소유자 · 수명 · 의존 순서가 드러나고 테스트가 범위마다 새로 세운다.

### K. 일원화 감사 (2026-10-30)

같은 일을 하는 장치가 둘 이상이면 하나로(밑바탕을 넓히거나 다른 단위로 재분류 — 우회 장치를 새로 만들지 않는다). 확인: Debug · Release 빌드와 ctest(단위 + 시나리오 22 단계 × 4 백엔드) 통과, Linux 빌드는 못 돌렸다(X11 · Wayland · ALSA · epoll 쪽 변경이 있다 — Linux 에서 한 번 짓는다).

**끝난 것**
- 태스크: `TaskHandleTracker` 삭제 → 주인별 `TaskStageHandle`(`addToStage` · 끝없는 `waitStage(stage, owner)` 와 10 초 진단), `waitAll(owner)` 도 기한 없이. Jolt · 내비메시 · GPU 업로드(`runParallel`) · AsyncFileIO 종료가 스테이지로, `MaterialPSOBuilder` 가 `DedicatedWorker` 로. 정지 신호는 `StopSource`/`StopToken` 하나. 남긴 것(이유는 코드 주석): RenderGraph 레벨 스테이지(High 줄), SQL 연결 풀(연결마다 스레드), TextureCache 로드 대기, AssetLoadBlock 단계 워드, TaskManager 워커 `_bStop`, `AsyncFileIOQueue::_bStopping`.
- 결정성: 에셋 도착을 프레임에 묶는다(`gv_finishAssetWorkEachFrame` — 로드 · 스트리밍 · IO · 업로드 · PSO 변형을 프레임 경계에서 끝낸다).
- 등록부: 정적 벡터 · 손짠 목록 → `RegistrationList`(+ `addSubscribed` — 해제 때 주인 정리), 콜백 목록 → `MulticastDelegate`(+ `broadcastUntil`), 스레드 안전 → `LockedMulticastDelegate`(해제 대기도 기한 없이), `NameRegistry::addOwned`, `AtomicSlot`, `IDAllocator`, `ItemSubscriptionList<T>`. 카메라 · 씬 오디오 · 에셋 매니저 · 넷 라우터 · 내비게이션 · 프레임 훅 · 입력 맵 콜백 · 리로드 콜백.
- 서브시스템 확장: E1 사전 등록 사슬(`SW_MODULE_SUBSYSTEM`, 모듈 객체 등록자), E2 공개 칸(`SubsystemScope::publish` — `IRenderTargetDeclarationSink` · `IMeshDerivedCache` 처럼 위 층이 구현을 타입으로 낸다), E3 서브시스템이 가진 상태, E4 프로세스 범위, E5 모듈 자식 범위. 옮긴 것: NetSecurity, PackManager 바인딩, 키트 DLL 정적(넷 시뮬 세션 · 계정 티켓 · 저장소 드라이버), ScheduleSystemRegistry, GameDataCache, 일괄 틱 표, 메시 무효화 훅, 프로파일러 트레이스 세션, GPU 흔적, 입력 플랫폼 정적, 차림 베이크 표, 자동화 씬 단계, UISystem 의 서비스 열둘(클립보드 · 전역 명령 · 바인딩 변환 · 알림 · 자막 · 툴팁 · 문서 · 스타일 시트 · 테마 · 오프스크린 · World 위젯 · 접근성). UISystem 에 남은 것은 입력 라우터 · 페인트 상태 · 레이어 숨김(화면 스택 자체).
- 클래스 분해: FrameRenderer → `FramePSOManager` · `FramePostProcess` · `FrameExtensionHost` · `FrameViewResources` · `FramePassExecutor` · `FrameRenderState`(헤더 1015 → 494 줄), InputMap → `InputGlyphResolver` · `InputRemapSerializer` · `InputAccessibilitySettings`, AudioEngine → `AudioSnapshotMixer` · `AudioMusicPlayer`, 편집기 패널 넷 모델/뷰, `EditorAssetType` 닫힌 열거형 → 엔진 종류 정의 열쇠 + `SW_EDITOR_ASSET_TYPE` 등록, PhysicsSystem 백엔드 팩토리, Widget 자식 목록 가상화, UI 캡처 GL 분기 → 렌더러 물음.
- 유틸 · STL: `HashUtil::mix32` · `mix64`, `HexUtil`, `reserveUniquePath`, `OnceKeySet`, `VarIntUtil`, `StringUtil::toBytes`, `ThreadUtil`(sleep · yield), `MathUtil`(min · max · clamp), `formatToString` + 이어 붙이기 252 줄, 실수 글 규칙 하나(`%#` = 가장 짧은 왕복 표기, printf 실수 서식은 여섯 자리).
- 작은 파일 · include: 게임 헤더 13 · 데이터 종류 18 · 리로드 캐시 · 호스트 창구 · 자기 .cpp 전용 헤더 · 계정 포트 합치기, 안 쓰는 파일 여섯 삭제, `RHITypes.h` 쪼개기, `ReflectionCast.h` · `Component.h` 가 `ReflectionTypes.h` 를 끌지 않는다, ImGui · imgui-node-editor 가 프로젝트 헤더로 새지 않는다, 전방 선언 다수.

**남은 것**
| 일 | 내용 |
|---|---|
| 정적 API 캐시 | MeshCache · SharedAssetCacheDefinition · MeshUtil · SpriteRenderUtil · SpriteMeshBuilder · ShaderReflectionLibrary · ShaderCookStamp · ContentHash · AssetPaths 해석 캐시 · StringTable · SceneTransformStorage · SystemFontLocator 의 `ScopeOwned` 를 서브시스템 소유로(부르는 쪽 중 범위 없는 쿠커 · 도구 · 워커는 이미 가진 문맥으로) — 진행 중 |
| STL 나머지 | `std::atomic` · `std::pair` · `VectorUtil` 관용구 · C 문자열 |
| 같은 모양의 큰 클래스 | `CanvasRenderer` · `RenderPacketBuilder` · `GameInstance` · `Engine`(서비스를 값으로 들고 중계), 1000 줄 넘는 다른 편집기 패널(모델/뷰) |
| 측정 | `castTo` 의 파생 확인이 줄 밖 함수가 됐다 — 뜨거운 경로(~4 ns) 측정 |
| 남긴 것(이유 있음) | Core 아래 층의 `ScopeOwned`(TagID · LogOnce), OS 가 요구하는 프로세스 전역(신호 처리 · 서비스 콜백 · RenderDoc · CRT 누수 기준), 프로세스 유일 번호 카운터, 진단 카운터, SQL 드라이버 표(링크 시점 목록) |

### H. 뜻이 안 보이는 이름 (조사 2026-10-29, `BugIt` → `BugReport` 와 같은 종류)

**끝남(2026-10-29)**: 아래 표를 모두 바꿨다(H1 · H2). `SmartObject*` 는 `UseSlotSlots` 처럼 말이 겹쳐 `UsableObject*` 로 했다. 디버그 키 사용자 파일 이름이 `debugkeys.xml`(루트 `<DebugKeyBindings>`)로 바뀌어 예전에 다시 묶은 키는 한 번 다시 묶는다. 콘텐츠의 소문자 에셋 이름(`benchscene.xml` · `benchtextured.material`)은 에셋 id 라 두었다.

기준: 여러 엔진 · 업계가 함께 쓰는 공용어는 둔다. 한 엔진 · 한 게임의 고유 이름 · 브랜드 · 약어는 내용으로 바꾼다. 별칭은 두지 않는다.

| 지금 | 바꿈 | 규모 | 까닭 |
|---|---|---|---|
| `PersistAcrossScenesComponent` | `PersistAcrossScenesComponent` | 3 파일 | 유니티 API 이름 |
| `HidingGhostDef` | `HidingGhostDef` | 3 파일 | 닌텐도 캐릭터 이름 |
| `AbilityPresentationDef` | `AbilityPresentationDef` | 9 파일 | 언리얼 GAS 은어 |
| `AbilityComponent` · `Ability` · `AbilityEffect*` · `TagCountTable` · `AbilityTaskWaitEvent` | `AbilityComponent` · `Ability` · `AbilityEffect*` · `TagCountTable` · `AbilityTaskWaitEvent` | 35 파일 | "Gameplay" 가 내용을 말하지 않는다(언리얼 GAS 그대로) |
| `UsableObjectComponent` · `UsableObjectDef` | `UseSlotComponent` · `UseSlotDef` | 12 파일 | 언리얼 기능 이름 |
| `Br*`(배틀로얄 키트) | `BattleRoyale*` | 12 파일 | 약어 |
| `Metro*` · `MetroidvaniaDeathRestState` · `MetroidvaniaCharm*` | `Metroidvania*` · `MetroidvaniaDeathRestState` · `MetroidvaniaTrinket*` | 12 파일 | 약어 · 다른 게임 은어 |
| `Witcher*`(폴더 `MonsterHuntRPG`) | `MonsterHunt*`(`MonsterContract` · `Alchemy` · `QuickSpell`) | 13 파일 | 게임 브랜드 |
| `ServerPoseOverlay` · `gv_serverPoseOverlay*` · `net.serverOverlay` | `ServerPoseOverlay` · `gv_serverPoseOverlay*` · `net.serverOverlay` | 6 파일 | 넷코드 은어 |
| 명령 `vlog.*` | `visuallog.*` | 2 파일 | 타입은 `VisualLogger` 인데 명령만 약어 |
| `gv_stressTest*` · `StressTestScene*` · `UIStressTestScreen` | `gv_stress*` · `StressTestScene*` · `UIStressTestScreen` | 35 파일 | 줄임말이고 좌석 슬롯 `bench` 와 겹친다 |
| `gv_netEmulation*` · `gv_ioEmu*` | `gv_netEmulation*` · `gv_ioEmulation*` | 약 30 줄 | 타입(`NetEmulation`)과 철자를 맞춘다 |
| `DebugKeyActions` · `debugkeys.rebind` · `debugkeys.xml` | `DebugKeyActions` · `debugkeys.rebind` · `debugkeys.xml` | 16 파일 | "셸" 이 무엇인지 안 보인다 |
| `ScreenshotRequestQueue` | `ScreenshotRequestQueue` | 5 파일 | 비유 이름 |
| `gv_deferredShading` · `gv_meshMorphDebugMode` | `gv_deferredShading` · `gv_meshMorphDebugMode` | 24 줄 | 무엇이 디퍼드 · 진단인지 없다 |

둔 것(공용어 · 실제 이름): `Cook*`(언리얼 · 프로스트바이트 · id 가 함께 쓰는 업계 말이고 대안 "Build" 는 빌드와 겹친다 — 2 절 규칙에 적는다), `Pawn` · `Possess`(게임 프레임워크의 일반 말로 굳었고 82 파일 — 대안이 더 모호하다), `Breadcrumbs`(Aftermath · 언리얼 공용), `EditorChrome`(UI 업계 말), `Tracy` · `LiveLinkFace`(실제 라이브러리 · 앱 프로토콜), `cheat.god` · `GM`(게이머 · 운영 공용어), `KartGhost`(레이싱 장르 표준), `DrainGate`.

## 5. 이번 세션의 근본 원인 (같은 결함이 남았을 곳)

- 층 위반은 include 글자 검사로는 직접 include 만 보였다 → 빌드 형식(층 부분 + `#error` 껍데기)으로 바꿔 전이 include 까지 막는다. Core 는 아직 티어 표만 있고 껍데기가 없다 — 같은 방식을 Core 에 넣을 후보.
- 실행기 없는 시나리오 검사가 엔진 서비스가 있어야 푸는 이름(입력 슬롯)을 "틀렸다" 로 냈다 → `AutomationStepDependency::InputSlot`. 검사 함수가 이름을 새로 묻게 되면 `findUnresolvedDependency` 에 더한다.

## 6. 이번에 고친 기동 결함

- 깨끗한 체크아웃의 App 이 서지 않았다: 엔진 기본 텍스처의 원본 · 생성기가 없었고(원본으로 둠), 약어 정리가 핫 리로드 ABI 스탬프의 머리를 C++ 쪽만 바꿔 모든 모듈을 거절했다(머리를 C++ 한 곳으로).
- `CommandLineManager::initialize` 가 인자마다 단언을 펼쳐 Debug 스택 틀이 인자 수에 비례했다(인자를 더하면 기동 전 스택 넘침) — 단언을 함수 하나로.
- 기본 파이프라인의 모든 실행이 Empty 확장 데모 오류를 남겼다 — 확장 셰이더를 선언하지 않은 파이프라인에서는 확장이 쉰다.
