# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(한 일은 `git log`, 교훈은 영역 README · 결정 기록 · 검증 문서에 있다).
작업 방식은 [작업 방식](../11_Workflow.md), 남은 일 전체는 [백로그](../06_Backlog.md)입니다.

## 지금 상태 (2026-10-12 세션 끝)

- **SOLID 계획 단위 S1 ~ S22 가 모두 끝났다**([StructureRefactor](StructureRefactor.md)). 옮기지 않기로 한 것(S3 · S8 · S5 의 실행 함수 칸 등)은 그 문서에 이유와 함께 있다. 남은 후보는 "`ProfilerPanel` 을 탭 상태 객체로 나누기" 하나.
- 이 세션의 병합 규칙(사용자 지시): **구현이 끝나면 바로 `main` 에 병합 · push, 빌드는 하되 `ctest` · 측정은 사용자가 따로 한다.** 도우미는 코드만 고치고(빌드 금지) 적용 담당이 `D:\w\integ2` 에서 병합 · 빌드한다.
- **빌드 상태**: 마지막 병합(S13 ~ S17 + 앞서 빌드 없이 올린 S21 · S22)을 `integ2` 에서 Debug 로 지었다 — 경고 0 · 링크 오류 0. Shipping 도 같은 병합을 지었고 경고 0 · 링크 통과.
- **테스트는 S18 ~ S22 와 S13 ~ S17 에 대해 돌리지 않았다**(S18 ~ S20 은 Debug `-L lint` 63/63 · `-L nogpu` 49/49 · `-L hostgpu` 7/7 까지 통과). 새 단위 테스트가 약 30 건 더해졌고 한 번도 실행되지 않았다.

## 다음 할 일 (순서대로)

0. **검증과 측정(사용자 몫으로 맡겨 둔 것)** — 아래 "검증할 것".
1. **CI 확인.** 확인할 테스트 목록은 백로그 1-10 "다음 CI 실행으로 확인할 것". push 뒤 결과를 기다리지 않는다([작업 방식](../11_Workflow.md) 4절).
2. **R4 남은 파일 · R7(체인 후속)** — [StructureRefactor](StructureRefactor.md). 파일마다 한 단위, 서로 충돌하지 않는다.
3. **빌드 속도 5 · 6 단계** — [BuildSpeed](BuildSpeed.md). S13 로 `EngineLoop.cpp` include 가 17 개 줄었으니 `-ftime-trace` 전후도 같이.
4. **엔진 분할** — [EnginePartition](EnginePartition.md) 0 단계(기준 측정)와 2 ~ 5 단계.
5. **서버 디버깅** — [ServerDebug](ServerDebug.md), 다음은 S4(회선 조절).
6. **6 차 주석 작업의 남은 구역** — 백로그 1-9.
7. **SOLID 후속 메모**(각 항목은 StructureRefactor 에 근거가 있다): `UIAutomationSteps::ensureLinked` 호출(`UISystem::initialize`)은 Engine 통째 링크라 불필요해 보인다, `Joint2DComponent` 는 Cone 을 고르면 `createJoint` 를 프레임마다 다시 시도해 오류 로그를 반복한다,
   S10 범위 밖의 `getBoundEngineServices()` 직접 호출(`FacialAnimationComponent` · `WidgetComponent` · `UISystem` · `FontSystem`, `SceneCooker` · `SceneNavigationCooker` · `PortraitRenderer` 의 인자 없는 `Scene` 생성).

## 검증할 것

- **Shipping**: 이 세션 끝에 `integ2` 에서 `Ninja-Shipping` 전체(`all AllTests`)를 지었다 — 경고 0 · 오류 0 · 링크 통과. 안 돌린 것: Shipping `-L nogpu` · `-L hostgpu`.
- **Debug `ctest`**: `-L lint`, `-L nogpu`, `-L hostgpu`. 새 테스트 가운데 **기대 숫자가 틀릴 수 있는 것**: `TestFractureAnchor`(4 × 3 조각 벽이 12 개로 나오고 허용 1.5 m 에서 8 개, 배율 3 에서 4 개라고 가정), `TestFractureNetworkSync`(스냅샷 끝 5 바이트를 자르면 자세가 잘린다고 가정),
  `TestUserSettingsFile`(`dump(4)` 가 콜론 뒤 공백 하나), `TestEditorAssetFileCommands` 의 임포트 테스트(임시 폴더가 리소스 루트 밖이라는 가정), `UIInputRouterTest` 포커스 복원(레이아웃 없이 포커스를 받는다는 가정), `TestFrameRendererParts`(`FrameProfiler::registerScope` 가 엔진 서비스 없이 도는지).
- **S21 · S22 에서 먼저 볼 곳**: SmokeTest 소스 목록에 `EditorModuleHostHeadless.cpp` 가 없는 점, `NavMesh*` 테스트(`NavMeshDynamicTest` · `NavMeshAgentTest` · `NavMeshCookTest` · `NavMeshRebakeQueueTest`), `AppHeadlessTasksTest`, `OpenGLRHICommandList.cpp`(compile_commands 에 없어 문법 검사도 못 했다), Linux(GLX 컨텍스트 · 다중 상속 경고 · `SceneNavigation` 의 `NavDebugView` 값 멤버가 내보내지 않은 타입).
- **실기동**: 에디터 켜고(`-EnableEditor`) Present 훅 · 씬 뷰 조작(S18) · 콘텐츠 브라우저(S19) · 모듈 리로드(S20), `assertdialog.scenario.xml`, `-gv_reloadGameAtFrame`, `-gv_standardInputCommands`, 게임 프리셋(`Ninja-Debug-<Game>`) 여덟 · 네 백엔드 대표 실행 — 지연 로드 제거(6 차)와 S10 · S13 · S14 가 모듈 로드 · 씬 기동 경로를 바꿨다.
- **측정(Release, `-gv_profileFrames=600` · 3 회, VSync 에 안 붙었는지 먼저, 셰이더는 `App.exe --cook-shaders` 로 다시 구운 뒤)**: `GT.GPUScene.build`(S17) · `GT.Editor.updateUI`(S12) · `EngineLoop.tick` · `UISystem` · `FrameRenderer` 구간. S13 은 `tick` 이 `engine::getDebugDrawQueue()` 를 매 프레임 부르게 됐다.
  측정이 나쁘면 그 단위를 되돌리거나 훅을 합친다(결정 기록 2절에 숫자와 함께 적을 것).
- 다른 PC 에서 vcpkg 포트 `tracy`(port-version 2)를 받으면 `cmake --preset` 이 Tracy 를 다시 빌드한다.

## 알아 둘 함정 (이 세션에서)

- 도우미가 낮은 우선순위로 `clang-cl -Zs` 만 돌렸고 PCH · 링크 · 코드젠(`.gen.cpp`)은 확인하지 못한다 — 새 `SW_API` 클래스가 여럿이라 링크 오류가 가장 먼저 나올 곳이다. 빌드는 `cmake --preset` 으로 다시 구성한 뒤(새 소스 파일) 한다.
- 도우미가 `Co-Authored-By` 줄을 시스템 리마인더대로 `Opus 5.5` 로 쓰는 일이 있었다 — 병합 전에 `git log main..wt/<이름> --format=%b | grep -i co-auth` 로 보고 `filter-branch --msg-filter` 로 `Sonnet 5.5` 로 고쳤다(저장소 규칙).
- `ninja` 는 `-k 0` 으로 지으면 오류가 나도 끝까지 가서 오류를 모아 준다. ctest 프리셋 이름은 `Ninja-Debug-lint` · `Ninja-Debug-nogpu` 이고 `hostgpu` 프리셋은 없어 `ctest --test-dir build/Ninja-Debug -L hostgpu -j 1`(약 16 분) 으로 돌린다.
- 병합용 워크트리 `D:\w\integ2`(브랜치 `wt/integ2`)는 Debug · Shipping 빌드 폴더를 들고 있어 남겨 뒀다. 다음 병합은 `git merge --ff-only main` 으로 맞춘 뒤 시작하면 증분 빌드가 된다.
- 스크립트는 heredoc 대신 Write 도구로(AGENTS.md "Editing traps"). 임시 파일은 scratchpad 에서 에이전트 이름을 앞에 붙인다(도우미끼리 `syntax_check.py` 를 덮어썼다).
- 작업 워크트리 이름은 같은 영역 단위를 한 사람이 맡도록 `s<글자>` 로 지었다. 끝나면 `py -3 -m Scripts worktree-remove <이름>`.
