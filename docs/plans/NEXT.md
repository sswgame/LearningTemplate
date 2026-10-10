# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다.

## 지금 상태 (2026-10-10 저녁 — 5 차 b 검증은 끝났고, 그 뒤 구조 정리 묶음은 컴파일과 린트만 봤다)

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
  **남은 것**: 보기 모드가 렌더러 전역이라 두 뷰가 함께 보이면 게임 뷰에도 걸린다(EditorPlus G1 에 적었다), 두 뷰가 함께 보일 때 그림자 볼륨은 게임 카메라 기준.
  **검증 대기**: SmokeTest(`ArchitectureTest.ModuleCompilerAndLiveReloadE2E`)는 같은 시각 빌드 속도 작업(`d649e08f4`)이 공유 vcpkg 를 다시 설치하는 동안
  Debug 재구성이 vcpkg 잠금 · 없는 zstd 패키지로 실패해 확인하지 못했다(V1 코드와 무관한 환경 문제) — vcpkg 설치가 끝난 뒤 `ctest -R ^SmokeTest$` 로 다시 본다.
- 그 뒤 묶음(2026-10-10): AppHost → ModuleHost 개명 · FixedTimestep 을 `Engine/Config` 로(`6863b1f95`), 에디터 격자 월드 고정 · 적응 간격 · 가장자리 흐림(`0c3f1fd37`),
  텍스처 BC 띠 병렬 압축(`e71f3b8ae`, 모듈 ABI v2 — `importEditorAssets` 가 서비스 표를 받는다), 런타임 UI 그리기 목록 맞바꾸기(`53898be30`), 서드파티 DLL 은 `Bin` 한 벌(`13b41e74a`).
  검증(묶음 끝에 한 번): Debug 경고 0, `ctest --preset Ninja-Debug-lint` 59/59, Debug `-L nogpu` 42/42 · `-L hostgpu`(에디터 시나리오 4 백엔드 — `viewportgrid` 포함),
  Shipping 빌드 · `-L hostgpu`, Debug-Server 빌드, App 에디터(DX12) · Vulkan 기동 · 핫 리로드(`-gv_reloadGameAtFrame`) `[Error]` 0, Debug `--import-textures` 경고 한 줄 · `--check-textures` 0 문제.
  **검증 대기**: 게임별 프리셋(`Ninja-Debug-<Game>`) 빌드, 네 백엔드 × Shooter3D · NileCity 실행, Debug-Server `-L nogpu`, 리눅스(CI 결과로 — 이번 묶음은 Windows 전용 CMake 경로와 ABI 판을 바꿨다).

## 5 차 b 검증 결과(2026-10-10)

- 통과: `ctest --preset Ninja-Debug-lint` 59/59(CheckLoopBraces 포함), Debug `-L nogpu` 42/42 · `-L hostgpu`, Shipping 빌드 경고 0 · `-L nogpu` · `-L hostgpu`,
  App 네 백엔드 × Empty · 에디터 · Shooter3D · NileCity(종료 코드 0, `[Error]` 0, Empty 스크린샷은 네 백엔드 sha 같음, RHI DLL 은 `Bin/Modules` 에서 뜬다),
  핫 리로드(`-gv_reloadGameAtFrame` — 섀도 사본이 `Bin/Modules` 에 생기고 지워진다), SmokeTest, `-crash-reporter` 를 Resource 없는 폴더에서(종료 코드 0, 로그 한 줄).
- 검증 중 고친 것(커밋 메시지에 경위): 버린 결과 이유 넷(`14a50494c`), 없는 머티리얼이 체커로 안 가던 결함(`f6ad913fd`), 지워진 readme.meta 를 보던 시험(`b0e908296`),
  옵션 골든(`443b365a9`), 크래시 보고 종료 단언 · 누수 기준선 · Shipping 경고(`d26b832e9`), 배포본 글 수집이 UI 글을 지우던 결함 · Shipping 전용 시험 둘(`15949bce3`),
  텍스처 스탬프 다섯 장(`f54bf981d`), Reset Default Layout 이 아무 일도 안 하던 결함 · `layout.reset`(`db8a8df3a`).
- UI 그리기 Release 측정: `GT.UI.Paint` p50 0.29~0.36 ms · Layout 0.06 ms(목표 합 0.3 ms 에 조금 못 미침, 백로그 1-6).
- 판정: D4 통과(시나리오), D26 은 간헐(세 번 중 두 번 짐 — 백로그 1-4 에 새 단서), D21 · D24 는 실행으로 보지 못했다(백로그 1-4 그대로).
- 보지 못한 것: Tracy 0.14.1 뷰어 연결, 배율 1.5 · 2 에디터(D24), Dialogue Graph 첫 열기(D21). 텍스처 임포트는 Debug(BC7) 로 12 분을 넘겨 Release 로 했다 — 임포트는 Release App 으로 돌린다.
- 리눅스 RPATH(TestBin 시험이 Bin 의 공유 라이브러리를 찾는지)는 로컬 WSL 로 보지 않았다 — **CI 결과로 확인**한다(리눅스 잡의 시험 단계).

## 다음 세션이 할 일 (2026-10-10 저녁 기준, 순서대로)

구조 이동(Core 층 · GameFramework · Engine 폴더 · DevTools 해체 · Network 이동 · ModuleHost 분리)과 도구(약어 · 이름 린트 · 빌드 속도 측정)는 끝났다. 아래는 **코드가 대량으로 바뀌는 단계**라 한 세션이 한 줄기로 잇는다.
작업 방식은 [작업 흐름](../11_Workflow.md). 이 세션에서 쓴 규칙: 도우미 여럿은 워크트리(`py -3 -m Scripts worktree-make <이름>`)에서만 일하고 push 하지 않으며, 적용 담당 하나가 `git merge --ff-only wt/<이름>` 으로 `main` 에 넣고 push 한다.
도우미가 동시에 빌드할 때는 `D:\Projects\Personal\LT-wt\BUILD-LOCK-README.txt` 의 슬롯 잠금(`.slot1~3`)을 쓴다. 사용 한도에 두 번 걸렸으니 도우미는 셋 안팎으로 둔다. **검증은 마지막에 한 번**(사용자 결정), 큰 이동 사이에는 Debug 풀 컴파일 + 린트 게이트만. WSL(리눅스)은 CI 로 본다.

1. **약어 철자 통일 — 끝났다(2026-10-10).** 등록부의 약어 32개를 모두 대문자로 바꾸고 강제한다(`kEnforced` = `kAcronym`, 규칙은 AGENTS.md "Function names").
   검증은 Debug 풀 빌드(all + AllTests) 경고 0 · `App --cook-shaders` [Error] 0 · `ctest --preset Ninja-Debug-lint` 62/62 까지다.
   **검증 대기(아직 돌리지 않았다)**: Debug `-L nogpu` · `-L hostgpu`, Shipping 빌드 · `-L hostgpu`, 게임별 프리셋(`Ninja-Debug-<Game>`), 서버 프리셋 `-L nogpu`,
   네 백엔드 × 대표 셋 실행, 리눅스(CI — `IOUring` · `Epoll` · `LinuxAsyncFileIOBackend` 등 리눅스 전용 파일은 이름이 바뀐 뒤 한 번도 컴파일되지 않았다).
   바뀐 것 가운데 실행으로 볼 것: 모듈 이름(`GF_TacticsSRPG` · `GF_SQLStore` · `GF_Server_SQLStore` · `GF_NetMMO` · `GF_WitcherRPG` · `GF_ClassicJRPG`),
   모듈 내보내기 심볼(`exportGameAPI` · `exportEditorAPI`) · 엔진 서비스(`getAsyncFileIO`) — 모든 모듈을 같이 다시 지어야 한다, 시험 실행 파일 `EditorUITest`,
   데이터에 든 이름(UI 문서 루트 `UIDocument` · 레이어 `HUD` · 리그 노드 `TwoBoneIK` · 프리팹 이름 · 리플렉션 키 `_meshID` 등), 셰이더 이름 `g_*VisibleInstanceIDs`.
2. **이름 정리 — 기계적 치환.** 계획 [이름 정리](NamingPass.md)(린트와 어휘표는 들어갔다). 예외 표 66줄(`CheckFunctionVocabulary` · `CheckOutParameterNames`)의 "개명 예정"을 줄여 간다: `build*` 38종, `MatrixMath::create*` 21개 → `make*`, `ctx` → `context`(257곳), bare `out`(19), `Attr` → `Attribute`, 부정형 불린 → 긍정형(데이터 재작성), `_time` 스윕 비율 → `_hitFraction`, 부작용 숨은 함수 개명, `AssetManager::getMaterialManager`/`getTextureManager`. 큰 이름: `ResourceUtil` → `ResourcePaths` + `ResourceIO`(313), `EditorUtil` 경로 → `EditorPaths`. 약어 통일은 끝났으니 바로 할 수 있다.
3. **빌드 속도 2 ~ 6 단계.** 계획 [빌드 속도](BuildSpeed.md). 0 단계 기준선(Debug, Ryzen 7 6800H, 3회 중앙값: 풀 131.5 s · 헤더 하나 수정 131.6 s · `.cpp` 하나 7.2 s · 워크트리 콜드 142.9 s)과 상위 헤더 20개(헤더 파싱 비율 82 %, 1위 `Engine/pch.h`, 2위 `CoreMinimal.h`)는 `docs/08_Verification.md` 빌드 속도 절과 `BuildSpeed.md` 에 있다. **헤더 하나를 고치면 풀 빌드와 같은 시간이 든다**(Core 기본 헤더 — 헤더 다이어트의 근거). Release 는 풀 한 번(192 s)만 재었다. Jolt 래퍼 TU 는 자기 PCH 를 가진 OBJECT 라이브러리로 11.1 → 5.5 s 로 줄었다(6 단계 끝). 헤더 다이어트 → PCH 공유(타깃마다 86개) → 개발 증분(PCH)과 콜드(PCH 없이 유니티 + sccache) 구성 분리 → 유니티 확대. 단계마다 같은 표로 전후를 남기고, 효과 없으면 되돌려 `docs/09_Decisions.md` 3절에 숫자와 함께 적는다. 7 ~ 9 는 필요할 때만(사용자 결정).
4. **행동이 바뀔 수 있는 중복 정리.** 결정적 난수를 Core 로(xorshift32 사본 7곳 · splitmix64 3곳; 상태 바이트 호환을 시험으로 증명), 격자 사본 13곳 → `GridTopology`(Engine · Editor 용은 Core 나 Engine 쪽으로 올리는 선결 과제), 타이머 약 25곳 → `Countdown`, 실시간 키트 HP 직접 관리 → `Vitality`. 상태 바이트와 게임 동작에 닿으므로 시험과 함께.
5. **엔진 분할 1단계 이후.** 계획 [엔진 분할](EnginePartition.md): 작은 고리(`Automation ↔ UI`, `Text` 티어 불일치), `Object` 코어와 기능 컴포넌트 가르기(`GameObject/` 코어가 `Graphics` 15 · `Physics` 10 · `Navigation` 5 · `Audio` 2 를 include), `OBJECT` 라이브러리 분할 → (빌드 속도 측정으로 이득이 확인되면) DLL 승격, 다중 월드. 에디터는 `GameFramework` 를, 서버 실행 파일도 `GameFramework` 를 include 할 수 없다 — 그래서 에디터 · 서버가 쓰는 폴더는 Engine 에 남는다.
6. **에디터 보강** — [에디터 보강 계획](EditorPlus.md). V1 · E5 · G2 · G3 · H1 은 끝났다. 남은 단위(C1~C5, P1~P4, I1~I3, A1, G1 보기 모드 분리, G4, H2 · H3, T1~T4, 아이콘 R3 · R4 · R5 · R9, 패널 점검 N1~N12)를 선행 칸 순서로. 끝나면 에디터 문서를 새 문체로 다시 쓴다.
7. **전체 검증 한 번(마지막).** `ctest --preset Ninja-Debug-lint`, 시험 실행 파일을 먼저 짓고(`AllTests` 타깃 — 기본 빌드는 시험을 안 짓는다) Debug `-L nogpu`(V1 에서 41/42, **실패 한 건의 이름을 모른다** — 먼저 찾는다) · `SmokeTest`, Shipping 빌드 · `-L nogpu` · `-L hostgpu`, 대표 실행(네 백엔드 × Empty · 에디터 · Shooter3D · NileCity), 서버 프리셋 `-L nogpu`, 게임별 프리셋(`Ninja-Debug-<Game>`) 빌드. 리눅스는 CI 결과(특히 Core 의 새 Posix 파일 `PosixThreadCrashStack` 등은 한 번도 컴파일되지 않았다).
8. **6 차(주석을 새 문체로)** 와 끝난 계획 문서 정리(`EnginePartition.md` · `GameFrameworkLayout.md` · `BuildSpeed.md` 에서 끝난 단위 삭제, 교훈은 영역 README 함정 절 · 결정 기록으로).

### 알아 둘 함정 (이 세션에서 겪은 것)

- **vcpkg 가 갑자기 포트 전체를 다시 지으면**(20분 넘게): `VCPKG_BINARY_SOURCES` 가 `cmake --preset` 환경에만 있어 ninja 재구성에서 빠졌었다(`d649e08f4` 가 `Vcpkg.cmake` 에서 정한다). 그래도 재발하면 `Tools/vcpkg/packages/*/share/*/vcpkg_abi_info.txt` 의 `triplet_abi` 가 흔들리는지 본다 — 한 번은 두 vcpkg 가 서로 다른 환경(OpenMP 를 찾는 쪽)에서 `3922d84…` 와 `4299d71…` 를 번갈아 설치했다(원인 미확정). 설치 도중에는 건드리지 않는다.
- **`main` 체크아웃에서는 빌드 · 편집하지 않는다**(도우미가 워크트리에서 일한다). 시험 실행 파일은 `AllTests` 를 지어야 최신이다(옛 exe 로 돌리면 모듈 ABI 불일치로 진다 — 지금 ABI v4).
- **임시 파일**은 scratchpad 에서 에이전트 이름을 앞에 붙인다(서로 덮어쓴 적이 있다).
- **측정**은 다른 빌드가 도는 동안 오염된다 — 슬롯 세 개를 모두 잡고 한다. 측정 PC 이름(CPU)을 표에 적는다(문서에 i5-8500 이라 적힌 옛 수치가 있었다 — 이 PC 는 Ryzen 7 6800H).
- 사용 한도(`rate_limit`)로 도우미가 끊기면 워크트리 작업은 그대로 남는다 — 한도가 풀린 뒤 `SendMessage` 로 이어서 시킨다.

### 에디터 작은 단위(E5 · G2 · G3 · H1)가 남긴 검증 대기

`main` 에 있다(`0a4376257` ~ `6f748f3ba`). 단위마다 Debug 경고 0, 새 시험은 통과(`EditorWindowTitleTest` 4 · `ScreenshotPathUtilTest` 2 · `RenderPassGPUTest.ScreenshotDumpWritesPngAndPpm` · `RenderDocCaptureTest` 1 · `AssertDialogTest` 4). 새 시나리오 `windowtitle` · `screenshotbutton` · `renderdocbutton` · `assertdialog` 는 폴더 이동 전 바이너리로 DX12 단독 PASS.
**전체 검증 때 돌릴 것**: rebase 뒤 전체 빌드, 새 시나리오 넷 재실행, 기존 `sceneviewgameview` · `workflow` 회귀, `AppScenarioTest` 전체(네 백엔드), `AppTest`(`-unattended` 추가), 리눅스 X11 · dlopen 갈래, 실제 RenderDoc 캡처(이 PC 에 RenderDoc 이 없다), 단언 대화상자 세 단추(모달이라 손으로만).
남은 작은 일: 스크린샷 토스트 "Show in Explorer" 단추(알림 관리자에 동작 단추가 없다), `screenshotbutton` 시나리오가 `Bin/Saved/Screenshots` 에 남기는 PNG, Engine README 개발 명령 줄의 경로 확인, 시나리오가 늘어 `AppTest` 의 `HOST_SHARDS` 조정 검토.
RenderDoc 은 기동 단계를 따로 두지 않고 RHI 단계 맨 앞에서 올린다. `ThirdParty/renderdoc` 에 원문 헤더를 넣었고 그 폴더의 `.clang-format` 은 `DisableFormat` 이다.

### 이 세션에 병합된 것 (되돌아볼 때)

V1 씬 뷰 / 게임 뷰(ABI v3), 빌드 속도 1단계(시험이 기본 빌드에서 빠짐 — `AllTests`), GameFramework 폴더 재배치, DevTools 해체 · Network 이동(ABI v4), Core 층 정리(순환 0 · `CheckCoreLayers`), Engine 폴더 재배치 1~7, 중복 정리(`NameRegistry` · `UITextAssetCache` · `XMLCatalog` · `EditorModuleHost`), 이름 린트(`build`/`generate`/`construct` 금지 · `CheckOutParameterNames`), 약어 도구, 에디터 소단위, `Jolt` PCH, vcpkg 바이너리 캐시 수정.
계획 문서 중 끝난 단위를 아직 안 지운 곳이 있다(8번에서 정리한다).

## 검증할 것

- 리눅스 RPATH: CI 결과로 확인(사용자 결정 2026-10-10 — 로컬 WSL 빌드는 하지 않는다). WSL 클론은 이번에 `Tools/vcpkg` 를 기준선 `0699a19d` 로 옮기고 다시 부트스트랩했다(포트 설치는 중단).
