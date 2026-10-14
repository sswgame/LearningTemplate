# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 끝난 일은 여기 적지 않습니다 — 한 일은 `git log`, 결정과 근거는 [결정 기록](../09_Decisions.md), 검증 방법과 기준값은
[검증과 측정](../08_Verification.md), 남은 일 전체는 [백로그](../06_Backlog.md)(분류 A~H)에 있습니다. 작업 방식은 [작업 방식](../11_Workflow.md)입니다.

## 1. 지금 상태 (2026-10-14)

main 은 이 문서의 커밋이고, 그 앞이 C-27 ③ · ⑥ 커밋(외부 프로파일러 · 메모리 예산 · 리소스 루트 서브시스템, OnlineLoadBot 의 부트스트랩 서브시스템)이다.
push 와 CI 확인은 조정자의 일이고, 이 문서를 쓸 때의 CI 결과는 적지 않았다. 서브시스템 정리의 ① ~ ⑤ 와 C-27 ① ② ③(남은 것 하나) ④ ⑥ · C-26 이 main 에 있습니다.
원격 브랜치 `origin/wt/rest`(빌드 전 WIP `7e7c1c0fd`)는 이제 쓸모없다 — 조정자가 `git push origin --delete wt/rest` 로 지운다.

**에디터 보강 브랜치 `wt/editor`(2026-10-14, 사용자 지시로 main 에 병합하지 않고 별도 브랜치로 push).** main 의 이 문서 커밋에서 갈라 에디터 보강 묶음을 넣었다 — 버그 리포트 창 · 인스펙터 보기 옵션 ·
컨텍스트 메뉴 확장 지점(하이어라키 · 콘텐츠 브라우저 · 뷰포트) · UI 디자이너 슬롯 편집 + 앵커 프리셋 · 대화 · 애니메이션 그래프 저장 시나리오(`asset.open`) · `cheat.*` 치트 명령 · 시나리오 녹화 · 포토 모드 ·
`LightingOnly` 보기 모드 + 셰이더 재쿠킹 산출물 · 입력 바인딩별 시간 · Negate · MultiTap · 패드 가상 커서. 2차 묶음(같은 날): 어빌리티 디버거 · NPC 일정 창 · 오디오 이벤트 창 · 리그 디버그 창 + 시퀀서 `RigSlot` 트랙 · 모듈 창 상세 ·
인스펙터 즐겨찾기 · UI 디자이너 끌기 손잡이 · 지형 브러시 기초 · 시나리오 녹화 휠 기록. 설계 문서 둘: [다중 월드](EditorMultiWorld.md) · [캐릭터 편집 창](EditorCharacterEditor.md) (코드 없음, 다른 PC 에서 병합할 때 근거).
`D3-1` ⑦ 은 구현하지 않고 까닭과 단계적 대안을 결정 기록 5-11 에 적었다. 검증: Debug `all AllTests` 경고 0, `ctest --preset Ninja-Debug-nogpu` 48/49
(실패 하나 `PythonTest_TestAcronymSpelling` 은 이 PC 에서 파이썬이 git 출력을 파이프로 받지 못하는 환경 문제). **검증하지 않은 것**: Shipping 빌드 · 테스트, GPU 테스트, 에디터 시나리오 실행, 눈으로 본 확인,
그리고 커밋 전 훅(모든 커밋이 `--no-verify`). 병합 전에 [백로그](../06_Backlog.md) `D3-9` 의 확인 목록을 먼저 본다. 덤프(에디터 패널 덤프 · 크래시 덤프)와 서버 타깃은 하지 않았다(사용자 지시).

## 2. 이번 세션에서 확정된 방침

- **상용 엔진과 비교해 고른다.** 언리얼 · 유니티 · 고도의 방식과 견주어 더 낫거나 같으면 그대로 따르고, 우리가 더 나은 점은 확장으로 남긴다. 근거는 결정 기록에
  "~로 알고 있다" 로 적는다(기억에 의존한 비교임을 밝힌다).
- **구조는 규모가 커도 장기 이득이면 과감히 바꾼다.** 래퍼 · 별칭 · 옛 이름 호환은 두지 않는다(데이터도 다시 쓴다).
- **서브시스템 판정 기준**: 수명 범위 · 초기화/종료 순서 · 의존이 있는 서비스는 서브시스템이다. 프레임마다 도는 핫 데이터 · 게임플레이 객체 · 값 객체 ·
  정적 초기화의 등록 표는 아니다(예: 에셋 캐시는 값 저장소라 제외 — 결정 기록 리소스 절).
- **검증에 Shipping 빌드 + Shipping `nogpu` 를 넣는다.** Debug 는 할당 헤더 모양이 달라 정렬 할당 객체의 잘못된 해제 같은 힙 손상을 못 본다.
  단계 검증: Debug `all AllTests` 경고 0 → `ctest -L "lint|nogpu"` → Ninja-Shipping `all AllTests` 경고 0 → Shipping `ctest -L nogpu` → (해당하면) 앱 실행 · 모듈 리로드.
- **에이전트는 push 하지 않는다.** 조정자가 push 하고 `gh` 로 CI 를 본다. `gh` 는 `C:\Program Files\GitHub CLI` 를 PATH 에 더해야 잡힌다.
- **에이전트를 띄울 때 모델을 밝힌다**(보고 · 커밋의 `Co-Authored-By` 줄과 같게).

## 3. 서브시스템 정리 — 남은 단계

진행 방식: 단계마다 커밋하고, 그 단계의 검증(2절)을 통과한 뒤 main 에 ff 병합합니다. 단계 안에서 빠뜨린 것은 다음 단계로 미루지 않고 고칩니다.
지금 모양: 호스트(엔진 루프 · 테스트 하네스)가 부트스트랩 → 서비스 → 엔진 세 `SubsystemScope` 를 부모로 잇고, 씬은 `GameObjectManager` 의 씬 범위,
게임 · 에디터는 각자 범위를 듭니다. 서비스 표(`engine::getXxx`)는 바인딩 때 한 번 찾아 둔 캐시이고, 모듈은 호스트가 넘긴 서비스 범위에서 타입으로 찾습니다.

1. **C-27 ③ 의 남은 것** — 부트스트랩 순서는 진단 → 이름 풀 → 로거 → 명령줄 → 크래시 핸들러 → 리소스 루트 → 메모리 예산 → 정적 저장소다
   (`EngineInitSequenceTest.BootstrapScopeRunsFromDiagnosticsToStaticStorage` 가 지킨다). 메모리 예산 표는 첫 프레임 끝에 읽는다(타입 등록부가 엔진 범위라서).
   남은 ③: 자동화 구동기(`AutomationSession`)는 App 호스트 범위로(4번과 함께). `Dev` 호스트 비트는 두지 않는다 — 개발 도구는 호스트가 `#if !defined( SW_SHIPPING )`
   한 자리에서 넣는다. `DevCommandRegistry` · `AutomationStepRegistry` · `AutomationProbes` 는 정적 등록 표라 제외.
2. **C-27 ⑤ 판정을 결정 기록에 적는다.** 오디오는 이미 역할별 팩토리가 `Audio` 서브시스템 하나를 고르고(언리얼 오디오 믹서 플랫폼과 같은 모양), 물리는
   Jolt · Box2D 가 갈아 끼우는 쌍이 아니라 함께 쓰는 둘이라 바꿀 것이 없다 — 코드를 한 번 더 보고 확정한다.
3. **`EngineLoop` → `Engine` 개명**(조정자가 시점을 알린다). `sw::Engine`(클래스)과 `sw::engine`(네임스페이스)의 철자 충돌을 먼저 정리한다. 약 95 파일과
   AGENTS.md · 결정 기록까지 고친다.
4. **호스트 범위**(App · ServerApp · ModuleHost 의 컬렉션, 개명 뒤). 엔진 범위의 부모로 두고, 엔진의 ModuleImages · ModuleTypes 단계가 ModuleHost 델리게이트를 부르는
   고리를 "부모 범위의 모듈 호스트에 의존" 으로 바꿀 수 있는지 본다. DLL 언로드는 씬 뒤 · 서비스 앞이라 엔진 범위 종료의 두 패스(stop → shutdown) 사이에 있어야 한다.
   `_bHostStarted` · `_bSchedulerStarted` 같은 플랫폼 플래그도 이때 지운다.
5. **판정 남은 것**: `ResourcePakManager`(AssetManager 가 소유) · `ServerServiceManager` 를 서브시스템으로 둘지 2절 기준으로 정해 결정 기록에 적는다.

지킬 요구: 엔진 루프 · 부트스트랩 · 헤드리스 헤더는 호스트만 include 한다(`CheckEngineLayers` 가 Games · GameFramework · Editor 에서 막는다). 모듈 조회 창구는
읽기 전용 `ISubsystemLocator`(Core)이고 타입 해시로 DLL 경계를 넘는다. 컴포넌트는 `SceneEngineServices` 주입을 쓴다.

## 4. 검증 창 결과

통합 담당 1 이 main `2e36f8f11` 에서 돌렸다(`D:\w\scope`).

- Debug `all AllTests` 경고 0 통과. **Release 빌드 · 스크린샷 sha 대조는 실행하지 않았다.**
- `ctest -L hostgpu` 직렬 1차(1373 초): 7 개 중 4 통과, AppTest_HostOnly_Shard1 ~ 3 실패(EngineTest 와 나머지는 통과).
  원인: 네 백엔드에서 `editor.extensionpanel` · `editor.chainreloadkitext`(두 번) · `editor.hidelock` · `editorstartup`(패널 수 35 기대, 실제 34)과
  `AppSmokeTest.EditorSelfTestsPassInsideTheEditor` 가 졌다 — C-25 커밋 `1edc65fac`(게임이 쓰지 않는 키트를 올리지 않는다)로 기본 게임 Empty 에서 ThemePark 키트와
  그 에디터 확장이 올라오지 않는데 테스트가 그것을 기대했다. `editor.hidelock` 은 단독 3/3 통과(체인 앞 단계 실패의 여파로 본다).
  Vulkan 만 `editor.uipreview` 가 한 번 더 졌다(언어 버튼 이름표 없음, 1 회, 미확정).
- 수정(시나리오 XML 넷 + `TestAppSmoke.cpp` 를 Empty 기준으로 — 패널 수 34, extensionpanel · reloadkitext 를 GF_Admin 으로, netplay 서버 접속 줄 확인을 종료 뒤로,
  `KitExtensionSelfTestsPassInTheirGame` 추가)은 `1f7c3657b` 로 들어갔다. 그 뒤 `EditorScenariosPassOn*` DX12 · DX11 · Vulkan · OpenGL 모두 통과
  (체인 실패 0, 에디터 모듈 로드 로그 확인).
- 재실행에서 진 둘: ① Shard1 `EditorScenariosPassOnDirectX12` — `editor.netplay` 22 프레임에서 서버 접속 줄 `ExpectLog` 0 건(`netplay.wait` 는 클라이언트 줄만
  기다리고 서버 줄은 서버 출력 파이프로 늦게 온다). 단독 5 회 중 1 회 실패 — 테스트 경합이고 확인을 종료 뒤로 옮겨 6/6
  (로그 `D:\w\scope\build\Ninja-Debug\Bin\Saved\Automation\netplay_dx12.log`). ② Shard2 `EveryGamePackScenarioPassesOnEveryBackend` —
  `game/shooter3d/automation/closewindow.scenario.xml -dx11` 종료 코드 10, `ExpectCursorClip: cursor clip is not inside the client area (clip 0,0,2560,1440)`.
  단독 3/3 통과, 창이 앞으로 오지 못한 포커스 문제로 의심(미확정), 서브시스템 변경과는 무관해 보인다(로그 `...\Bin\Saved\Automation\closewindow_dx11.log`).
- 미확인: 마지막 AppTest(필터 `*EditorScenariosPassOn*` + `EditorSelfTestsPassInsideTheEditor` + `KitExtensionSelfTestsPassInTheirGame`)의 스모크 둘은
  셸 종료 코드 127 이라 결과를 보지 못했다(로그는 조정자 scratchpad 의 `v_ed.log`).
- 따로 본 결함(미조사): 키트 리로드 때 `Online server kit 'chat'/'admin' is registered twice` 가 [Error] 로 남는다 — 백로그 `E-11`.

**새 세션의 첫 일(남은 검증)**: Release 빌드 + 네 백엔드 스크린샷 sha 대조(기준 `D:\w\perfbase` 는 Ninja-Release 만 있다), 수정이 들어간 뒤 `hostgpu` 재확인
(1차와 같은 조건 — 직렬), 위 스모크 둘 확인. 그다음 3절.

## 5. 그 밖의 남은 일 (우선순위 순)

1. **C-24 스레드 안전 탐지기의 빈틈** — `join` · 이벤트 대기를 대기 그래프에, 정적 소멸자 · `DllMain` 안 `join` 막기, "대기 여부 + 결과" 를 한 번에 돌려주는 API,
   풀 핸들 세대 확인. 새 함수를 늘리지 말고 기존 모양을 고친다. `D:\w\rest` 에 `git stash`("c24wip" — `sw::thread` 래퍼 초안 · 테스트)가 있다.
2. **C-28 전역 서비스 접근자 호출 줄이기**(서브시스템 정리의 남은 단계 뒤) — `engine::getXxx` 호출 약 826 줄 · 209 파일을 주입으로 줄인다.
   컴포넌트 · 씬은 `SceneEngineServices`, 서비스 클래스는 만들 때 · 초기화 때 의존 서브시스템 포인터를 멤버로 받는다. `getTypeRegistry`(273)는 쓰임새를 보고
   `TypeLookupCache` 같은 기존 구조로 풀지 판단한다. 호출 분류 → 많은 접근자부터 주입 → 0 이 된 행의 getter 삭제, 접근자 단위 커밋 · 성능 측정(백로그 `C-28`).
3. **폴더 쪼개기** — 평평한 `Editor/Common/Commands`(61) · `Engine/Object/GameObject`(45) · `Engine/Renderer/Frame`(43). `EditorNetPlaySession.h` → `Workspace/`.
4. **C-17 이름 점검** — "적용기"(Applier 번역어, 상용 엔진 용어 아님) 후보를 더한다.
5. **C-3** — 백로그 항목대로.
6. **`nogpu` 샤드 결정** — 합 540초(AppTest HostOnly 샤드 86~136초, EngineTest NoGPU 39~66초). 샤드를 더 쪼갤지 · 기동 중복을 줄일지 사용자 결정 대기.
7. **`TaskNode` 소유 포인터 변경 전후 성능** — 기준선 워크트리 `D:\w\perfbase`(`2772e1a7a`) · `D:\w\perfbase2`(`6a86561ef`)와 비교. 중립이면 유지.
8. **PCH 측정** — 모든 구성에서 PCH 를 켠 뒤의 빌드 시간.
9. **에디터 기동 시간** — 7초 안팎(이전 4~6초). 조용한 기계에서 병합 전(`22b94b2e4`) · 후(`52bf2b4b7`) · 최신을 번갈아 5회씩.
10. **E 절 관찰** — 백로그 E 절의 관찰 항목.
11. **A 열린 결함** — GL 체인 1회 `editor.bugit` · `editor.viewportgrid` 기대 미달(창이 앞에 오지 못함), TDR 뒤 행의 원인 미상.
   그 밖: 반복 패턴 정리(`ScopedRegistration<TRegistry,TEntry>` · `caseState<T>()` · `IServiceStoreWork` 람다 · 온라인 메서드 X-macro), 서버 디버그 `S3 · S6 · S7`,
   다중 월드 `D3-7`, 에디터 보강 `D3-1`.

## 6. 알려진 함정

- **정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨진다**(Debug 는 조용). `sw::default_delete` 가 그 변환을 컴파일 오류로 막는다 — 제 타입으로 들거나
  넘긴 타입으로 지우는 소유자(`SubsystemCollection` · `SubsystemScope`)에 넘긴다.
- **엔진 정리를 정적 소멸에 맡기지 않는다.** `std::exit` 앞에서는 하네스 · 범위를 명시적으로 내린다(정적 소멸 순서는 범위의 역순을 지키지 않는다).
  이름 풀 표는 정적 저장소에 세워 종료 정적 소멸에서 빠졌다.
- **낡은 빌드 폴더는 sccache 런처를 캐시해 둔다** — sccache 를 지운 뒤 예전 빌드 폴더를 그대로 쓰면 구성이 없는 런처를 부른다. 빌드 폴더를 새로 구성한다.
  `Tools/Sccache` 폴더는 삭제 대기다.
- **워크트리 정리**: 정션(`Tools/*`, `build/vcpkg_*`)을 `cmd //c rmdir` 로 **먼저** 끊고 폴더를 지운다 — 안 그러면 공유 도구가 지워진다.
  남은 워크트리: `D:\w\c19` · `c20` · `c21` · `c22` · `c23`(병합 끝 — 정리 대상) · `scope`(통합 담당 1 의 수정이 main 에 들어갔으니 정리 대상) ·
  `rest`(C-27 ③ · ⑥ 은 병합 끝 — C-24 stash "c24wip" 만 남아 있어 그것을 옮기기 전에는 지우지 않는다) · `perfbase` · `perfbase2`(성능 기준선 — 남긴다). 워크트리가 아닌 폴더 `c25p` · `rfP1` 도 남아 있다.
- **모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 말 것** — DLL 을 내릴 때 정적 소멸자는 로더 락을 쥔 채 돈다.
- **무효 bindless 인덱스(0xFFFFFFFF)** 는 DX12 페이지 폴트 · Vulkan DEVICE_LOST. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders` 로 다시 굽는다.
- **`castTo<Component>( Component*, 타입 )` 은 항상 성공한다** — 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )`.
- **커밋 훅은 우회하지 않는다.** 스크립트로 파일을 고칠 때는 heredoc 대신 Write 도구로 scratchpad 파일을 만들고 CRLF 를 보존한다(`sed -i` 는 CRLF 를 지운다).
  bash 큰따옴표 안의 백틱은 명령으로 실행된다 — 주석 글을 셸 인자로 넘기지 않는다.

## 7. 새 세션 시작 절차

1. 이 문서 → [작업 방식](../11_Workflow.md) → [백로그](../06_Backlog.md) C 절(`C-24` · `C-27` · `C-28`) → [결정 기록](../09_Decisions.md) 서브시스템 절 순서로 읽는다.
   C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md).
2. 4절의 남은 검증(Release 빌드 + 스크린샷 sha 대조 · hostgpu 재확인 · 스모크 둘)을 먼저 한다.
3. 그다음 3절 2번부터 이어간다(1번 C-27 ③ · ⑥ 병합은 끝났다).
