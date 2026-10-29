# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 다음 세션은 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: D · H 와 E · F · G 의 앞부분이 끝났다. **다음은 7 절 I(편집기 · 개발 · 테스트 기능을 빌드 단위로 엔진에서 떼기)부터** — 과도기 `EditorFoundation` 을 없애는 일(되돌리기 데이터화 · `IModuleStateStore`)이 첫째, 그다음 `DevTools` 단위, 테스트 · 자동화 실행기 분리다. 원칙: 게이트는 최후의 수단, `#if` 구역 대신 빌드 단위.

## 1. 지금 상태 (2026-10-29)

- **빌드**: `Ninja-Debug`(활성 게임 Empty) · `Ninja-Release`(전체) 오류 0. **테스트**: `ctest`(단위 `App --test` + 시나리오 엔진 3 · Empty 16) 통과 — 형식과 쓰는 법은 `Test/README.md`.
- **콘텐츠**: 임포트 · 쿠킹 산출물은 저장소에 없다(.gitignore) — 새 체크아웃은 Release App 으로 `--import-textures` · `--import-models` · `--import-heightfields` · `--cook-shaders` 를 차례로 돈다(Debug 의 텍스처 압축은 장당 20 분이라 쓰지 않는다). 엔진 기본 텍스처 둘(`missing` · `loading`)만 원본으로 둔다.
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
| 낮은 층 · 높은 층이 서로 알아야 하면 위가 아래에 건다: 팩 매니저(`AssetIO::bindPackManager`), 메시 무효화(`SW_MESH_INVALIDATION_HOOK`), 서브시스템 순서(위 층의 의존 칸) | 언리얼 FPlatformFileManager · 모듈 등록 |
| 키트 하나의 에디터 확장은 그 키트 폴더의 `Editor/`(런타임 · 서버로 나뉜 키트는 `<기능>/Editor/`). `Plugin/Editor` 에는 GameFramework 자체와 여러 키트를 잇는 확장만 | 언리얼 플러그인의 <X>Editor 모듈 |
| 폴더 · 파일 이름은 내용으로: `Engine/FileSystem`(층 1 콘텐츠 파일 시스템) · `Engine/Foundation` · `Core/{Foundation,Hash}` · `Engine/2D`(층 6, 언리얼 Paper2D) · `Graphics/LOD` · `RHI/{Backend,Transfer,Diagnostics}` · 에셋 파일 `*Asset` · XML 정의 `*Def` | |
| 둔 것: `Editor*` · `Automation*` 접두어(타입 이름은 전역 유일), App 폴더(실행 파일 호스트 부품 — 언리얼 Launch), `*Info` 정적 사실 표(언리얼 FPixelFormatInfo), `Core/Container` 등 폴더 이름과 같은 접두어 묶음(언리얼 Containers 처럼 평평하게) | |

## 3. 테스트 · 시나리오

`Test/README.md` 가 정본이다(쓰는 법 · 옛 문제와 고친 것 표). 요약: 단위 테스트는 기능 옆 `Tests/` 의 `SW_TEST_CASE` 를 `App --test` 한 번이 엔진 위에서 섞어 돌린다(로그 게이트 · 단언 0 건 실패 · 스냅숏 · 케이스별 쓰기 폴더). 시나리오는 적은 순서대로 도는 단계(`<Wait>` · `<WaitUntil>`), 백엔드별 골든 이미지(`-scenario-update-golden`), 시나리오 안 `<AllowLog>`, 실행별 산출물 폴더 · 사용자 설정이다.

- **병렬**: CTest 항목은 시나리오마다 하나이고, 그 App 이 `-scenario-backends=all` 로 백엔드마다 자식 App 을 동시에 띄워 모은다(`AutomationBackendMatrix` — Gauntlet 컨트롤러 자리, 자식은 `kProcessKillSeconds` 180 초 안에 끝나야 한다). 항목끼리도 `ctest -j4` 로 돈다(76 실행 64 초). 스레드 병렬은 쓰지 않는다 — 창 · 장치 · 전역 변수 · 씬이 프로세스에 하나라서.
- **로그**: `-log-file=<경로>` 가 그 파일 하나에 쓴다(ctest 는 `<빌드>/Testing/Logs/<항목>[.<백엔드>].log`). 명령줄이 로거보다 먼저 서고, 싱크가 서기 전의 줄은 `Logger` 가 고정 배열 64 줄로 쌓았다가 넘긴다(언리얼 backlog).
- **종료 코드**: 이 PC 에서 못 도는 백엔드(77)는 행렬이 그 백엔드만 건너뜀으로 센다.
- **겹치는 쓰기를 막은 곳**: 로그 파일은 싱크가 안다(`ILogSink::getLogFilePath` — BugReport · 콘솔 패널이 폴더의 "가장 새 파일" 을 고르지 않는다), 같은 초의 폴더 · 사진 이름은 `FileUtil::reserveUniquePath`(만들기가 곧 자리 잡기), 개발 TLS 인증서는 한 PEM(`devserver.pem`).

## 4. 남은 일

| 일 | 내용 |
|---|---|
| 테스트 늘리기 | 옛 `Test/`(3f70e665f^ 에 약 950 파일)에서 옮길 것을 기능 옆 `Tests/` 로 — 매크로 이름이 같아 순수 함수 테스트는 include 와 위치만 바꾸면 된다. 엔진 서비스가 이미 서 있으므로 옛 하네스는 필요 없다 |
| 게임 시나리오 | Empty 외 게임의 옛 시나리오(27b17ee91^)를 옮긴다 — 변환은 `<At>` → 순차(`<Wait>`), `probe` → `watch`, 지표 문턱 → 골든, 씬 `WorldSettings` → `SceneSettings`. 활성 게임을 바꿔(`SW_ACTIVE_GAME`) 골든을 뜬다. 에디터 시나리오도 같다(에디터 실행) |
| `GameFramework/Base` · `Plugin/Editor/Base` | 이름이 모호하다(GameFramework 의 기반 라이브러리 · 그 에디터 확장). 바꾸면 모듈 이름(`GF_EditorPlugin_Base`) · 매니페스트가 따라간다 |
| 남은 처리기 인터페이스 | `IEngineFrameObserver` · `IWindowTitleTarget` 은 등록 + 델리게이트 후보(프로토콜 인터페이스는 둔다) |
| `-log-file` 과 크래시 묶기 | 크래시 리포트 묶기(`CrashReportService::collectSessionLog`)는 `*_<세션>.txt` 로 로그를 찾아 고정 이름 로그를 못 찾는다 — ctest 는 크래시 파일 옆에 로그가 있어 당장은 문제없다 |
| 그 밖 | 없는 스크립트를 부르는 곳(`Scripts/qa/ValidateAssets.py` · 설정 색인 · 아이콘 폰트 · `RunBuildBaseline.py`), 지운 `docs/plans/*` 를 가리키는 주석, 생성기 · CMake 파일 이름의 `Abi`(`Scripts/generate/EngineAbiStamp.py` · `cmake/Engine/EngineAbiStamp.cmake`) |

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
- 1: 스위치 `SW_WITH_DYNAMIC_MODULES`(`Core/Foundation/DynamicModules.h`, 배포 0)가 언로드 장치 · RHI DLL 로드 · 로그 싱크의 코드 범위 떼기 · 등록표 리로드 대체(`RegistrationReloadScope::isActive` 가 constexpr false)를 뺀다. 처리기 멤버 13 곳은 `SW_MODULE_UNLOAD_HANDLER` 한 줄.
- 2 의 일부: `Engine/AssetReload/`(개발 빌드만) — `FileWatchDispatcher` · `ConfigReloadSubsystem`(편집기 없는 개발 App · 서버도 설정을 다시 읽는다, 편집기는 `EditorToolDefaultsReload` 처리기만). 게임 쪽 `AppearanceDataWatcher` 는 그 분배기의 클라이언트. Core 의 OS 감시기 · 리로드 어댑터 캐시(오디오 · 현지화)는 배포 빌드에서 빠진다.
- 3: `LiveShaderSubsystem` 은 배포 빌드에서 짓지도 세우지도 않는다.
- Shipping 빌드 · 쿠킹 · 실행(시나리오 sceneready PASS) 확인.
남은 것은 I 절로 옮겼다 — 에셋 핫 리로드를 엔진으로 넣지 않는다(편집 기능은 편집기, 개발 기능은 엔진 밖 `DevTools`). 위 2 의 `Engine/AssetReload` 도 엔진 밖으로 나간다.
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
| `DevTools`(만들 것) | 개발 전용 — 에셋 · 설정 · 셰이더 리로드, 파일 감시. 엔진 확장점에 데코레이터 · 등록으로 붙는다 | 개발 App · Server |
| `EditorFoundation` | 편집기 리로드에도 살아야 하는 상태 · OS 도구(되돌리기 · 세션 · 복사 버퍼 · 파일 대화상자) — App 이 링크하고 호스트 범위에 건다 | 개발 App |
| `EditorModule` · 편집기 확장 | 편집 기능 전부(저장 · 프리팹 작업 · 굽기 · 패널). 데이터 사본을 고쳐 엔진 직렬화로 쓰고 런타임이 다시 읽는다 | 개발 App |

**끝남**: 현지화 수집 · 번역 메모리, 의상 아틀라스, 리타깃 점검 · 기준 포즈 덮어쓰기, 문자열 변경 구간, 편집 조건식, 리그 되쓰기, 지형 저장, 프리팹 인스턴스 되돌리기 · 갱신, 에셋 파일 지우기를 편집기로. 리타깃 `*ForEdit` → 일반 설정자.
파일 대화상자는 편집기(`Editor/Common/Backend/FileDialog`)로 — 스레드를 들고 있다가 닫히면 합류시키고, 그 전까지 `LiveReloadManager::addReloadHold`(편집기 API `isReloadHeld`)가 리로드를 대기열에 잡아 둔다(버리지 않는다).

**`EditorFoundation` 없앰(2026-10-29)**: 되돌리기는 데이터 단계(`UndoStep` — 함수 이름 해시 + 인자 바이트, `SW_UNDO_STEP( 함수, 인자... )`, 처리기는 처음 쓰일 때 스스로 등록)이고,
리로드 · 백엔드 교체 사이 상태는 호스트의 맡김 칸(`ModuleAPI/Service/IModuleStateStore` — App 의 `ModuleStateStore`, 호스트 범위)으로 넘긴다. 편집기 쪽 주인: `EditorUndoHistory`(메인 스택 · 씬 변경 구독) ·
`ObjectCopyBuffer`(정적 전역 → 편집기 서브시스템) · `ImGuiEditor` 세션(선택 · 미저장). 패널 인스턴스에 묶인 명령(UI 미리보기 · 열린 문서)만 코드 단계(`UndoStep::makeTransient`)이고 맡길 때 버린다.
키트 · 게임 모듈도 같은 칸으로 리로드 사이 상태를 넘길 수 있다.

**남은 것**(순서대로):
1. `DevTools` 단위 신설 — `Engine/AssetReload`(파일 감시 분배기 · 설정 리로드), `LiveShaderSubsystem` · `ShaderRecompiler`, Core 의 OS 파일 감시기를 옮긴다. 엔진은 "설정을 다시 읽어라" · "이 셰이더를 바꿔라" 같은 확장점만 둔다.
2. 에셋 리로드를 데코레이터로 — `IAssetCache::reload` 를 런타임 계약에서 빼고, `DevTools` 가 캐시를 감싸 리로드를 덧붙인다(리로드 전용 어댑터 캐시도 그쪽으로). 텍스처 캐시의 리로드 세대는 비동기 로드와 섞여 인터페이스부터 나눈다(가장 큰 일).
3. 편집기 `AssetHotReload` 는 편집기에 두고 `DevTools` 의 감시 위에 선다.
4. 테스트 · 자동화도 같은 기준("배포한 게임이 쓰는가") — 테스트 틀(`Core/Testing`)과 기능 옆 `Tests/` 는 소스 자리는 두고 Engine.dll 이 아니라 테스트 단위(Core · Engine 테스트 DLL, `App --test` 가 올린다)가 짓는다.
   자동화의 판정 · 골든 · 보고 · 백엔드 행렬 · 종료 코드(`Engine/Automation/Runner`)는 엔진 밖 개발 단위로. 시나리오 재생기 · 단계 · 프레임 통계(내장 벤치마크 · 어트랙트 모드)는 배포 게임이 쓰므로 Engine 에 남는다.
5. 굽기(`LightProbeBaker` · `ReflectionProbeBaker`)는 편집기만 부른다 — 쿠커처럼 헤드리스 App 이 부르는 길이 없으면 편집기로(테스트 `TestLightProbeBaker` 는 편집기 쪽 테스트 자리가 생길 때 같이).

### J. 구조로 풀 수 있는데 우회로 막는 자리 (감사 2026-10-29)

원칙(I 와 같다): 게이트 · `#if` · 주소 훑기 · 주석 규칙 대신 빌드 단위 · 확장점 · 데이터 · 형식. 값이 큰 순서:

| # | 지금 | 고칠 모양 | 크기 |
|---|---|---|---|
| 1 | 안 쓰는 테스트 훅 `FrameRenderer::setDebugOverrides`(`FrameDebugOverrides`) · `Memory::injectAllocationFailures` | 지운다 | 5 파일 |
| 2 | `EventDispatcher` 채널 표가 처음 만든 모듈의 템플릿 썽크를 가리켜 그 DLL 을 매핑한 채 둔다(`outKeepLibraryMapped` · `DeferredLibraryUnloadQueue::_bKeepMapped`) | 채널 항목을 타입 지운 `MulticastDelegate<void(const IEvent&)>` + Core 의 비템플릿 broadcast 로 — keep-mapped 경로 전체가 빠진다 | 4–5 |
| 3 | 모듈 언로드 때 레지스트리마다 주소 범위를 훑어 뗀다(`SW_MODULE_UNLOAD_HANDLER` · `isCodeWithin` · `removeCodeWithin` 9 종 — AssetManager 는 vtable 주소까지) | 모듈의 모든 등록을 모듈이 소유한 `Subscription` 묶음(ModuleScope)에 담고 언로드 = 묶음 파괴. `Subscription` 을 안 돌려주는 등록(`UIBindingConverterRegistry` · `CompressionCodecRegistry` · InputManager 슬롯 · IWindow 처리기)만 고친다 | ~20 |
| 4 | 등록 해제를 주석 규칙에 맡김(`CompressionCodecRegistry::unregisterCodec`, `CrashContext::setCrashExtraTextWriter`, `ServerServiceManager::stopAll`, `AssetManager` 종료 경고) | 모두 `Subscription` 을 돌려주고 3 의 ModuleScope 가 소유 | 6–8 |
| 5 | 개발 명령 · 디버그 HUD · 개발 콘솔이 Engine/Core 안에서 `#if SW_DEV_COMMANDS_ENABLED` 로 파일째 감싸짐(~28 파일) · 엔진 루프가 개발 도구를 이름으로 부른다(`Engine.cpp` `#if` 13 곳) | `DevTools` 단위(I 절)로 옮기고 `IEngineFrameObserver` · 서브시스템 등록으로 붙는다 | ~40 |
| 6 | 쿠커(PackCooker · SceneCooker · ShaderCooker …)가 Engine.dll 에 있고 런타임 읽기마다 `CookJob::admitRead`, `HeadlessSubsystem` 의 하드코딩 분기 | 쿠킹 단위로 분리, 헤드리스 작업은 이름 등록 commandlet, `AssetIO` 에는 읽기 관찰자 자리 하나 | ~25 |
| 7 | "쿠킹본만 읽기" 를 `#if SW_SHIPPING` 으로 파일마다(SceneDocument · PrefabAsset · ShaderCache · ConfigManager …) | 호스트 조립 때 고르는 읽기 정책 하나(`ContentSource`), 소스 XML 리더는 쿠킹 단위로 | 8–10 |
| 8 | 리플렉션 메타데이터의 에디터 칸(category · tooltip · `_bCallInEditor` …)이 `#if !SW_SHIPPING` | 생성기가 에디터 메타를 사이드 테이블(`*.editor.gen.cpp`)로 내고 편집기가 짓는다 | 생성기 + 6 |
| 9 | 회선 에뮬레이션 데코레이터를 `GameNetSession` 이 `#if` 로 붙임 | 세션에 transport 데코레이터 등록 자리, `DevTools` 가 등록 | 3 |
| 10 | `RHIBackendRegistry` 가 백엔드 헤더 넷을 include 하고 DLL 로드까지 | 백엔드가 스스로 등록, DLL 로드는 ModuleManager | 4 |
| 11 | 모듈 언로드 정리 함수가 런타임 타입에 `#if`(`GameObjectManager::destroyComponentsOfModule` 등) | 범용 API + 타입의 모듈 태그(데이터), 절차는 ModuleManager | 6 |
| 12 | `GameObject::_bHiddenInEditor` 를 렌더러가 읽음 | 편집기 숨김 집합을 뷰의 제외 필터로 | 4 + 편집기 |
| 13 | `IEngineFrameObserver` 에 자동화 전용 칸(실행 상태 · 사진 경로) | 자동화 관찰자가 자기 서비스로 | 4 |
| 14 | UI 데모 · 스트레스 화면이 Engine.dll | Games/Empty 또는 DevTools | 4 |
| 15 | 그 밖: 의사 현지화 `#if`(쿠킹이 뺀다), AbilityComponent 의 vtable 훑기(타입 id 로 다시 만들기), 린트 include 규칙 1(pch 강제 포함과 중복), 롤백 결정성 · 리플렉션 후보 CMake 게이트(타깃 종류 · 생성 대상으로), 호스트 전용 `SceneManager::update`(HostOnly 노출), 순서 규칙 주석(API 모양으로) | | 각 1–3 |

**전역 상태 → 서브시스템**(조사 2026-10-29): 정적 등록표(모듈이 정적 초기화 때 채우는 표 — `AssetTypeRegistry` · `AutomationStepRegistry` · `UndoStepRegistry` …)는 범위보다 먼저 채워지므로 정적이 맞다. 바꿀 것은 **프로세스 상태를 정적 칸에 둔 곳**이다 —
`ScopeOwned` 전역 캐시 ~25 곳(MeshCache · SceneTransformStorage · ShaderReflectionLibrary · PhysicsMeshShapeCache · StringTable · ContentHash · AssetPaths 해석 캐시 · SystemFontLocator · GameDataCache …),
`ScenarioPlayback::s_pPlayer`(호스트 범위 조회로), `GameTimeScale` 의 프레임 시간, `PackPatchTrustSource` 의 공급자 함수, `MeshCache` 무효화 훅 목록, `TickBatchRegistry` 상태, `AutomationSceneSteps` 의 대기 목록(실행기 상태로),
편집기의 포커스 스택 · 선택(`EditorService` 의 `s_pFocused*`), `ViewportInputOverlay` 설정. 서브시스템이면 소유자 · 수명 · 의존 순서가 드러나고 테스트가 범위마다 새로 세운다.

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
