# 다음 세션 인계 — 구조 개편

> 질문: 지금 어디까지 왔고, 다음 세션은 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **바로 다음은 [StructureRefactor](StructureRefactor.md) 의 9 단계 나머지**(아래 목록), 그다음 10 단계, 그다음 SOLID · 프로젝트 구성 점검 리팩토링(사용자 요청 2026-10-31). 0 ~ 8 단계는 2026-10-31 에 끝났다(매 묶음 전체 ctest 통과, 캐시를 비운 실행 포함):
> - 형식 · 루트: `Core/Path/AbsolutePath.h`(`tryMake` · `tryResolve` - 사람이 적은 경로의 해석 규칙 하나), `Core/File/ProcessRoots.h`(실행 파일 위치로 한 번: `_binary` · `_project` · `_content` · `_packs` · `_saved`(실행 산출물) · `_derivedData`(구성이 함께 쓰는 DDC · 쿠킹 기록 · 개발 인증서) - 패키지는 `Content` 없이 `Packs` 만이라 바이너리 폴더가 프로젝트 루트), `Engine/FileSystem/ResourcePath.h`(`ResourcePath` · `DomainTable`, 아직 쓰는 곳 없음).
> - OS 경계(R1 · R2): 파일 시스템은 `Core/File/PlatformFileSystem.h`(`FileUtil` 구현만 include) → Win32 · POSIX, `std::filesystem` 0. **상대 경로가 OS 경계에 닿으면 `File.RelativePath` 오류로 실패한다**(`PlatformFileUtil::acceptsPathInput`). 라이브러리 `ModuleLibraryUtil::loadLibraryAt` · `loadSystemLibrary`, 실행 파일 `ProcessCommand::at` · `fromSearchPath`.
> - 입력 경계(R5): 명령줄 경로 인자 `CommandLineDiskPath`(틀린 인자는 기동 오류), 전역 변수 `GlobalDiskPath`, 개발 명령 `DevCommandRegistry::readDiskPathArgument`(절대만), 설정 파일은 프로젝트 루트 한 곳, 세이브는 사용자 데이터 폴더(`UserDataPath::resolve`), 실행마다 다른 폴더는 `reserveUniquePath`(프로세스 id 재사용 결함).
> - 쿠킹 입력 종류 셋(`CookInputKind` Resource · DiskFile · Listing, 기록 `SWCOOKREC 3`), 셰이더 소스 경로 추측 제거, 없을 수 있는 파일은 `FileUtil::readOptionalTextFile`.
> - 둔 것(이유): 파일 감시기의 디렉터리 핸들 · 크래시 처리기의 모듈 이름 조회(할당 없는 경로) · `ModuleBuildID`, 시나리오 단계 속성의 경로 종류 선언(`{out}` 은 명령 글 안에서도 쓴다 - 명령이 형식을 검사).
> - **Linux 는 WSL 에서 바뀐 파일의 문법 검사만 했다 — 실제 빌드 · 테스트는 아직**(WSL 클론 `~/LT-linux`).
> - 8 단계(T2) 끝: 팩 VFS 도메인 없는 키의 패치 교체 · 지움, dlc 팩, 팩 형식 2(마운트 도메인을 팩 안에), `collectAssets` 합집합, `hasAsset` 폴더, 오디오 디코드를 에셋 로드 단계로. 9 단계(T3) 끝난 것: 저장 입구 `save*(AbsolutePath)`, `AssetPaths::toDiskPath` · `findLooseFile`(`optional<AbsolutePath>`) 두 해석, 씬 소스는 늘 리소스 id(`saveActiveSceneAs`), 셰이더 소스 규칙 하나(`ShaderCompiler::findSourceFile`), 런타임 읽기 `AssetIO`(SQL · 외형 감시 `watchAssetFolder` · 모디파이어 파일). **남은 것**: `EditorSourceFile{ResourcePath, AbsolutePath}` 와 편집 문서 읽기 · 쓰기 넷을 하나로(포커스 블록 복사 11 벌 · 자체 저장 패널 8 개를 기반으로, `TileMapPanel` 의 두 종류 입력란), 도구 문서 경로(`EditorToolAssetCommands::resolveExistingOrRelativePath` 의 모양 규칙)를 그 형식으로, `isMountedDiskDomainID` 제거, Localization 문서 읽기 `AssetIO`.
> 그다음 4 절의 테스트 전수. L(상대 키)은 그 문서의 `ResourcePath` 로 대체됐다. 원칙: 게이트는 최후의 수단, `#if` 구역 대신 빌드 단위 · 확장점 · 개발 전용 이름 규칙.

## 1. 지금 상태 (2026-10-29)

- **빌드**: `Ninja-Debug`(활성 게임 Empty) · `Ninja-Release`(전체) 오류 0. **Linux**: 프리셋이 없다 — WSL(Ubuntu 26.04)의 따로 받은 클론에서 `cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug` + vcpkg 툴체인으로 짓고(단위 테스트 · 전용 서버 기동 통과), 받은 LLVM 의 `lld` 가 `libxml2.so.2` 를 찾아 서지 않으면 시스템 `lld` 로 바꾼다(26.04 는 libxml2.so.16). **테스트**: `ctest`(단위 `App --test` + 시나리오 엔진 3 · Empty 16) 통과 — 형식과 쓰는 법은 `Test/README.md`.
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
| 콘텐츠(씬 · 프리팹)가 쓰는 형식의 모듈은 그 도메인이 가진 것이다 — `game/<팩>` 은 그 게임 매니페스트 의존, `common/` 은 모든 게임이 가진 것, `editor/` 는 엔진 · EditorModule, `engine/` 은 엔진만. configure 가 맞대고 멈춘다(`Scripts/generate/ContentModules.py`, 씬을 고친 뒤에는 다음 configure 가 본다) | 언리얼 .uproject Plugins |
| 둔 것: `Editor*` · `Automation*` 접두어(타입 이름은 전역 유일), App 폴더(실행 파일 호스트 부품 — 언리얼 Launch), `*Info` 정적 사실 표(언리얼 FPixelFormatInfo), `Core/Container` 등 폴더 이름과 같은 접두어 묶음(언리얼 Containers 처럼 평평하게), `IWindowTitleTarget`(창 제목을 거는 길을 하나로 막는 이음매 — `MainWindowSubsystem` 이 비공개 상속, 테스트가 가짜를 넣는다) | |

## 3. 테스트 · 시나리오

`Test/README.md` 가 정본이다(쓰는 법 · 옛 문제와 고친 것 표). 요약: 단위 테스트는 기능 옆 `Tests/` 의 `SW_TEST_CASE` 를 `App --test` 한 번이 엔진 위에서 섞어 돌린다(로그 게이트 · 단언 0 건 실패 · 스냅숏 · 케이스별 쓰기 폴더). 시나리오는 적은 순서대로 도는 단계(`<Wait>` · `<WaitUntil>`), 백엔드별 골든 이미지(`-scenario-update-golden`), 시나리오 안 `<AllowLog>`, 실행별 산출물 폴더 · 사용자 설정이다.

- **백엔드당 프로세스 하나**: CTest 항목 `Scenario.<게임>` 하나가 시나리오 모두를 체인(`-scenario=a,b,c -scenario-keep-going`)으로 돌리고, `-scenario-backends=all` 이 백엔드마다 자식 App 하나를 띄운다(창은 2×2 바둑판 `-WinX/-WinY`). 단계마다 세션이 빈 씬으로 앞 씬을 비우고 → 전역 변수를 세션 시작 값으로 되돌리고 → `<Launch>` 값을 건 뒤 → 게임을 다시 세운다(`AutomationSession::startStage`). 렌더 파이프라인 변수는 실행 중에 다시 고른다(`FrameRendererSubsystem`). 둘째 단계부터는 게임 인스턴스를 상태 복원 없이 다시 세운다(`IGameRestarter` — App 이 `GameModuleManager` 를 호스트 범위에 건다. 게임을 내리고 → 활성 씬을 닫고 → 월드를 플레이 전으로 두고 → 새로 만든다, 언리얼 PIE 의 새 GameInstance). 그래서 단계마다 게임의 기동 흐름(첫 씬 · 벤치)이 첫 단계와 같은 길로 돌고, 시나리오 모두가 체인 하나다(벤치 · kaykit 포함 22 단계). 전체 ctest 85 초(처음 272 초), 동시 App 최대 5(부모 + 넷). 단계별 결과는 `Testing/Logs/Scenario.<게임>.<백엔드>.log` 의 `chain stage i/n`.
- **에셋 도착**: 시나리오 세션은 `gv_finishAssetWorkEachFrame=1` 도 건다 — 프레임 경계에서 렌더 스레드가 받은 패킷을 다 끝낸 뒤 에셋 로드 단계 · 스트리밍 큐 태스크 · 비동기 IO 가 함께 빌 때까지 기다리고(`AssetManager::finishPendingAssetWork`, 언리얼 FlushAsyncLoading), 렌더 스레드는 텍스처 업로드를 상한 없이 내고 끝남까지 기다리며(`TextureCache::finishPendingLoads`) 머티리얼 초기화 · 밉 스트리밍 상한을 풀고, 머티리얼 PSO 변형을 빌더 스레드 대신 그 프레임에 만든다. 그래서 요청한 에셋 · PSO 는 늘 같은 프레임에 보인다 — 예전에는 셰이더 캐시가 빈 첫 실행에서 디더 · runner 가 달랐다. 확인은 `Bin/Saved/ShaderCompileCache` · `ShaderCache` 를 지운 ctest.
- **결정적 실행**: 시나리오 세션은 `gv_animationWaitForClips=1` 을 건다 — 애니메이터가 시작 때 비동기로 읽는 클립을 `onBeginPlay` 에서 기다린다(언리얼 자동화의 FlushAsyncLoading). 끄면 첫 포즈가 나오는 프레임이 디스크 지연에 달려 래그돌 · 물리 이력이 갈렸다. 물리는 구성마다 같은 비트다: Jolt 는 `CROSS_PLATFORM_DETERMINISTIC=ON`(`ThirdParty/joltphysics/vcpkg-port`), `Engine/Physics` 의 TU 는 FMA 축약을 끈다(`sw_markDeterministicSources` — 파괴와 같은 규칙). 렌더는 대상이 아니다(디버그 셰이더 코드젠 · 렌더 수학의 FMA 로 전체 해상도 바이트는 다르고, 골든 비교 크기에서는 같다). 남은 문턱 안 차이(vfxgpulights 0.14 % · kaykitrig vk 0.28 %)는 그 렌더 차이다.
- **셰이더**: 소스 정책(개발 실행)은 쿠킹본을 읽지 않는다 — 로컬 캐시(`Bin/Saved/ShaderCache`) → 실시간 컴파일, 컴파일 결과 캐시는 `Bin/Saved/ShaderCompileCache`(언리얼 DDC). 리플렉션도 그 바이트코드에서 실행 중에 만든다. 실시간 컴파일의 기본은 쿠킹과 같은 최적화 코드젠이고 디버그 코드젠은 `-gv_shaderDebugCodegen=1`(RenderDoc, 언리얼 `r.Shaders.Optimize=0`). 쿠킹본 정책(배포 · `-cooked-content`)은 팩의 쿠킹본만 읽는다. 굽기는 `<쿠킹 폴더>/<도메인>/shaders/bin/<백엔드>` 에 쓰고(씬 · 프리팹과 같은 스테이징), 팩 굽기(`--cook-all` · `--cook-packs`)가 늘 바뀐 것만 다시 굽고 판정한 뒤 담는다. 소스 트리의 `shaders/bin` 은 낡은 잔재다(쿠킹본 접미사가 `Cooked` 역할이라 팩이 경고하고 뺀다).
- **시작 시점의 결정성**: 로딩 화면은 씬 로드 받아들임 알림(`SceneManager::onSceneLoadRequested`, 요청마다 `SceneLoadKind` 필수 — 편집기 · 진단은 `Tool`)에 게임이 붙어 띄우고(언리얼 PreLoadMap), 떠 있는 동안 게임 시간을 멈춘다(`LoadingScreen` 형식). 그래서 시나리오의 첫 프레임은 늘 월드의 첫 틱이다 — 예전에는 로드 시간 · 최소 표시 시간만큼 월드가 로딩 화면 뒤에서 돌았다(cloth 골든을 다시 떴다).
- **종료 코드**: 표 하나 `Config/Engine/ProcessExitCodes.json` → C++ `ProcessExitCode` · CMake `SW_EXIT_CODE_<이름>`.
- **남은 일**: 창 바둑판 간격은 창 틀 두께를 Win32 대략 값으로 둔다(`AutomationBackendMatrix` 의 shortcut) — 모니터가 2.6K×1.5K 보다 작으면 겹친다. 플랫폼 작업 영역 · 창 틀 크기 API 가 생기면 그 값으로.
- **겹치는 쓰기를 막은 곳**: 로그 파일은 싱크가 안다(`ILogSink::getLogFilePath` — BugReport · 콘솔 패널이 폴더의 "가장 새 파일" 을 고르지 않는다), 같은 초의 폴더 · 사진 이름은 `FileUtil::reserveUniquePath`(만들기가 곧 자리 잡기), 개발 TLS 인증서는 한 PEM(`devserver.pem`).

## 4. 남은 일

### L. 리소스 참조를 상대 키로 — 멈춤(2026-10-31, ③~⑤ 하지 않는다)

- **결정**: 데이터 참조는 전역 id 그대로 둔다. 언리얼(`/Game/…` · 플러그인 경로)과 Godot(`res://`)의 참조는 늘 전체 경로이고, 덮어쓰기는 같은 전체 경로를 더 높은 우선순위의 pak · PCK 가 덮는 방식이다 — 게임이 엔진 에셋을 상대 키로 가리는 장치는 둘 다 없다. 여기도 같은 장치(`PackManager` 우선순위 · `mountPatchPack`)가 있다. 상대 키 데이터는 참조 색인 · 이름 바꾸기 참조 고치기(`EditorReferenceIndex` · `replaceReference` — 전역 접두어로 찾는다)와 쿠커가 게임마다 다른 검색 순서로 풀어야 해 비용이 크고, 얻는 것(게임이 엔진 · 공통 에셋을 가리기)은 상용 엔진도 주지 않는다. 남긴 것: 게임 사이 참조 0(캐릭터 키트는 `common/`), 쓰기는 `DomainAssetID` 로만(`AssetPaths::toWriteID`). 아래는 조사 기록이다.

- **질문**: 검색 순서(`_listResourcePriority` = game · common · engine · editor, `dlc/<이름>` 토큰 가능)로 겹쳐 찾는 장치(`AssetPaths::findLooseFile` 의 팩 상대 키, `PackManager` 우선순위 · `mountPatchPack`)는 있는데 아무도 쓰지 않는다. 게임 · DLC · 패치가 같은 상대 경로로 엔진 · 공통 에셋을 덮지 못한다.
- **실측**(`scratchpad` 스크립트 — 다시 뽑을 때는 아래 4 의 검사로): 콘텐츠 데이터 458 파일의 참조 2,007 개가 모두 도메인 고정(`game/` 1,473 · `engine/` 510 · `common/` 24), 상대 키 0. 상대 키로 바꿔도 해석 대상이 바뀌는 참조 **0**. 같은 상대 키가 여러 도메인에 있는 것 43 — 게임끼리 36(한 번에 하나만 올라가 무해), 엔진 · 공통과 게임 4(`credits.md` · `textures_raw/import.stamp` 는 참조되지 않음, `materials/grass.material` engine↔themepark · `materials/water.material` engine↔voxelcraft 는 덮어쓰기인지 정한다). 깨진 참조 2(`game/empty/models/testhead.facial.xml` — 임포트 산출물인지, `game/empty/prefabs/old/testprop.prefab.xml` — GUID 로 찾는 옛 경로 시험인지 확인).
- **모양**: 데이터 `mesh="models/testhead.mesh"`(상대 키 — 이긴 파일), 일부러 고정할 때만 `material="engine/materials/grass.material"`(데이터 검사가 고정 참조로 목록에 올림). 코드 `AssetKey`(읽기 — `AssetManager::load` · 검색 순서) / `DomainAssetID{ "common", "textures/rock.dds" }`(쓰기 — 저장 · 임포트 · 이름 바꾸기 · 지우기는 이것만 받는다, 상대 키로 쓰면 컴파일 오류). 편집기: 콘텐츠 브라우저는 도메인 폴더 그대로 보이되 `[가려짐 ← game/x]` · `[덮음 → common]` 표시, 인스펙터가 고른 에셋은 상대 키로 저장(가려진 파일을 고르면 "실행에는 game 것 — 고정할까" 확인). 상용 엔진: Godot `res://` + PCK 겹치기, 언리얼 pak 우선순위(읽기는 겹쳐 찾고 쓰기는 마운트 지점을 붙인다).

### 그 밖의 남은 일

| 일 | 내용 |
|---|---|
| 테스트 전수 | 정본은 `PYTHONIOENCODING=utf-8 py -3 Scripts/dev/TestCoverage.py [--uncovered] [접두어]` 의 표다(2026-10-30: 기능 폴더 517 중 테스트 없음 325 · cpp 1117 — GameFramework 184 중 165, Engine 170 중 87, Editor 90 중 45). 2026-10-31 기준 폴더 517 중 테스트 없음 311(처음 325), 단위 케이스 514. 된 것: GameFramework 일정 · 인벤토리 · 전투 · 자동 저장, 온라인 원장 · JWT · HTTP/1.1 파서 · HPACK · HTTP/2 프레임 · 서비스 저장소(조건 · 원자성 · 멱등) · 우편 · 제재, Engine JSON · XML · 현지화 서식 · PO · 객체 델타 · 공유 에셋 표 · 쿠킹 저장소(기록 · DDC). 테스트가 드러낸 결함 셋(알림 합치기의 null 화면, HTTP 머리 크기 상한 · 서로 다른 Content-Length)은 고쳤다. 다음: Engine 의 Asset/Image(DDS) · `PackCooker` · Serialization/Serializer · Reflection · Net(`GameNetSession` 은 단위가 아니라 `net-pair` CTest 로), GameFramework 의 나머지 온라인(Bus · Cache · Directory · Server · Service), Kits. 일정의 하루를 도는 게임 시나리오(`gv_timeScale` 16 배)는 아직 |
| 테스트 늘리기 | 옛 `Test/`(3f70e665f^ 에 약 950 파일)에서 옮길 것을 기능 옆 `Tests/` 로 — 매크로 이름이 같아 순수 함수 테스트는 include 와 위치만 바꾸면 된다. 엔진 서비스가 이미 서 있으므로 옛 하네스는 필요 없다 |
| 게임 시나리오 | Empty 외 게임의 옛 시나리오(27b17ee91^)를 옮긴다 — 변환은 `<At>` → 순차(`<Wait>`), `probe` → `watch`, 지표 문턱 → 골든, 씬 `WorldSettings` → `SceneSettings`. 활성 게임을 바꿔(`SW_ACTIVE_GAME`) 골든을 뜬다. 에디터 시나리오도 같다(에디터 실행) |
| **구조 리팩토링(다음 묶음)** | 설계 · 순서 · 검증은 [StructureRefactor](StructureRefactor.md) 가 정본이다(2026-10-31 Opus 설계 검토 6 건을 합침). 규칙: OS 경계 하나(`PlatformFileUtil` 네이티브) · 디스크 I/O 는 절대 경로만 · 리소스는 `ResourcePath{도메인(선택), 상대 경로}` 만 · 쓰기는 `FileUtil` 이 절대 경로로 · 상대 → 절대는 기동 때 고정한 루트로 입력 경계에서 한 번 · 폴백 없음 · 확인과 실행은 같은 해석 · 같은 일은 하나 · `#if` 대신 빌드 단위. 0 단계(`AbsolutePath` 엄격 판정 · `ProcessRoots` · `ResourcePath` · `DomainTable`)부터 |
| `Scenario.editor` 속도 | `uiaudit` 은 캡처 단계가 레이아웃 안정을 스스로 기다리게 해 고정 대기를 지웠다(2026-10-31). 남은 큰 단계: `flowterrain`(73 초 — 플레이 모드를 여러 번 들고 남, 한 번 4~5 초) · `prefabwindow` · `meshmodifiers` · `terrainbrush`(각 30 초 안팎). 캡처 앞의 고정 대기는 다른 시나리오 15 개에도 있다 — 굽기 · 썸네일을 기다리는 것인지 보고 조건(`<WaitUntil>`)으로 |
| `GameFramework/Base` · `Plugin/Editor/Base` | 이름이 모호하다(GameFramework 의 기반 라이브러리 · 그 에디터 확장). 바꾸면 모듈 이름(`GF_EditorPlugin_Base`) · 매니페스트가 따라간다 |

## 7. 다음 설계

### D. 빌드 속도 마무리

- 가장 무거운 컴파일: 편집기 `MaterialPanel` · `PrefabPanel`(각 50 초대, Release) — 무엇이 무거운지(템플릿 · ImGui 헤더 · 단일 거대 함수) `-ftime-trace` 로 보고 나눈다.
- `.ninja_log` 에 지운 옛 테스트 실행 파일(TestBin/*)이 남았다 — 빌드 폴더의 옛 산출물은 다시 구성할 때 지운다.

### E. 테스트 틀 고도화 (안정성 · 속도 · 더하기 · 빼기)

남은 것: 옛 케이스 이식. 기계 이식(2026-10-29)이 Core 55 · Engine 41 파일을 옮겼고, 빠진 것(Core 56 · Engine 약 520 · Reflection 14)은 지워진 하네스(TestBench · 자식 프로세스 · 픽스처 파일) · 바뀐 API 에 기대 손으로 다시 쓴다(시나리오나 `SW_TEST_CASE_F`). 다시 기계로 하려면 파일 이름으로 include 풀기 → 티어가 가장 높은 폴더의 Tests/ → 빌드 -k 0 로 안 되는 파일 빼기 → 실행해 실패 파일 빼기. 쓰는 법은 `Test/README.md`.

### F. 시나리오 범용화와 게임 쪽 확장

지금 결합(조사): 결과 = 프로세스 종료 · 입력 기본 Exclusive(OS 입력 막음) · `gv_fixedFrameDelta` 강제 · 산출물 기본 · 명령줄로만 시작 · 행 감시가 `_Exit`. 단계는 어느 모듈이든 등록할 수 있다(`SW_AUTOMATION_STEP`, 키트 · 게임 등록은 아직 0). 겹치는 기능: 시퀀서(`Engine/Sequencer` — 판정 · 입력 없음), 입력 녹화 `InputReplay`(`.swreplay`, 시나리오와 같은 `IVirtualInputSource`), `ScenarioRecorder`(녹화 → 시나리오 XML).

**나눌 층**: 재생기(`ScenarioPlayer` — 단계 · 타임라인 · 가상 입력, 배포에도 있음, 게임 코드가 `play(asset, options)` 로 시작하고 끝 델리게이트를 받는다)와 그 위의 판정(`AutomationJudge` · 로그 게이트 · 골든 · 종료 코드 — 테스트 실행만). 시나리오 파일의 테스트 전용 칸(`AutomationTestOptions`)은 이미 따로 있다.

| 쓰임 | 빌려 올 곳 | 필요한 것 |
|---|---|---|
| 튜토리얼 · 온보딩(플레이어가 X 를 하면 다음) | 하스스톤 · 대부분의 튜토리얼 스크립트 | `WaitUntil` 의 게임 조건(액션 · 감시 값) + UI 강조 · 안내 단계, 입력은 막지 않음 |
| 고스트 · 리플레이(타임 어택 고스트, 킬캠) | 마리오 카트 고스트, 스타크래프트의 입력 기반 결정적 리플레이 | `InputReplay` 를 액션(의도) 단위로 녹화 · 재생, 결정성(고정 델타 · 난수 씨앗) 단계 |
| 자동 플레이 봇 · 소크 테스트(밤새 돌리기) | 씨 오브 시브즈 자동 테스트, EA · 유비 봇 플레이 테스트, 언리얼 Gauntlet | 반복 · 무작위 분기(씨앗 고정) · 메모리 · 프레임 상한 단언, 행렬처럼 여러 프로세스 |
| 컷신 · 이벤트 연출에서 게임 행동 끼우기 | 언리얼 Sequencer 이벤트 트랙 · 유니티 Timeline Signal | 시퀀서 이벤트 트랙이 시나리오(또는 단계 묶음)를 부르고, 시나리오가 시퀀스를 재생 · 기다림(`PlaySequence` · `WaitSequence`) |

남은 것: 시퀀서 이벤트 트랙이 시나리오를 부르는 쪽, 액션 단위 녹화 · 리플레이(고스트 · 킬캠 — `InputReplay` 를 의도 단위로). 이미 있는 것: 게임 API `ScenarioPlayback::play` · 콘솔 `scenario.play`, 단계 `<LoadScene>` · `<PlaySequence>` · `<WaitSequence>` · `<BeginFrameStats/>` … `<EndFrameStats/>`.

### G. 리로드를 떼었다 붙이는 단위로

남은 것: 캐시 쪽 리로드 계약을 `IReloadableAssetCache` 로 떼어 배포 빌드의 `IAssetCache` 에 reload 가 없게 한다(텍스처의 리로드 세대는 비동기 로드와 섞여 있어 마지막에). 확인은 Shipping 빌드에서 `CodeWithin` · `reload` 기호가 남지 않는지 `llvm-nm` 으로(게이트로 둘지는 그때 정한다). 에셋 핫 리로드는 엔진에 넣지 않는다 — 개발 기능은 `DevTools`.

### I. 편집기 · 개발 기능을 엔진에서 떼기 (2026-10-29)

원칙: Engine · Core 에는 편집기 · 개발 도구만 쓰는 함수가 없다. `#if WITH_EDITOR` 구역도, 게이트도 두지 않는다(게이트는 최후의 수단) — **빌드 단위**로 가른다(언리얼 Runtime · Developer · Editor 모듈).

| 단위 | 담는 것 | 짓는 빌드 |
|---|---|---|
| `Core` · `Engine` | 런타임, 읽기 API, 읽기 · 쓰기 짝 직렬화, 확장점(이벤트 · 등록표 · 감쌀 수 있는 인터페이스) | 모두 |
| `DevTools`(`Source/DevTools`, 공유 라이브러리 `SW_DEVTOOLS_API`) | 개발 전용 — 설정 · 셰이더 리로드, 파일 감시 분배기, 개발 콘솔(UI 오버레이) · 디버그 HUD, 서버 포즈 고스트, 프레임 지연. 호스트가 `Engine::setStartupExtension` 으로 기동 단계에 서브시스템을 더하고 `Engine::getFrameHooks().add` 로 프레임 시점에 붙는다(상속 없음) | 개발 App · Server · 에디터 · GameFramework |
| `EditorModule` · 편집기 확장 | 편집 기능 전부(저장 · 프리팹 작업 · 굽기 · 패널). 데이터 사본을 고쳐 엔진 직렬화로 쓰고 런타임이 다시 읽는다 | 개발 App |

## 5. 근본 원인이 남았을 곳

- 모듈 언로드 훅(`onDetaching`)에서 서비스를 찾는 일 — 도구 명령 종료 크래시(2026-10-31)의 모양이다. 지금 남은 곳은 없다. 도구 실행의 종료 길은 CTest `ToolCheckText`(`App --check-text`)가 지난다.
- Linux 전용 .cpp(X11 · Wayland · ALSA · epoll)는 Windows 가 짓지 않는다 — Linux 빌드를 정기적으로 도는 것 말고는 길이 없다(CI 후보).
- 전용 서버 종료 때 태그별 증가 진단(Game 약 0.5 KB · Unknown · EngineMisc 각 32 B)이 Windows · Linux 에 함께 나온다. CRT 검사는 누수 없음 — 종료 직전까지 사는 할당이다. 어디서 오는지는 보지 않았다.
- Core 는 티어 표만 있고 층 껍데기(`#error`)가 없다 — Engine 과 같은 빌드 형식을 Core 에 넣을 후보.
- 실행기 없는 시나리오 검사가 새 이름 종류를 묻게 되면 `findUnresolvedDependency` 에 더한다(`AutomationStepDependency::InputSlot` 과 같은 방식).
