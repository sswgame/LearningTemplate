# 다음 세션 인계

새 세션은 이 문서를 먼저 읽습니다. 배치 경계마다 적용 담당이 고쳐 씁니다. 끝난 항목은 지웁니다(한 일은 `git log`, 교훈은 영역 README · 결정 기록 · 검증 문서에 있다).
작업 방식은 [작업 방식](../11_Workflow.md), 남은 일 전체는 [백로그](../06_Backlog.md)입니다.

## 통합 3 (2026-10-12, 브랜치 `wt/integ3`, 워크트리 `C:\w\integ3`, `main` 으로 fast-forward 하고 push 했다 — `6f3373831`)

검증할 것은 [통합 3 검증 체크리스트](VerifyIntegration3.md)에 모았다. 검증을 마치면 그 문서와 이 절을 지운다.

- **병합**: `main`(8fce26aaf) 위에 1 차 여섯(tcore → tgfs → tinv → tcombat → tposttick → tgfd, 충돌 없음)과 2 차 아홉(uinc → utex → uxml → uaud → uonl → umod → ugfa → ugfb → upipe)을 병합했다.
  uonl 은 `TestServiceClientCallTable.cpp` 에서 충돌(양쪽 테스트를 모두 남김). upipe 의 8021b9337(쿠킹 스탬프 SWCOOK 3 → 4)은 병합 뒤 되돌렸다 — 다시 쿠킹이 필요하다(아래 검증). 사용자가 `main` 에 넣고 push 하라고 해서 fast-forward 로 넣었다. 이 PC 에서는 테스트 · 실행을 하지 않았으므로 검증은 아직이다.
- **빌드(마지막)**: `cmake --preset` 재구성 뒤 Debug · Shipping `all AllTests` 각 한 번 — 오류 0 · 링크 오류 0 · 경고 0. 처음부터 지은 1 차 빌드의 `-Wnrvo` 약 50 곳 · `-Wunique-object-duplication` 3 곳은 원인에서 고쳤다(억제 없음).
  게임 프리셋 Shooter3D · StarSkirmish · HarvestValley · VoxelCraft 는 1 차 병합 직후 `SWGame` 만 지었고(오류 0, 게임 파일 `-Wnrvo` 3 곳은 고침) 2 차 병합 뒤에는 다시 짓지 않았다.
- **린트**: `lint-suite` 통과, 바뀐 헤더 76 개 `CheckHeaderSelfContained` 통과, `CheckSourceGlob` 통과, 2 차 병합분 C++ 139 개는 고정 clang-format 20.1.8 로 차이 없음. 새 게이트 `CheckPostTickThisBinding`(틱 뒤 실행에 `this` 메서드 델리게이트 금지).
- **이번 세션에 고친 결함 · 구조**(커밋마다 문제점/해결방안/결과): `CameraRole` 옛 이름을 기대한 테스트, 틱 뒤 실행 도우미가 삭제 대기 컴포넌트를 부르던 것 + Debug 타입 단언, `this` 를 묶은 틱 뒤 실행 여섯 곳, 모듈 언로드 전 틱 뒤 큐 비우기,
  호출 표의 id 대조 없는 '보내는 중' 응답 · 버리는 `clear`, `OnlineReplyUtil` · `ServiceClientCallTable` 의 `OnlineServiceClient.h` include, 씬 무효화 구독 해제, 개발 명령 · HUD 레지스트리 잠금과 include 정리(`DevCommandsEnabled.h` · `DebugHUDSectionFormat.h`),
  `SW_GF_API` 한 곳 + 게임 서비스 연결 함수 C 링크(`RuntimeAPI/Export/GameFrameworkAPI.h`), `isServiceBound<T>()` 일반화, 리플렉션 생성 이름 충돌 검사 · 구성 뒤 매크로가 생긴 헤더 검사(`cmake/Engine/CheckReflectionCandidates.cmake`), 엔진 층 티어 표.
- **통합 4(로컬, push 안 함)**: 잠재 결함 점검 뒤 수정 여섯 브랜치(tfx1~6)를 `wt/integ3` 에 병합했다(충돌 없음). 재구성 뒤 Debug · Shipping `all AllTests` 경고 0 · 오류 0 · 링크 0, lint-suite · 바뀐 헤더 7 개 자립 · `CheckSourceGlob` · clang-format 20.1.8 통과. 테스트 · 실행은 안 했다.
- **남은 위험**:
  - `MaterialInstance` 는 리로드 세대가 바뀌면 쥔 포인터의 `getSrv()` 로 다시 패킹한다. 다른 텍스처가 아직 리로드 중이면 그 SRV 를 한 프레임 읽을 수 있다(다음 세대에 다시 패킹돼 회복). `Material::refreshTextureBindings` 는 `find` 를 써서 해당 없다.
  - `SW_DELEGATE_METHOD(this)` 를 다른 지연 경로(틱 뒤 실행 밖)에 넘기는 곳은 여전히 핸들을 거치지 않는다. 게이트는 `deferPostTick` · `executeOrDeferPostTick` 만 본다.
  - Shooter 적 체력이 `Vitality` 로 바뀌어 최대 체력 1 하한 · 음수 피해 0 처리가 생겼다(데이터에서 체력 0 을 주면 예전에는 바로 죽고 지금은 1 로 산다).
  - 키트 DLL 이 Base 클래스의 암시적 멤버를 내보낸다(백로그 1-6).
  - tgfd 가 일부러 남긴 것: 대체값을 받는 `readName`(AdventureDungeon · AdventureCooking), trim 하는 `AppearanceXMLUtil::readName`, 공개 API 인 `ScheduleCondition::parseNameList` · `AppearanceXMLUtil::readNameList`(본문만 위임).
  - `GameDataXML.h` 개선은 71 곳 중 5 곳만 `XMLDocument.h` 를 끊었다 — 나머지 66 곳은 노드를 직접 읽어, 호출부를 읽기 인터페이스로 바꿔야 완전히 풀린다.
  - DX11 · GL 의 bindless 인덱스 즉시 재사용 — 다른 해제 경로에도 지연 해제가 필요한지 아직 보지 않았다.
  - umod 의 리눅스 `__builtin_return_address` 분기는 이 PC 에서 짓지 못했다(CI).
- **옮기지 않기로 한 것(C 묶음 판정)**: 장비 피팅 규칙 · Gimmick/Genre · CommandStack · Observability · Dialogue/Sequencer/Localization/GameAutoplay · 리플렉션 에디터 메타 이름 — 언리얼 · 유니티도 이것들을 엔진 쪽(또는 에디터 공용)에 두고 게임 프레임워크로 내리지 않는다(Sequencer · Localization · Dialogue 는 엔진 모듈, 되돌리기는 에디터 트랜잭션, 메타 이름은 UPROPERTY 메타처럼 리플렉션 소유).
  **이득이 작아 보류**: CharacterHit 피해 배율 · HitWindow damage · Ballistics 키트 공유 · FixedTimestep · KeyValueFile 의 Core 이동.
- **사용자가 할 검증**: Debug `-L lint` · `-L nogpu` · `-L hostgpu`, Shipping `-L nogpu`. 새 · 바뀐 테스트 — `ReflectionEnumInfoTest`, `GameObjectManagerTest.PostTickOnComponentSkipsPendingDestroy`, `GameObjectManagerPoolTest.ModuleUnloadDrainsDeferredPostTickWork`,
  `ServiceClientCallTableTest.ForeignResponseDuringSendDoesNotTakeTheSendingDelegate`, 옮긴 `TestDismemberment` · `TestWitcherRPG` · `TestTacticsSRPG` · `TestProgression` · `TestSpatial` · `TestProfilerTimeline`, 온라인 키트 테스트, umod 의 `TestGameService` 회수 · `ModuleHostTest.SharedModuleCannotBeRegisteredForReload`,
  utex 의 새 GPU 테스트(작업 스레드 안 `SW_TEST_DEFENSIVE_SCOPE` 가 스레드별로 동작하는지). 게임 프리셋 넷을 2 차 병합 뒤 다시 짓기.
  **쿠킹 가능한 환경에서**: 8021b9337 을 다시 적용하고 `App --cook-shaders` 로 다시 쿠킹해 쿠킹 셰이더 · `cook.stamp` 와 함께 커밋.
  실기동: 에디터(`-EnableEditor`, 씬 바꾸기 뒤 되돌리기 기록이 비는지 · 뷰포트 카메라 · 디버그 HUD 섹션), Shooter3D(적 체력 · HP 바 · 쓰러짐 · 절단), VoxelCraft · HarvestValley · StarSkirmish 한 판, 모듈 리로드.

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
- `Co-Authored-By` 줄은 실제로 일한 모델 이름이다. 통합 3 이력에는 `Opus 5.5` 27 개와 `Sonnet 5.5` 24 개가 섞여 있고, 사용자가 **실제 모델 기록을 우선하라고 정해 고치지 않았다**(이전 세션처럼 `filter-branch` 로 `Sonnet 5.5` 로 통일하지 않는다).
- `ninja` 는 `-k 0` 으로 지으면 오류가 나도 끝까지 가서 오류를 모아 준다. ctest 프리셋 이름은 `Ninja-Debug-lint` · `Ninja-Debug-nogpu` 이고 `hostgpu` 프리셋은 없어 `ctest --test-dir build/Ninja-Debug -L hostgpu -j 1`(약 16 분) 으로 돌린다.
- 병합용 워크트리 `D:\w\integ2`(브랜치 `wt/integ2`)는 Debug · Shipping 빌드 폴더를 들고 있어 남겨 뒀다. 다음 병합은 `git merge --ff-only main` 으로 맞춘 뒤 시작하면 증분 빌드가 된다.
- 스크립트는 heredoc 대신 Write 도구로(AGENTS.md "Editing traps"). 임시 파일은 scratchpad 에서 에이전트 이름을 앞에 붙인다(도우미끼리 `syntax_check.py` 를 덮어썼다).
- 작업 워크트리 이름은 같은 영역 단위를 한 사람이 맡도록 `s<글자>` 로 지었다. 끝나면 `py -3 -m Scripts worktree-remove <이름>`.
