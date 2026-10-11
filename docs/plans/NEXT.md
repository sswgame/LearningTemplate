# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다.

## 검증 결과 (2026-10-11 전체 검증 — 에디터 보강 뒤 한 번)

PC `SW`(Ryzen 7 6800H), main `603653abc` + 이 검증의 고침. WSL 리눅스는 돌리지 않았다(CI 결과로 본다).

- **통과**: `ctest --preset Ninja-Debug-lint` 63/63, Debug 풀 빌드(`all AllTests`) 경고 0, Debug `-L nogpu` 46/46(`SmokeTest` 포함 — V1 의 "41/42 이름 모름" 은 더 없다),
  Debug `-L hostgpu` 7/7(`AppTest_HostOnly_Shard1~3` 426 · 478 · 272 s — 에디터 시나리오 60 개 × 네 백엔드 건너뜀 0, `sceneviewgameview` 포함),
  Shipping 빌드 경고 0 · `-L nogpu` 45/45 · `-L hostgpu` 7/7, 대표 실행 네 백엔드 × Empty · 에디터(`-EnableEditor`, 에디터 로드 확인) · Shooter3D · NileCity
  (16 실행 모두 종료 코드 0 · `[Error]` 0, Empty 벤치 스크린샷 sha `549917D10CD5` 네 백엔드 같음, 남은 프로세스 0), Debug-Server 빌드 · `-L nogpu` 43/43,
  Shipping-Server 빌드 경고 0, 게임별 프리셋 여덟(`Ninja-Debug-<Game>`) 구성 경고 0 · 빌드 경고 0.
- **흔들림 판정**: `assetmanage`(Vulkan) · `profilercapture`(DX11)는 ctest 두 번 + 직접 세 번 = 5/5 통과(재현 안 됨).
  `ActionCombatKit_MonsterStatsAreInMetersAndReachUnitStats` 는 `--test_shuffle=11 · 12 · 13 · 14`(EngineTest 전체)와 `GameFrameworkTest.*` 씨앗 1~5 에서 재현 안 됨 — 백로그 1-10.
  대신 씨앗 13 이 다른 순서 의존을 드러냈다(`LiveShaderTest.ManualReloadWithNoTargetsIsNoop` — 앞 케이스가 엔진 셰이더 캐시에 지운 프로브를 남김) — 고쳤고 씨앗 13 · 14 통과.
- **고친 것**: Shipping 빌드 깨짐 셋(EditorTest 의 `_displayName` · `collectTypeChain`, `TestDebugHUD` — 에디터 보강 뒤 Shipping 을 짓지 않았다)과 Shipping 경고 둘
  (`ScreenshotPathUtil.cpp(20)` · `EditorSettingsRegistry` `keyCount`), Shipping AppScenarioTest 의 `bugit` · `debughud` 읽기 오류(11) — `DevCommand` 가 든 시나리오는
  개발 명령이 없는 구성에서 건너뜀(13), 대소문자만 바뀐 개명이 남긴 빌드 폴더 결함 둘(ReflectionParser 가 `Gui` → `GUI` 를 "같은 이름의 다른 헤더" 로 봐 Debug 빌드가 섰다,
  매니페스트 복사가 `GF_ClassicJrpg` → `GF_ClassicJRPG` 를 복사한 뒤 지워 서버 `ModuleCatalogTest` 가 졌다), `GF_Server_ServerDirectory` 의 오브젝트 경로 250 자 경고
  (기준선 `9482adb50`(10-06)부터 — 생성 소스 이름에 타깃 이름이 들어갔다; 표식 · 엔진 도장 둘 다), `AppCookTest.SceneCookReadsTheSourceTreeCleanly`(Map Check 시나리오의
  일부러 틀린 씬이 값 검증 경고를 낸다 — 값 검증 줄은 읽기 실패가 아니다), `AppTest_HostOnly` 시한 초과(에디터 시나리오 한 케이스가 18 분 — 백엔드마다 케이스 · 조각 셋).
- **파일 대화상자**: `WindowsFileDialogHostTest`(EngineTest 호스트 스위트)가 실제 열기 · 저장 대화상자를 띄워 Win32 메시지로 파일 이름을 넣고 확인 · 취소를 누른다 — 통과.
- **못 한 것**: 리눅스(CI 결과로 — 특히 Core 의 Posix 파일과 약어 개명 뒤 처음 컴파일되는 리눅스 전용 파일), 실제 RenderDoc 캡처(이 PC 에 없다), 단언 대화상자 세 단추(모달),
  Tracy 0.14.1 뷰어 연결, 배율 1.5 · 2 에디터(D24), Dialogue Graph 첫 열기(D21), 리눅스 RPATH(TestBin 이 Bin 의 공유 라이브러리를 찾는지).
- 빌드 폴더 함정: 옛 대소문자 이름의 CMake 폴더(`Test/EditorUiTest`)가 남아 있으면 `-Wnonportable-include-path` 경고가 난다 — 그 폴더를 지우고 다시 구성한다.

## 에디터 보강 끝(2026-10-11)

- 계획 문서 `EditorPlus.md` 의 단위를 모두 넣고 계획 문서를 지웠다. 이 세션 마지막 묶음: N8 Animation Graph 전이 편집, N6 플레이 옵션(플레이 중 인스펙터 편집 · Keep Simulation Changes ·
  F8 Eject · Maximize On Play · 새 창 플레이), N9 Sequencer 키 트랙(도우미), 프로파일러(호출 트리 · 프레임 고르기 · Pause on spike · 캡처 저장), 에디터 프레임당 할당(draw 스냅샷 풀 — 프레임당 ~260 KB 0),
  Input Map Editor 탭 셋, 디버그 HUD 에디터 연결(옛 Stats · 오버레이 줄 대체), 렌더 타깃 보기(깊이 선형화 · 채널 · 픽셀 값, 도우미), 에디터 README 를 모듈 README 틀로 다시 쓰기.
  남은 로드맵(프로젝트 설정 창 · 카탈로그 편집기 · 다중 월드 툴 창 …)은 백로그 1-4 "에디터 로드맵" 한 항목이다.
- 에디터 시나리오는 이제 네 백엔드 모두 통과한다(위 "검증 결과").
- 도우미 워크트리는 모두 지웠다(editor-plus 하나만 이 세션의 적용 자리).

## 지금 상태 (2026-10-10 밤 — 이름 정리 기계적 치환 · CMake 구조 정리까지 들어갔다)

- CMake 구조 정리(2026-10-10 밤, 동작 불변): `cmake/Engine/ModuleTargets.cmake` 를 여섯 파일로(`TargetCompileRules` · `DelayLoad` · `EngineAbiStamp` · `ModuleRegistry` · `ModuleTargets` · `ExecutableTargets`),
  `ModuleManifest.cmake` 에서 `ModuleActivation.cmake` 를 가르고 옛 산출물 지우기를 `BuildLayout.cmake` 로, Engine 압축 코덱 찾기를 `sw_findEngineCompressionLibraries`, libclang 찾기를
  `cmake/Environment/FindLibclang.cmake`, 시험 폴더 손 목록을 폴더 훑기 + `EXTRA_SOURCES`, `GenerateConfigConstants.cmake` 의 죽은 변수 · 흩어진 절 정리. 하지 않은 것과 이유는 결정 기록 5-2.
  검증: 네 프리셋(Ninja-Debug · Ninja-Shipping · Ninja-Debug-Server · Ninja-Shipping-Server)의 TU 별 명령 · 링크 엣지 · ninja 타깃 · File API 답이 전과 같다(EditorTest 오브젝트 순서만 다르다).
  `NOMINMAX` · `WIN32_LEAN_AND_MEAN` 을 CMake 정의로 옮긴 커밋은 모든 Windows TU 에 정의 둘이 붙는 것 말고 다른 차이가 없다. 백로그 1-9 ①(OS 헤더를 PCH 에서 빼기)의 선행이 끝났다.
  풀 시험은 돌리지 않았다(사용자 결정 — 에디터 보강 뒤 한 번에) — 아래 7번 "전체 검증" 에 그대로 든다.

- 이름 정리 기계적 치환(2026-10-10 밤): [이름 정리](NamingPass.md) "끝난 것" 절. 두 린트의 "개명 예정" 59 줄을 고치고 표에서 지웠다(`CheckOutParameterNames` 예외 표는 비었다).
  약어 통일이 남긴 문자열 `UiDocument` · `ExpectUi` 도 `UIDocument` · `ExpectUI` 로 고쳤다(UI 시험 셋이 지고 있었다).
  검증: Debug 경고 0, `ctest --preset Ninja-Debug-lint` 62/62, Debug `-L nogpu` 44/44, Shipping 빌드 · `-L hostgpu` 5/5, `Ninja-Debug-Shooter3D` · `Ninja-Debug-NileCity` 빌드,
  네 백엔드 × Empty · 에디터 · Shooter3D · NileCity(종료 코드 0, `[Error]` 0), `App --cook-shaders` [Error] 0.
  씬 뷰 시나리오 셋(`sceneviewgameview` · `viewportgrid` · `viewportpick`)이 지던 것은 커밋 회귀가 아니었다 — 저장된 레이아웃이 없는 워크트리에서 기본 배치의 앞 탭이
  Prefab Editor 였다(V1 부터 같다). `EditorDockLayout::updateDefaultTabSelection` 과 시나리오 `defaultlayout` 으로 고쳤다(Editor README 함정 절).
  Shipping 빌드 경고 하나도 기준선부터 있다: `ScreenshotPathUtil.cpp(20)` unused member function `runScreenshot`.

- Scripts 정리(2026-10-10, 파이썬만 — 빌드 없음): 게이트 공용 글 읽기를 `Scripts/common/CodeText.py` 로(include 줄 · 층 폴더 · `IncludeResolver` ·
  `lineOf` · `blankMatch`), `CheckCodeConventions` 를 `Scripts/lint/conventions/` 일곱 모듈로, 죽은 정의 일곱 개 삭제. 린트 묶음 117.7 → 67.1 s,
  커밋 훅 21.1 → 9.5 s(staged 1). 게이트 · 셀프테스트 65 개의 트리 전체 출력이 전과 같다. 남은 후보는 백로그 1-9 "Scripts 정리의 남은 후보".

- main 에 들어간 것: 3 차(가상 입력 · 자동화 시나리오 · 빙의 · 탑승), 4 차(런타임 UI 전부 + 원시 장치 조회 → 입력 맵 액션), 5 차 a(문서 분리 · 다시 쓰기 · 작업 방식),
  5 차 b 전부와 그 뒤 추가분(의존성 기준선 · 옵션 창 · 에디터 고정 픽셀 배율 · Dev 산출물 자리 `Bin/Modules` · `Bin/Symbols` · `TestBin` · 크래시 보고 인자 · 반복문 중괄호 · 중복 정리).
- 6 차(주석을 새 문체로)는 시작하지 않았습니다.
- 에디터 보강 V1(씬 뷰 / 게임 뷰 분리, 2026-10-10): 엔진 다중 뷰에 호스트 타깃 뷰 · EditorAPI `getSceneViewport` · 모듈 ABI v3(`6651ef893`),
  Scene · Game 패널 분리 · 상단 툴바 `EditorPlayToolbar` · 이름표 `sceneView.*` / `gameView.*` / `toolbar.*` · 시나리오 `sceneviewgameview`(`8fb82d7ee`), 문서(이 커밋).
  검증: Debug 경고 0, `ctest --preset Ninja-Debug-lint` 59/59, Debug `-L nogpu` 41/42(SmokeTest 는 아래), 에디터 자체 시험 37/37,
  `AppScenarioTest.EditorScenariosPassOnEveryBackend`(10 시나리오 × 네 백엔드, 건너뜀 0) · `AppSmokeTest.Editor*` · Vulkan 씬 뷰 리사이즈 통과,
  Shipping · Debug-Server 빌드 경고 0, 창 캡처로 Scene 탭 격자 · 기즈모 · Game 탭 보조선 없음 확인.
  **남은 것**: 보기 모드가 렌더러 전역이라 두 뷰가 함께 보이면 게임 뷰에도 걸린다(보기 모드는 뷰마다로 고쳤다 — Editor README), 두 뷰가 함께 보일 때 그림자 볼륨은 게임 카메라 기준.
- 그 뒤 묶음(2026-10-10): AppHost → ModuleHost 개명 · FixedTimestep 을 `Engine/Config` 로(`6863b1f95`), 에디터 격자 월드 고정 · 적응 간격 · 가장자리 흐림(`0c3f1fd37`),
  텍스처 BC 띠 병렬 압축(`e71f3b8ae`, 모듈 ABI v2 — `importEditorAssets` 가 서비스 표를 받는다), 런타임 UI 그리기 목록 맞바꾸기(`53898be30`), 서드파티 DLL 은 `Bin` 한 벌(`13b41e74a`).
  검증(묶음 끝에 한 번): Debug 경고 0, `ctest --preset Ninja-Debug-lint` 59/59, Debug `-L nogpu` 42/42 · `-L hostgpu`(에디터 시나리오 4 백엔드 — `viewportgrid` 포함),
  Shipping 빌드 · `-L hostgpu`, Debug-Server 빌드, App 에디터(DX12) · Vulkan 기동 · 핫 리로드(`-gv_reloadGameAtFrame`) `[Error]` 0, Debug `--import-textures` 경고 한 줄 · `--check-textures` 0 문제.
- 텍스처 임포트는 Release App 으로 돌린다(Debug BC7 은 12 분을 넘긴다).

## 다음 세션이 할 일 (2026-10-10 저녁 기준, 순서대로)

구조 이동(Core 층 · GameFramework · Engine 폴더 · DevTools 해체 · Network 이동 · ModuleHost 분리)과 도구(약어 · 이름 린트 · 빌드 속도 측정)는 끝났다. 아래는 **코드가 대량으로 바뀌는 단계**라 한 세션이 한 줄기로 잇는다.
작업 방식은 [작업 흐름](../11_Workflow.md). 이 세션에서 쓴 규칙: 도우미 여럿은 워크트리(`py -3 -m Scripts worktree-make <이름>`)에서만 일하고 push 하지 않으며, 적용 담당 하나가 `git merge --ff-only wt/<이름>` 으로 `main` 에 넣고 push 한다.
도우미가 동시에 빌드할 때는 `D:\Projects\Personal\LT-wt\BUILD-LOCK-README.txt` 의 슬롯 잠금(`.slot1~3`)을 쓴다. 사용 한도에 두 번 걸렸으니 도우미는 셋 안팎으로 둔다. **검증은 마지막에 한 번**(사용자 결정), 큰 이동 사이에는 Debug 풀 컴파일 + 린트 게이트만. WSL(리눅스)은 CI 로 본다.

전체 검증(옛 7 번)은 2026-10-11 에 했다(맨 위 "검증 결과"). 리눅스는 CI 결과로 본다(`IOUring` · `Epoll` · `LinuxAsyncFileIOBackend` · `PosixThreadCrashStack` 등은 개명 뒤 한 번도 컴파일되지 않았다).

1. **이름 정리 — 남은 것.** `MathUtil::getRandom` 뿐(3번 "결정적 난수를 Core 로" 와 함께). 나머지는 2026-10-11 에 끝났다.
2. **빌드 속도 2 ~ 6 단계.** 계획 [빌드 속도](BuildSpeed.md). 0 단계 기준선(Debug, Ryzen 7 6800H, 3회 중앙값: 풀 131.5 s · 헤더 하나 수정 131.6 s · `.cpp` 하나 7.2 s · 워크트리 콜드 142.9 s)과 상위 헤더 20개(헤더 파싱 비율 82 %, 1위 `Engine/pch.h`, 2위 `CoreMinimal.h`)는 `docs/08_Verification.md` 빌드 속도 절과 `BuildSpeed.md` 에 있다. **헤더 하나를 고치면 풀 빌드와 같은 시간이 든다**(Core 기본 헤더 — 헤더 다이어트의 근거). Release 는 풀 한 번(192 s)만 재었다. Jolt 래퍼 TU 는 자기 PCH 를 가진 OBJECT 라이브러리로 11.1 → 5.5 s 로 줄었다(6 단계 끝). 헤더 다이어트 → PCH 공유(타깃마다 86개) → 개발 증분(PCH)과 콜드(PCH 없이 유니티 + sccache) 구성 분리 → 유니티 확대. 단계마다 같은 표로 전후를 남기고, 효과 없으면 되돌려 `docs/09_Decisions.md` 3절에 숫자와 함께 적는다. 7 ~ 9 는 필요할 때만(사용자 결정).
3. **행동이 바뀔 수 있는 중복 정리.** 결정적 난수를 Core 로(xorshift32 사본 7곳 · splitmix64 3곳; 상태 바이트 호환을 시험으로 증명), 격자 사본 13곳 → `GridTopology`(Engine · Editor 용은 Core 나 Engine 쪽으로 올리는 선결 과제), 타이머 약 25곳 → `Countdown`, 실시간 키트 HP 직접 관리 → `Vitality`. 상태 바이트와 게임 동작에 닿으므로 시험과 함께.
4. **엔진 분할 1단계 이후.** 계획 [엔진 분할](EnginePartition.md): 작은 고리(`Automation ↔ UI`, `Text` 티어 불일치), `Object` 코어와 기능 컴포넌트 가르기(`GameObject/` 코어가 `Graphics` 15 · `Physics` 10 · `Navigation` 5 · `Audio` 2 를 include), `OBJECT` 라이브러리 분할 → (빌드 속도 측정으로 이득이 확인되면) DLL 승격, 다중 월드. 에디터는 `GameFramework` 를, 서버 실행 파일도 `GameFramework` 를 include 할 수 없다 — 그래서 에디터 · 서버가 쓰는 폴더는 Engine 에 남는다.
5. **6 차(주석을 새 문체로)** 와 끝난 계획 문서 정리(`EnginePartition.md` · `GameFrameworkLayout.md` · `BuildSpeed.md` 에서 끝난 단위 삭제, 교훈은 영역 README 함정 절 · 결정 기록으로).

### 알아 둘 함정 (이 세션에서 겪은 것)

- **vcpkg 가 갑자기 포트 전체를 다시 지으면**(20분 넘게): `VCPKG_BINARY_SOURCES` 가 `cmake --preset` 환경에만 있어 ninja 재구성에서 빠졌었다(`d649e08f4` 가 `Vcpkg.cmake` 에서 정한다). 그래도 재발하면 `Tools/vcpkg/packages/*/share/*/vcpkg_abi_info.txt` 의 `triplet_abi` 가 흔들리는지 본다 — 한 번은 두 vcpkg 가 서로 다른 환경(OpenMP 를 찾는 쪽)에서 `3922d84…` 와 `4299d71…` 를 번갈아 설치했다(원인 미확정). 설치 도중에는 건드리지 않는다.
- **`main` 체크아웃에서는 빌드 · 편집하지 않는다**(도우미가 워크트리에서 일한다). 시험 실행 파일은 `AllTests` 를 지어야 최신이다(옛 exe 로 돌리면 모듈 ABI 불일치로 진다 — 지금 ABI v4).
- **임시 파일**은 scratchpad 에서 에이전트 이름을 앞에 붙인다(서로 덮어쓴 적이 있다).
- **측정**은 다른 빌드가 도는 동안 오염된다 — 슬롯 세 개를 모두 잡고 한다. 측정 PC 이름(CPU)을 표에 적는다(문서에 i5-8500 이라 적힌 옛 수치가 있었다 — 이 PC 는 Ryzen 7 6800H).
- 사용 한도(`rate_limit`)로 도우미가 끊기면 워크트리 작업은 그대로 남는다 — 한도가 풀린 뒤 `SendMessage` 로 이어서 시킨다.

### 에디터 작은 단위(E5 · G2 · G3 · H1)의 남은 것

시나리오 넷(`windowtitle` · `screenshotbutton` · `renderdocbutton` · `assertdialog`)과 `AppScenarioTest` 전체는 2026-10-11 전체 검증에서 네 백엔드로 통과했다.
남은 것: 리눅스 X11 · dlopen 갈래(CI), 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다), 단언 대화상자 세 단추(모달), 스크린샷 토스트 "Show in Explorer" 단추(알림 관리자에 동작 단추가 없다),
`screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로 확인.
RenderDoc 은 기동 단계를 따로 두지 않고 RHI 단계 맨 앞에서 올린다. `ThirdParty/renderdoc` 에 원문 헤더를 넣었고 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

### 이 세션에 병합된 것 (되돌아볼 때)

V1 씬 뷰 / 게임 뷰(ABI v3), 빌드 속도 1단계(시험이 기본 빌드에서 빠짐 — `AllTests`), GameFramework 폴더 재배치, DevTools 해체 · Network 이동(ABI v4), Core 층 정리(순환 0 · `CheckCoreLayers`), Engine 폴더 재배치 1~7, 중복 정리(`NameRegistry` · `UITextAssetCache` · `XMLCatalog` · `EditorModuleHost`), 이름 린트(`build`/`generate`/`construct` 금지 · `CheckOutParameterNames`), 약어 도구, 에디터 소단위, `Jolt` PCH, vcpkg 바이너리 캐시 수정.
계획 문서 중 끝난 단위를 아직 안 지운 곳이 있다(5 번에서 정리한다).

## 검증할 것

- 리눅스 RPATH: CI 결과로 확인(사용자 결정 2026-10-10 — 로컬 WSL 빌드는 하지 않는다). WSL 클론은 이번에 `Tools/vcpkg` 를 기준선 `0699a19d` 로 옮기고 다시 부트스트랩했다(포트 설치는 중단).
