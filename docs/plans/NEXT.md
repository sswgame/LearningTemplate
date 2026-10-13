# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(한 일은 `git log`, 교훈은 코드 주석 · 결정 기록 · 검증 문서에 있다).
작업 방식은 [작업 방식](../11_Workflow.md), 남은 일 전체는 [백로그](../06_Backlog.md)(분류 A~H)입니다.

## 지금 상태 (2026-10-14)

- push 는 통합 조정자가 한다. 이번 세션에 들어간 것: 게임별 프리셋 삭제 · sccache 제거(PCH 는 모든 구성에서 켬) · CI 수정 둘(`427a2e8c6` · 오디오 디코드 태스크 수명 `ef6e5cda8`) · B-11 · B-13 · B-14 · 쿠킹 스탬프 지문(`054a116fd`) ·
  게임 · 에디터 서비스의 범위 서브시스템(`SubsystemScope`, `6ccdeb60e`)과 에디터 내부 정리(`EditorContext` 는 범위를 드는 얇은 껍데기 — 상태는 `EditorRenderViews` · `EditorPlaySessionState` · `EditorTheme`, 결정 기록 5-8).
  에디터 정리의 검증: Debug `all AllTests` 경고 0, `ctest -L "lint|nogpu"` 112/112, 네 백엔드 `-EnableEditor` 120 프레임 오류 0. **에디터 시나리오(`EditorScenariosPassOn*`) 네 백엔드는 조정자가 돌린다.** 전체 `hostgpu` 는 `29c15ad22` 뒤 **다시 안 돌렸다**.
- GameFramework 분류(2026-10-14, 결정 기록 5-8): **서브시스템으로 옮김** — 화면 전환 · 로딩 화면 · 네트워크 세션(`GameInstanceSubsystem`, 게임 범위 소유, 세션의 정적 `s_pActiveSession` 삭제).
  **제외(게임플레이 객체 · 데이터)** — 카탈로그 · gamesettings(빌려 건다), 스냅샷 상태 저장소 · 씬 로드 대기(인스턴스 흐름), 키트 시스템 · 컴포넌트.
  **정적 남김(정의 약 75)** — 테이블 이름 상수 43 · 빈 값 7 · 불변 표 · 상태 없는 드라이버(Postgres · SQLite · OpenSSL) · 링크 때 등록 표(SQL 드라이버 · 로컬 저장소 · 서버 키트 팩토리 · 저장소 드라이버 표 ·
  GameDataCache — 이것은 `C-27` ② 에셋 캐시와 함께) · 프로세스 공유 메모리 DB · 원샷 이미터 id · 스크래치 버퍼 · 지금 게임 범위 포인터. **나중에 옮김** — 에디터 플러그인 함수 정적 둘(`C-27` ④ 의 모듈 기여 창구 뒤).
- `C-26` 은 닫았다(서브시스템으로 만들지 않고 종료 순서 단언을 넣음 — 결정 기록 5-5). 다음: `C-27` ④ 의 모듈 기여 창구(G-1 렌더 패스 등록 + 에디터 플러그인 함수 정적 둘) — 끝나면 묶어서 검증 요청(네 백엔드 hostgpu · 에디터 시나리오). PCH 측정(조용한 기계 필요). `Tools/Sccache` 폴더 · `%APPDATA%/Mozilla/sccache` 는 다른 빌드가 끝난 뒤 지운다.
- 사용자 방침: 폴리싱에 집중, 운영 · 배포는 보류(`H-3`). **성능 증거는 반드시** 남긴다.

## 사용자가 나중에 하기로 한 것

- **스레드 안전 탐지기가 못 보는 결함 종류 메우기** — 백로그 `C-24`. `DataRaceDetector` 는 `sw::` 컨테이너 래퍼 안만, `DeadlockDetector` 는 `sw::mutex` 대기 그래프만 본다.
  후보: `join` · 이벤트 대기를 대기 그래프에 넣고 정적 소멸자 · `DllMain` 안 `join` 막기 / '대기 여부 + 결과' 를 한 번에 돌려주는 API / 풀 핸들 세대 확인. 전용 함수를 늘리지 말고 기존 모양을 고친다.
- **에디터 기동 시간 회귀 확인** — 라이프사이클을 `SubsystemCollection` 으로 옮긴 뒤 7초 안팎(이전 4~6초). 부하인지 회귀인지 미확정. 기계가 조용할 때 병합 전(`22b94b2e4`)과 후(`52bf2b4b7`) 바이너리를 번갈아 5회씩 잰다. 그 뒤 서브시스템 2단계(`71ae65765`)가 더 들어갔으니 최신도 함께 잰다.

## 남은 검증 (이 순서)

1. **성능 증거(반드시)**: `PerfCompare` 9케이스와 `SceneLoadBench` 는 2026-10-13 에 쟀다(기준값은 [검증과 측정](../08_Verification.md), 중립이면 유지). `B-13` 의 구조 이동 측정(S12 ~ S22)도 끝나 회귀가 없었다. 남은 것은 이번 세션의 `TaskNode` 소유 포인터 변경(노드→매니저가 포인터 두 번)의 전후 측정이다. 비교용 기준선 워크트리를 남겨 두었다: `D:\w\perfbase`(`2772e1a7a`, Release `App` · `EngineTest`)와 `D:\w\perfbase2`(`6a86561ef`, S12 직전, Release `App`).
2. 전체 `hostgpu`(직렬 약 17분) 다시. 에디터 체인 DX11 · GL · Vulkan 은 최신 병합 뒤 안 돌렸다.
3. 네 백엔드 스크린샷 비교. 게임별 CMake 프리셋은 지웠다(2026-10-14) — 게임 코드는 `Ninja-Debug` 가 모두 짓고 실행은 `-game=<게임>` 이다.
   **`C-25` 는 거의 끝났다(2026-10-14, 마지막 세 커밋은 push 안 함).** 모든 게임이 타깃 `SWGame_<게임>`(모듈 이름 `SWGame`), 게임끼리 PCH 하나, `Ninja-Debug` 가 8게임을 150초에 짓고,
   `App.exe -game=<게임>` · `Server.exe -game=<게임>` 으로 고른다. 호스트 테스트에 게임 팩마다 도는 사례가 생겼다(DX12 직접 실행으로 10 시나리오 통과, 네 백엔드는 아직).
   남은 것은 백로그 `C-25` 의 셋(더한 키트 적재 · 서버 `.ogg` 경고 · 새 호스트 사례 네 백엔드).
4. `nogpu` 가 느리다(합 540초, `-j 2` 실제 약 324초): AppTest HostOnly 샤드 세 개가 86~136초, EngineTest NoGPU 샤드 여섯 개가 39~66초. 샤드를 더 쪼개거나 기동 체인 중복을 줄일 것(사용자가 물었음, 결정 대기).

## 열린 결함 (백로그 A)

- 범위 밖 관찰: GL 체인 1회에서 `editor.bugit` · `editor.viewportgrid` 가 SceneViewCameraX 기대 미달로 졌다(창이 앞에 오지 못해 건너뛴 단계가 같이 있었다).- TDR 뒤 행(hang)의 원인은 알 수 없음.

## 진행 중: 서브시스템 정리 (2026-10-14, 단계마다 main 에 병합)

순서: ① ISubsystem · SubsystemCollection 을 Core 로(끝) ② RHI 이전(끝) ③ 엔진 수명 매니저가 ISubsystem 직접 상속(끝, `93f0693e1`, EngineLoop.cpp 1361 → 822 줄)
④ 소유 · 조회 흡수 ⑤ 부트스트랩 분해 — 조회 창구는 main 의 `SubsystemScope`(`6ccdeb60e`)를 쓰고 ④b(세 범위 + ⑤ 를 함께 — 서비스 범위의 객체는 이름 풀 · 로거 뒤에 만들어야 해서 둘을 나눌 수 없었다)
끝, EngineLoop.cpp 822 → 887 줄(부트스트랩 · 서비스 범위 등록이 들어왔고 EngineBootstrap 201 줄 · EngineServiceCollection 265 줄이 없어졌다).
④c(모듈은 호스트가 넘긴 서비스 범위에서 찾는다 — ModuleServiceTable · 호스트 서비스 목록 삭제) · ④d(엔진 루프 · 부트스트랩 · 헤드리스 헤더는
호스트만 — CheckEngineLayers) 끝. ⑥ EngineLoop → Engine 개명(조정자가 시점을 알림). 그 뒤 C-27 ① ② ③ ⑤ ⑥(조정자 분담 — C-26 · C-27 ④ 는 다른 통합 담당).
- ④b 모양: 호스트는 `_bootstrap` → `_services` → `_startup` 세 컬렉션을 부모로 잇는다. 서비스 표(`engine::getXxx`)는 소유 없이 바인딩 때 한 번 찾아 둔 캐시이고,
  필수 행이 비면 호스트가 서지 않는다(`CheckEngineServiceBinding` 게이트는 지웠다). 세 범위는 `SubsystemScope`(타입 해시), 서브시스템이 아닌 행은 호스트가 `bind`.
  하네스는 `EngineSubsystemHost::kTestHarness` 로 서서 클라이언트 전용을 표의 자리로만 둔다. 테스트의 서비스 갈아 끼우기는 `test::ScopedEngineServiceOverride`.
- ③ 끝: FunctionSubsystem · FunctionSubsystemDesc · EngineFunctionStep · EngineInitSequence.h 가 없다. 엔진 수명 매니저(TaskManager · AsyncFileIO ·
  CompressionCodecRegistry · ConfigManager · AssetManager · IAudioService · InputManager · AssetStreamingQueue · SceneManager · UserSettingsManager)가 직접 상속하고,
  호스트 엮기는 작은 클래스다 — Reflection · ModuleImages · ModuleTypes(Engine/Module), EngineDefaultAssets(Localization), Headless(Engine 루트),
  RHI · FrameRenderer · RenderThread · LiveShader · SceneRHI(Engine/Renderer). 에디터의 기동 단계 일곱은 ImGuiEditor 의 중첩 클래스다.
  역방향 의존(`getSubsystemDependents`)으로 높은 층이 낮은 층의 순서를 적는다. ResourcePakManager · ServerServiceManager 는 아직 범위 밖이다(서비스 표의 행이 아니다 —
  C-27 정리 때 본다).
- ④ 요구(조정자): Engine 클래스는 호스트만 안다(Games · GameFramework · Editor 가 include 하면 층 게이트 실패), 모듈 조회 창구는 읽기 전용 `ISubsystemLocator`
  (Core 의 SubsystemScope 가 구현, 호스트가 모듈 API 표로 넘김), 컴포넌트는 `SceneEngineServices` 주입 유지 · 확대, 씬 수명은 `ISceneService` 로 따로,
  DLL 경계 조회는 이름 · TypeInfo 기반(핫 리로드 SmokeTest 로 확인). 호스트 범위 컬렉션(App · ServerApp · ModuleHost)이 엔진 범위 위에 얹힐 수 있게 로케이터에 범위 체인을 열어 둔다.
- 설계 메모(구현은 ⑥ 뒤): 엔진의 ModuleImages · ModuleTypes 단계가 ModuleHost 델리게이트를 부르는 호스트-엔진 고리는 호스트 범위 서브시스템
  (ModuleHost)을 엔진 범위의 부모로 두고, 엔진의 ModuleImages 가 "부모 범위의 모듈 호스트" 에 의존하는 꼴로 표현할 수 있는지 본다 — DLL 언로드는 씬 뒤 ·
  서비스 앞 구간이라 엔진 범위의 종료 두 패스(stop → shutdown) 사이에 있어야 한다.

## 이어서 할 정리 (사용자가 순서를 정하면 시작)

- 서브시스템: 플랫폼 플래그 `_bHostStarted` · `_bSchedulerStarted` 제거. `OnlineServerRuntime` · `App` · `ModuleHost` · `FrameRenderer` · `ServerApp` · `OnlineServiceHost` 는 LIFO 가 아니라 일부러 안 옮겼다.
- 감사에서 나온 반복 패턴: 등록자 템플릿 `ScopedRegistration<TRegistry,TEntry>`(등록자 11 + 테스트 4), 에디터 프로브 상태 `caseState<T>()`, `IServiceStoreWork` 람다(29), 온라인 메서드 X-macro(`kVersion` 올림 필요), 알림 핸들러, 컴포넌트 인스펙터, `GameObjectManager` 프레임 단계 표.
- 폴더: 평평한 `Editor/Common/Commands`(61) · `Engine/Object/GameObject`(45) · `Engine/Renderer/Frame`(43) 쪼개기. `EditorNetPlaySession.h` → `Workspace/`.
- 서버 디버그 `S3 · S6 · S7`(`S8` 은 다중 월드 필요), 다중 월드 `D3-7`, 에디터 보강 `D3-1` 남은 것.
- 명명 검토 `C-17` 에 "적용기"(Applier 의 번역어, 상용 엔진 용어 아님) 추가 후보.

## 검증 명령

- 그림 불변: `App.exe -<dx11|dx12|vk|gl> -gv_benchMeshes=8 -gv_benchGround=1 -gv_benchAnimate=0 -gv_profileFrames=20 "-gv_screenshot=s.ppm"` 병합 전/후 sha 동일.
- 무효 bindless 인덱스 추적: `-gv_validateBindlessIndex`.
- 에디터 시나리오: `AppTest --test_filter=*EditorScenariosPassOn*` 4백엔드. 지역화: `App.exe --check-text`.
- 테스트 빌드: `cmake --build --preset Ninja-Debug --target all AllTests` 다음 `ctest --test-dir build/Ninja-Debug -L "lint|nogpu" -j 2`. **결과를 확인한 뒤에 push 한다.**

## 알아 둘 함정

- **모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 말 것.** DLL 을 내릴 때 정적 소멸자는 로더 락을 쥔 채 돈다. 스레드를 가진 객체는 `EditorContext` 처럼 수명이 명시적인 소유자에 둔다.
- **무효 bindless 인덱스(0xFFFFFFFF)** 는 DX12 에서 페이지 폴트, Vulkan 에서 DEVICE_LOST 가 된다. 셰이더는 `swBindlessTextureSlot` 으로 마스킹한다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders` 로 다시 굽는다.
- **`castTo<Component>( Component*, 타입 )` 은 항상 성공한다.** 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )`.
- **테스트에서 워커가 공유 `sw::vector` 의 칸에 쓰면** 레이스 탐지기가 컨테이너 전체 동시 쓰기로 본다. 고정 배열을 쓴다.
- **서브시스템**은 `ISubsystem` 을 상속하는 클래스이고 `kSubsystemName` 상수를 둔다. 의존 칸은 다른 클래스의 상수를 가리키며(철자는 컴파일 오류), 낮은 층이 높은 층보다
  늦게 서야 하면 높은 층이 역방향 의존으로 적는다. 정렬 오류(순환 · 호스트 어긋남)는 `EngineInitSequenceTest` 가 호스트 종류마다 잡는다.
- 새 `SW_API`/`SW_GF_API` 클래스가 다른 모듈에서 쓰이면 선언에 매크로를 붙이고 `Core/Common/Macros.h` 를 직접 include 한다.
- 같은 기능을 두 작업자가 따로 만들 수 있다: 병렬 지시 때 소유 범위가 겹치는 기능은 한 명에게만. 작업자는 커밋만 하고 병합 · 빌드 · push 는 사용자 세션에서.
- 워크트리 정리: 정션(`Tools/*`, `build/vcpkg_*`)을 `cmd //c rmdir` 로 **먼저** 끊고 폴더를 지운다. 안 그러면 공유 도구가 지워진다.
- 커밋 훅은 우회하지 않는다. 실패하면 `Tools/LLVM/bin/clang-format.exe -i <파일>` 후 다시 `git add`. 스크립트로 파일을 고칠 때는 heredoc 대신 Write 도구로 scratchpad 파일을 만들고 CRLF 를 보존한다(`sed -i` 는 CRLF 를 지운다).
