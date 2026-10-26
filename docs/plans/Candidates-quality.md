# 품질 후보

> 상태: 후보 · 선행: 없음 · 영역: quality · 옛번호: B-5, B-7, B-12, B-14, B-15, B-16, C-28, C-33, E-1, E-4, E-10 · 진행: 해당없음

테스트 틀, CI, 린트 스크립트, 부하 검증, 쿠킹, 간헐 실패 관찰 가운데 아직 착수하지 않은 후보의 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일은 [BuildSpeed](BuildSpeed.md) · [GpuVerification](GpuVerification.md) · [ImportRuleOverlap](ImportRuleOverlap.md) · [SourceScanGates](SourceScanGates.md) · [RuntimeQualityAdjust](RuntimeQualityAdjust.md) 에 있습니다.

## 확인 대기

### LinuxManualChecks
제목: 리눅스에서 자동으로 안 도는 것
옛번호: B-7
왜 필요한가: X11 입력(좌표 · `XkbSetDetectableAutoRepeat`), yad 파일 대화상자의 두 번째 `--file-filter`(man 으로만 확인)가 자동으로 돌지 않는다.
왜 지금 못 하는가: CI 에 xvfb 는 있지만 입력을 넣을 XTest(`libxtst`)와 그것을 쓰는 호스트 테스트가 없다(별도 설계가 필요). yad 는 대화상자가 사람 입력을 기다려 헤드리스 확인이 안 된다(명령줄 조립은 코드가 이미 두 필터를 붙인다).
풀리는 조건: 사용자가 WSL 확인을 요청할 때(2026-10-10 결정, WSLg 의 `DISPLAY=:0` 으로 손으로 한 번), 또는 XTest 입력 테스트를 계획 문서로 올릴 때.

### IntegrationThreeVerification
제목: 통합 3 의 반쯤 남은 검증
옛번호: B-12
왜 필요한가: 통합 3(`8fce26aaf..6f3373831`, 81 커밋)의 검증 중 일상 게이트(Debug · Shipping `nogpu` · `hostgpu`, 리눅스 CI 빌드, `SWGame.dll` 링크)가 못 보는 Release 측정과 사람이 보는 실기동이 남았다.
왜 지금 못 하는가: Release 측정은 GPU PC 에서 `-ftime-trace` 전후 비교가 필요하고 실기동 항목은 창을 띄워 눈으로 보거나 탈것 · 크래시 경로를 직접 몰아야 한다. 풀리는 조건: 통합 담당이 `hostgpu` 를 한 번 돌린 뒤 그래도 남는 항목만 실기동한다.
남은 것:
- Release 측정: 텍스처 로드 중 다른 스레드의 `find` · `acquire` 대기, `Engine.tick` 의 틱 뒤 실행 경로 · `AudioEngine` 게시 · `GT.GPUScene.build`, `GameObject.h` 가 `TagSystem.h` 를 끌어오지 않는지(`-ftime-trace` 전후).
- 실기동(`-gv_profileFrames=60` 으로 스스로 끝나게): 에디터 씬 뷰 카메라가 게임 뷰 수집에서 빠지고 BugIt 캡처 정상 · 씬 교체 뒤 되돌리기 기록이 비고 종료가 깨끗 · 탈것 · 말 · 카메라 감독 · 물 · 지형 · 식생 · 플레이어 청크가 틱 뒤 실행 교체 뒤에도 정상(탑승 중 내림 · 오브젝트 파괴에서 크래시 없음) · 키트 하나만 리로드했을 때 `GameService` 가 새 인스턴스를 가리킴 · 에디터 복사 → 모듈 리로드 → 붙여넣기(`ObjectCopyBuffer`) · 환경설정을 고친 직후 리로드해도 값이 남음 · `registerModule` 이 공용 모듈(대소문자만 다른 이름 포함)을 거절 · Shooter 적 체력이 `Vitality`(0 아래로 안 내려가고 최대 1 미만이면 1 — 쓰러짐 · 절단 확인) · 텍스처 로드 중 다른 키 조회가 막히지 않고 같은 키 동시 요청이 한 번만 만듦, 리로드 직후 한 프레임 안에 머티리얼 SRV 가 새 것으로(네 백엔드) · DX12 두 스레드 `tickCompleted` 안전 · 온라인 응답이 보내는 중 다른 id 로 와도 현재 호출이 안 사라짐 · 로그 범주 이름 변경(`AbilitySystemComponent*` · `ScheduleSystem*`) · 에디터 HUD 섹션 · 개발 콘솔 명령(락 추가, 모듈 로드 교착 없음) · 디버그 HUD 섹션 켜고 끔에 프로파일러가 따라감 · `GameInstanceBase` 의 `GameSettings` 소유자가 게임 모듈 리로드 뒤 회수 · 다시 걸림 · 실패 콜백 · Ready 알림 중 키트가 파괴돼도 크래시 없음 · 모듈 언로드 때 `Deferred work kept re-queueing` 오류가 정상 경로에서 안 남.
함정: 옛 빌드 폴더는 `cmake --preset` 을 먼저 다시 돌린다(파일을 옮겨 코드젠 목록이 낡으면 `missing and no known rule`). 처음 돌린 테스트는 기대값부터 의심한다 — `ReflectionEnumInfoTest`(`CameraRole::Editor` → `Viewport`), `GameObjectManagerTest.PostTickOnComponentSkipsPendingDestroy`, `GameObjectManagerPoolTest.ModuleUnloadDrains*PostTickWork`, `ServiceClientCallTableTest.ForeignResponseDuringSendDoesNotTakeTheSendingDelegate`, 온라인 클라이언트 취소 테스트 둘, `KitDestroyedWhileFailingQueuedCallsGetsNoMoreCalls`, `KitDestroyedDuringReadyNotificationIsSkipped`(테스트 서버 Hello 가 `kGame+0x100` · `+0x200` 키트를 받아 주는지에 달렸다 — 판 불일치로 Ready 에 못 가면 테스트 결함부터), `TestGameService`, `ModuleHostTest.SharedModuleCannotBeRegisteredForReload`, `PrefabTest.CookedPrefabMatchesItsSource`(쿠킹본이 어긋난 것인지 쓰기가 불안정한 것인지부터), `TestRHISupport` 두 스레드 `tickCompleted`, `TestRenderPassGPU` 동시 `acquire`(DX12 · DX11 만 — `SW_TEST_DEFENSIVE_SCOPE` 가 스레드마다 따로 동작하는지), `EditorPreferencesStoreTest.ReloadedSectionReadsTheFileOnce`.

### VerifyBundleLeftovers
제목: verify 묶음 이후 남은 검증
옛번호: B-14
남은 것: Release 네 백엔드 스크린샷. main 은 포워드 · 디퍼드 모두 네 백엔드가 바이트까지 같고, 디퍼드 차이는 `ee6fff232`(반해상도 블룸)의 의도된 변경이다(결정 기록 5-5, 디퍼드 비교 기준은 그 커밋 이후). `py -3 Scripts/dev/RunBackendSmoke.py --preset Ninja-Release --meshes 8` 이 네 백엔드로 그려 평균 색 · 배경이 아닌 픽셀 수를 견준다(새 main 에서 통과하면 이 절을 지운다).

### ImageIntakeChecks
제목: 이미지 도입의 확인
옛번호: B-15
남은 것: normal 맵 G 채널 방향 — `Base_NormalMap` 이 `invert_green: true` 로 구워 Poly Haven `_nor_gl`(Y 위)을 엔진 규약으로 뒤집지만 렌더에서 요철이 맞게 보이는지 확인하지 않았다. 못 하는 이유: 빛 방향을 아는 표면 하나를 그려 요철의 명암 방향을 단언하는 GPU 시나리오가 없다. 풀리는 조건: 그 시나리오를 만들 때(`textures_raw/surfaces/*` 노멀을 쓰는 머티리얼 미리보기 + `ExpectImage`). 다른 크기의 이미지를 들이면 `texture-limits`(밉이 있는 텍스처는 2 의 거듭제곱) 검증이 `ValidateAssets` 에서 막는다.

### UiOnlyScreenCapture
제목: 씬 없는 화면의 다른 백엔드 확인
옛번호: B-16
왜 필요한가: 활성 씬이 없는 프레임을 그리지 않게 한 변경이 Vulkan 에서 `UNDEFINED` 백버퍼를 `Load` 로 여는 결함을 드러냈다(`RenderThread::executeFrameBody` 가 그래프가 그렸을 때만 `Load` 하게 고침). 2026-10-18: Release `-L hostgpu` 9/9 통과, 씬 없는 프레임(캔버스 테스트 그림 + 로딩 화면)은 네 백엔드가 같은 그림이다.
남은 것: 에디터가 씬을 비운 상태의 캡처 시나리오 `engine/automation/editor/emptyscene.scenario.xml`(`AppScenarioTest` 가 백엔드마다 돌린다)을 통합에서 처음 돌려 통과하면 그 항목을 지운다. 타이틀 화면은 엔진 공통 시나리오가 없어 게임 시나리오를 따로 써야 한다. `-gv_uiDemo` 견본이 안 보인 이유는 `AppUITest.DemoScreenMatchesAcrossBackends` 가 견본 사각형 색을 단언해 지킨다(로딩 화면이 가리면 그 테스트가 진다).

### NightlyCiFollowups
제목: 다음 CI 실행으로 확인할 것
옛번호: E-1
왜 필요한가: 리눅스는 컴파일까지 되고 테스트 단계에서 진다(CI 읽는 법은 [검증과 측정](../08_Verification.md) 4절). 원인을 아직 모르는 것이 남았다.
풀리는 조건: push 뒤 CI · 야간 실행이 끝났을 때.
남은 것:
- 원인을 모름: `UIOptionsMenuTest.RebindIgnoresUIActionsWhileListening`(리눅스 Debug · Shipping · ASan, 포커스 위젯 ID 839 ≠ 823 `Tab.controls` — 창이 닫힌 뒤 탐색 Up 이 한 번 발화한 것으로 추정, 다음 실패 메시지가 포커스 위젯 이름을 보인다), ASan 중단 두 건(`PlatformLoginTest.Oidc…` · `NetRequestTest.IdempotentRetry…`), TSan 샤드의 새니타이저 보고.
- 다음 야간 퍼징(`fuzz.yml`)에서 `LoaderFuzzer` `Compression` 이 통과하는지(원인: 헤더 원본 크기 620 MB 를 압축 크기 0 과 견주지 않음 — `decompressBound` 로 고침, 결정 기록 5-3, `seedCompression` 씨앗 추가).
- 다음 야간 부하(`load.yml`)에서 replication × Mid · LowEnd 가 시간 예산을 `::notice` 로만 올리고 통과하는지, connect × Mid 는 OnlineLoadBot 매치메이킹 키트(NEXT 2번)가 고쳐진 뒤 통과하는지.
  replication 의 tick p99 가 러너에서 15 → 17.5 ms 로 오른 것이 회귀인지는 이 PC 에서 `py -3 -m Scripts load run replication --profile Mid LowEnd --runs 3` 을 기준선(약 10 ms)과 견줘 본다.
- Windows STL containers 잡에서 건너뛰게 한 테스트 셋(`MemoryTagTest` 둘, `DestructionDamageTest.EventLogHash…`)이 기본 컨테이너 구성에서도 가끔 지는지.

## 정리

### TestFrameworkRemainders
제목: 테스트 틀 개편의 남은 것
옛번호: C-28
왜 필요한가: 틀 · 정책 · 확장 지점은 들어갔다(`Test/README.md`, 결정 기록 5-2 끝).
남은 것:
- 벤치 스위트를 부하 스위트로 옮긴 뒤의 `CoreTest` · `EngineTest` `seconds` 와 `loadSeconds` 는 뺄셈 어림이라 다음 정책 재측정 때 실제 값으로 맞춘다(통합 담당). `NetSimDestructionMatrixTest` 는 정확성 테스트라 nogpu, 호스트 벤치(`NetUDPBenchTest` · `GlyphBenchTest`)는 호스트에 남겼다.
- 구성 차이 건너뜀: `#if defined( SW_SHIPPING )` 아래의 `SW_TEST_SKIP` 27 곳은 `SW_TEST_REQUIRES_FEATURE( DevelopmentBuild )` 로 바꿨다(컴파일 확인 전). 남은 것은 `#if` 없이 런타임에 조건을 보는 건너뜀(서버 팩 오디오 `TestAudioStream`, 셰이더 컴파일러 유무 `TestShaderCompiler`, `gv_vfxSimulation` 등록 여부 `TestVfx`) — 필요하면 `BuildFeature` 에 `ServerPackAudio` 를 더한다.
- JUnit XML 리스너(`ITestRunListener`) — CI 대시보드를 쓰기로 정할 때.

### LogGateRemainders
제목: 로그 게이트의 남은 것
옛번호: C-33
왜 필요한가: 시나리오 · 호스트 스위트는 선언하지 않은 Warning · Error 로 진다(결정 기록 5-2, `Source/Engine/Automation/README.md`).
남은 것:
- CPU 단위 테스트(nogpu)는 아직 Error 도 세지 않는다. 표식 없는 Warning · Error 가 약 1,500 줄(2026-10-15, 일부러 깨진 카탈로그 픽스처 · 외형 스키마 테스트 · 경로 오류 등). `SW_TEST_DEFENSIVE_SCOPE` 로 감싸거나 원인을 고친 뒤 `HostLogJudge` 를 모든 스위트로 넓힌다. 줄 목록은 `ctest -L nogpu` 뒤 LastTest.log 의 `[Warning]` · `[Error]` 를 (호출자, 문구) 로 묶어 본다.
- 전용 서버를 서버 설정이 없는 게임(`Config/Server/<게임>.json` 이 Empty 뿐)으로 띄우면 Dev 기본값 Warning 두 줄. 서버를 낼 게임이 정해지면 설정 파일을 둔다.
- 에디터 켠 짧은 실행(`-gv_profileFrames=90`)을 다른 GPU 작업과 겹쳐 돌리면 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 가 났다(36 회 중 3 회, 단독 재실행 0 회). 종료 경로의 문구는 고쳤다(`ModuleHost::drainRenderWorkers` 가 종료 앞이면 "at shutdown" 문구와 남은 태스크 수를 찍는다, 컴파일 확인 전). 5 초 넘게 도는 에디터 배경 작업이 무엇인지는 모른다 — 다시 보이면 남은 태스크 수와 `py -3 -m Scripts stacks <pid>` 의 워커 스택으로 가린다.

## 관찰

### ShardExitCodeAfterPass
제목: 통과 뒤 0 이 아닌 종료 코드와 종료 멈춤
옛번호: E-4, E-10
왜 필요한가: 테스트가 모두 통과한 뒤 종료에서 지는 CI 실패가 세 번 났다 — Windows Debug `EngineTest_NoGPU_Shard3`(run 37457443235, 한 번, 로그가 `TaskManager Shutdown cleanly` 에서 끝나고 크래시 스택 없음), 리눅스 Shipping `EngineTest_NoGPU_Shard2`(run 38163565253, 요약까지 찍힌 뒤 마지막 줄 없이 180 초 한도 — `stop()` 안에서 멈춤), 리눅스 Shipping `EngineTest_NoGPU_Shard5`(run 38439985866, `TestHostRuntime::stop` 의 `destroyAll` 에서 끝나지 않음 — 같은 실행의 오디오 디코드 태스크 수명 결함(고침)이 워커를 망가뜨려 join 이 멈춘 것인지 미확정). 이 PC Debug 로 같은 조각 15 회 순차 · 12 회 4 개 동시 — 재현 안 됨.
왜 지금 못 하는가: 재현되지 않고, 종료 경로는 서브시스템 분해로 다시 쓰였으며 단계 앞마다 `[TestHost] stopping: …`, `main` 이 끝에 `[TestHost] shut down - exit code N` 을 이미 찍는다 — 코드를 더 읽어서는 좁혀지지 않는다.
풀리는 조건: 다시 나면 마지막 줄 `[TestHost] shut down - exit code N` 이 없으면 `TestHostRuntime::stop` 안(마지막으로 찍힌 `stopping:` 단계), 있으면 정적 소멸자다. 계측이 든 이후 CI 주석에서 처음 나는 실행으로 가른다.
