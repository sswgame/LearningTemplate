# 품질 후보

> 상태: 후보 · 선행: 없음 · 영역: quality · 옛번호: B-5, B-7, B-12, B-14, B-15, B-16, C-28, C-29, C-33, C-44, C-46, C-48, E-1, E-2, E-4, E-5, E-8, E-10, E-14, E-17 · 진행: 해당없음

테스트 틀, CI, 린트 스크립트, 부하 검증, 쿠킹, 간헐 실패 관찰 가운데 아직 착수하지 않은 후보의 목록입니다. 착수하면 절을 계획 문서로 옮기고 여기서 지웁니다.
이미 계획 문서가 있는 일은 [BuildSpeed](BuildSpeed.md) · [GpuVerification](GpuVerification.md) · [IncrementalCook](IncrementalCook.md) 에 있습니다.

## 결함

### ImportRuleOverlap
제목: 임포트 규칙의 넓은 와일드카드와 첫 매칭
옛번호: C-44
왜 필요한가: `ModelImportConfig.json` 이 "넓은 와일드카드 + 첫 매칭"이라, 새 팩 파일에 다른 팩의 배치 오프셋이 적용돼도 경고가 없다(`res3` 도입에서 확인, 새 줄을 표 앞에 두어 피했다).
credits 표는 고쳤다 — `CheckResourceCredits` 의 패턴은 경로 마디 단위(`*` 는 폴더를 넘지 않고 `**` 만 넘는다)이고 한 파일에 줄이 둘 이상 맞으면 오류다. 출처 목록(`*.sources.json`)의 해시 · 목록 밖 파일도 그 게이트가 본다.
남은 것:
- 임포트 규칙도 같은 마디 단위 매칭 + 둘 이상 맞으면 오류로: C++ `ModelImportConfig` · `TextureImportConfig` 매칭기. 순서가 뜻을 가진 규칙이 있어(`StarSkirmish_KayKitSpaceBase` 가 `StarSkirmish_KitLayoutOffset` 보다 먼저 맞아야 한다) 규칙 데이터를 겹치지 않게 다시 써야 한다.
- 출처 목록의 해시가 맞으려면 산출물이 결정적이어야 한다. 인코더가 만드는 다른 산출물(텍스처 DDS 변환 등)의 비결정 바이트를 점검한다(같은 원본을 두 번 구워 바이트 비교, App 빌드와 실행 필요).

### StlJobKitTestFailure
제목: CI Windows Debug · STL 잡의 키트 테스트 22 건 실패
옛번호: E-2
왜 필요한가: run 37251047781(`a380e2ee3`)에서 유니티 켬 + PCH 끔 Debug 에서만 졌다. 이 PC 의 같은 구성(`CI-Debug -DSW_ENABLE_PCH=OFF`)에서 통과해 재현되지 않았다.
착수 조건: 실패 보고(`CiFailureReport.py`)가 나아진 뒤 다음 CI 주석으로 본다. 그 뒤 액션 키트 테스트 넷이 네 실행 연속으로 지는 간헐 실패가 되었으므로 일회성으로 보지 않는다.

## 확인 대기

### LinuxManualChecks
제목: 리눅스에서 자동으로 안 도는 것
옛번호: B-7
왜 필요한가: X11 입력(좌표 · `XkbSetDetectableAutoRepeat`), yad 파일 대화상자의 두 번째 `--file-filter`(man 으로만 확인)가 자동으로 돌지 않는다.
왜 지금 못 하는가: CI 에는 xvfb 가 있지만 입력을 넣을 XTest(`libxtst`)와 그것을 쓰는 호스트 테스트가 없다 — 창을 띄운 채 좌표 · 자동 반복 이벤트를 주입하는 테스트는 설계가 필요한 별도 일이다. yad 는 대화상자가 사람 입력을 기다려 헤드리스 확인이 안 된다(명령줄 조립은 코드가 이미 두 필터를 붙인다).
풀리는 조건: 사용자가 WSL 확인을 요청할 때(2026-10-10 결정, WSLg 의 `DISPLAY=:0` 으로 손으로 한 번), 또는 XTest 입력 테스트를 계획 문서로 올릴 때.

### IntegrationThreeVerification
제목: 통합 3 의 반쯤 남은 검증
옛번호: B-12
왜 필요한가: 통합 3(`8fce26aaf..6f3373831`, 81 커밋)의 검증이 반쯤 남았다. 아래는 일상 게이트가 못 보는 Release 측정과 사람이 보는 실기동뿐이다(실기동 중 아래 테스트 이름이 있는 것은 `hostgpu` 가 돌린다).
왜 지금 못 하는가: Release 측정은 GPU PC 에서 `-ftime-trace` 전후 비교가 필요하고, 실기동 항목은 창을 띄워 눈으로 보거나 탈것 · 크래시 경로를 직접 몰아야 한다. 풀리는 조건: 통합 담당이 `hostgpu` 를 한 번 돌린 뒤 그래도 남는 항목만 실기동한다.
남은 것:
- 지운 것(일상 게이트가 지킨다): 테스트 묶음(Debug · Shipping `nogpu` · `hostgpu`, 게임 넷 실행)은 병합마다 도는 11_Workflow.md 3절 단계이고, 옮긴 테스트가 사라지거나 두 번 잡히면 `ctest` 개수와 `AllTests` 가 드러낸다. `GameFramework.dll` 내보내기는 `SWGame.dll` 링크가 심볼을 못 찾으면 Windows 링크가 서므로 `dumpbin` 이 따로 필요 없다. 반사 헤더 중복 `FATAL` 은 구성 단계 검사다. 리눅스 컴파일(GLX · 다중 상속 경고 · `NavDebugView` · `OpenGLRHICommandList.cpp`)은 CI 의 리눅스 빌드가 매번 본다.
- Release 측정: 텍스처 로드 중 다른 스레드의 `find` · `acquire` 대기, `Engine.tick` 의 틱 뒤 실행 경로 · `AudioEngine` 게시 · `GT.GPUScene.build`, `GameObject.h` 가 `TagSystem.h` 를 끌어오지 않는지(`-ftime-trace` 전후).
- 실기동(`-gv_profileFrames=60` 으로 스스로 끝나게): 에디터 씬 뷰 카메라가 게임 뷰 수집에서 빠지고 BugIt 캡처가 정상 · 씬을 바꾼 뒤 되돌리기 기록이 비고 종료가 깨끗 · 탈것 · 말 · 카메라 감독 · 물 · 지형 · 식생 · 플레이어 청크가 틱 뒤 실행 교체 뒤에도 정상(탑승 중 내림 · 오브젝트 파괴에서 크래시 없음) ·
  키트 하나만 리로드했을 때 `GameService` 가 새 인스턴스를 가리킴 · 에디터 복사 → 에디터 모듈 리로드 → 붙여넣기(`ObjectCopyBuffer`) · 환경설정을 고친 직후 리로드해도 값이 남음(새 섹션 읽기 포함) · `registerModule` 이 공용 모듈(대소문자만 다른 이름 포함)을 거절 ·
  Shooter 적 체력이 `Vitality`(0 아래로 안 내려가고 최대 1 미만이면 1 — 데이터 체력 0 이 예전엔 즉사, 지금은 1 로 산다; 쓰러짐 · 절단 확인) · 텍스처 로드 중 다른 키 조회가 막히지 않고 같은 키 동시 요청이 한 번만 만듦, 리로드 직후 한 프레임 안에 머티리얼 SRV 가 새 것으로(네 백엔드) · DX12 에서 두 스레드 `tickCompleted` 안전 ·
  온라인 응답이 보내는 중 다른 id 로 와도 현재 호출이 안 사라짐 · 로그 범주 이름 변경(`AbilitySystemComponent*` · `ScheduleSystem*` 가 파일 이름으로 — 로그 필터 도구가 있으면 맞춘다) · 에디터 HUD 섹션 · 개발 콘솔 명령(락 추가, 모듈 로드 교착 없음) · 디버그 HUD 섹션을 켜고 끄면 프로파일러가 따라 켜지고 꺼짐 ·
  `GameInstanceBase` 의 `GameSettings` 소유자가 게임 모듈 리로드 뒤 회수 · 다시 걸림 · 실패 콜백 · Ready 알림 중 키트가 파괴돼도 크래시 없음 · 모듈 언로드 때 `Deferred work kept re-queueing` 오류 로그가 정상 경로에서 안 남.
함정: 옛 빌드 폴더는 `cmake --preset` 을 먼저 다시 돌린다(파일을 옮겨 코드젠 목록이 낡으면 `missing and no known rule`). 처음 돌린 테스트는 기대값부터 의심한다 —
`ReflectionEnumInfoTest`(`CameraRole::Editor` → `Viewport`), `GameObjectManagerTest.PostTickOnComponentSkipsPendingDestroy`, `GameObjectManagerPoolTest.ModuleUnloadDrains{Deferred,Requeued}PostTickWork`,
`ServiceClientCallTableTest.ForeignResponseDuringSendDoesNotTakeTheSendingDelegate`, 온라인 클라이언트 취소 테스트 둘 · `KitDestroyedWhileFailingQueuedCallsGetsNoMoreCalls` ·
`KitDestroyedDuringReadyNotificationIsSkipped`(후자는 테스트 서버 Hello 가 `kGame+0x100` · `+0x200` 키트를 받아 주는지에 달렸다 — 판 불일치로 Ready 에 못 가면 테스트 결함부터), `TestGameService`(회수 · `ExplicitOwnerDecidesWhichImageReleasesTheService`),
`ModuleHostTest.SharedModuleCannotBeRegisteredForReload`(Windows 대소문자 케이스 포함), `PrefabTest.CookedPrefabMatchesItsSource`(틀리면 쿠킹본이 어긋난 것인지 쓰기가 불안정한 것인지부터),
`TestRHISupport` 두 스레드 `tickCompleted`, `TestRenderPassGPU` 동시 `acquire`(DX12 · DX11 만 — 작업 스레드 안 `SW_TEST_DEFENSIVE_SCOPE` 가 스레드마다 따로 동작하는지), `EditorPreferencesStoreTest.ReloadedSectionReadsTheFileOnce`.

### VerifyBundleLeftovers
제목: verify 묶음 이후 남은 검증 둘
옛번호: B-14
왜 필요한가: 2026-10-15 `verify` 묶음 이후 둘이 남았다.
남은 것:
- Release 네 백엔드 스크린샷: main 은 포워드 · 디퍼드 모두 네 백엔드가 바이트까지 같다. perfbase 와 포워드는 같고, 디퍼드 차이는 `ee6fff232`(반해상도 블룸)의 의도된 변경이다(결정 기록 5-5). 디퍼드 비교 기준은 그 커밋 이후다. 도구가 이미 있다 — `py -3 Scripts/dev/RunBackendSmoke.py --preset Ninja-Release --meshes 8` 이 네 백엔드로 그려 평균 색 · 배경이 아닌 픽셀 수를 견준다(같은 명령을 새 main 에서 돌려 통과하면 이 절을 지운다).
(지운 것: Linux 전용 파일 컴파일 — `PosixCrashHandler.cpp` · `EpollStreamTransport.cpp` · `FileUtil.cpp` · `HardwareProbe.cpp` 는 CI 의 리눅스 빌드가 매번 컴파일하므로 WSL 확인이 필요 없다.)

### ImageIntakeChecks
제목: 이미지 도입의 확인 둘
옛번호: B-15
왜 필요한가: `res1` 이미지 도입에서 둘을 확인하지 않았다.
남은 것:
- normal 맵 G 채널 방향 — `Base_NormalMap` 이 `invert_green: true` 로 구워 Poly Haven `_nor_gl`(Y 위) 을 엔진 쪽 규약으로 뒤집지만, 렌더에서 요철이 맞게 보이는지는 확인하지 않았다. 못 하는 이유: 빛 방향을 아는 표면 하나를 그려 요철의 명암 방향을 단언하는 GPU 시나리오가 없다. 풀리는 조건: 그 시나리오를 만들 때(`textures_raw/surfaces/*` 노멀을 쓰는 머티리얼 미리보기 + `ExpectImage`).
(지운 것: Kenney "Smoke Particles"(362x336) 는 들이지 않았고, 들인 Particle Pack `smoke_*.png` 네 장은 512x512 로 확인했다. 다른 크기를 들이면 `texture-limits`(밉이 있는 텍스처는 2 의 거듭제곱) 검증이 `ValidateAssets` 에서 막는다.)

### UiOnlyScreenCapture
제목: 씬 없는 화면의 다른 백엔드 확인
옛번호: B-16
왜 필요한가: `fixgfx`(옛 `A-25` · `A-26` · 렌더 스레드 수정)는 GPU 테스트를 DX12 호스트에서만 돌렸고, 활성 씬이 없는 프레임을 그리지 않게 한 변경이 Vulkan 에서 `UNDEFINED` 백버퍼를 `Load` 로 여는 결함을 드러냈다
(`RenderThread::executeFrameBody` 가 그래프가 그렸을 때만 `Load` 하게 고침, Debug `race` 5/5 로 확인). 2026-10-18 확인: Release(`-DSW_GAMES=ALL`) `-L hostgpu` 9/9 통과(샤드가 백엔드를 어떻게 나눴는지는 로그로 못 봤다), 씬 없는 프레임(캔버스 테스트 그림 + 로딩 화면)은 네 백엔드가 같은 그림으로 UI 를 그린다.
남은 것: 에디터가 씬을 비운 상태의 캡처는 시나리오 `engine/automation/editor/emptyscene.scenario.xml` 로 만들었다(`AppScenarioTest` 가 백엔드마다 돌린다). 통합에서 처음 돌려 통과하면 그 항목을 지운다. 타이틀 화면은 게임마다 다르고 엔진 공통 시나리오가 없어 게임 시나리오를 따로 써야 한다.
`-gv_uiDemo` 견본이 안 보인 이유는 `AppUITest.DemoScreenMatchesAcrossBackends` 가 견본 사각형의 색(강조색 단추 · 둥근 상자 · 자르기)을 단언해 지킨다 — 로딩 화면이 가리면 그 테스트가 진다.

### NightlyCiFollowups
제목: 다음 CI 실행으로 확인할 것
옛번호: E-1
왜 필요한가: 리눅스는 컴파일까지 되고 테스트 단계에서 진다(CI 읽는 법은 [검증과 측정](../08_Verification.md) 4절). 고친 것은 2026-10-18 CI 전 잡 통과(run 39163263777)로 확인해 지웠고, 원인을 아직 모르는 것이 남았다.
왜 지금 못 하는가: 다음 CI 실행 결과가 있어야 판정된다(`py -3 -m Scripts ci`류 도구로 최근 실행을 읽는다). 풀리는 조건: push 뒤 CI · 야간 실행이 끝났을 때.
남은 것:
- 원인을 모름: `UIOptionsMenuTest.RebindIgnoresUIActionsWhileListening`(리눅스 Debug · Shipping · ASan, 포커스 위젯 ID 839 ≠ 823 `Tab.controls` — 창이 닫힌 뒤 탐색 Up 이 한 번 발화한 것으로 추정, 다음 실패 메시지가 포커스 위젯 이름을 보인다), ASan 중단 두 건(`PlatformLoginTest.Oidc…` · `NetRequestTest.IdempotentRetry…`), TSan 샤드의 새니타이저 보고.
- 다음 야간 퍼징(`fuzz.yml`)에서 `LoaderFuzzer` `Compression` 이 통과하는지 확인한다(원인은 헤더의 원본 크기 620 MB 를 압축 크기 0 과 견주지 않은 것 — `decompressBound` 로 고침, 결정 기록 5-3. 그 입력은 `seedCompression` 씨앗으로 더했고, 팩 읽기 `decompressData` 도 할당 전에 `decompressBound` 로 끊는 것을 코드로 확인했다).
- Windows STL containers 잡에서 건너뛰게 한 테스트 셋(`MemoryTagTest` 둘, `DestructionDamageTest.EventLogHash…`)이 기본 컨테이너 구성에서도 가끔 지는지 지켜본다.

## 정리

### SourceScanRemainingGates
제목: 공유 훑기로 못 옮긴 소스 게이트와 픽서
옛번호: C-48
왜 필요한가: 파일마다 보는 소스 게이트 28 개는 `SourceScan` · `RunSourceGates`(CTest · CI 의 `CheckSourceFiles`)로 옮겼다(2026-10-17 `lintspeed`). 추가 줄 · 기준선은 `LintGate.BaselineSourceGate` · `SourceScan.LineScope` 로 옮겼다(2026-10-18).
남은 것: 못 옮긴 `NamespaceBlocks` · `IncludeOrder`(줄 끝 · utf-8-sig 를 따로 다루고 픽서와 판정이 나뉨), `ThirdPartyIsolation` · `SuppressionReasons`(C++ 와 CMake 두 파일 집합), `KitNamespaces`.
픽서(`FormatBranchBraces` 등)도 파일마다 따로 읽고 주석을 가리는 같은 틀이다 — 공유 드라이버가 판정 단위(줄 끝 · 인코딩 · 파일 집합)를 형식으로 갖게 한다.

### TestFrameworkRemainders
제목: 테스트 틀 개편의 남은 것
옛번호: C-28
왜 필요한가: 틀 · 정책 · 확장 지점은 들어갔다(`Test/README.md`, 결정 기록 5-2 끝). 아래가 남았다.
남은 것:
- 벤치 스위트를 부하 스위트로 옮겼다(2026-10-15). 옮긴 뒤의 `CoreTest` · `EngineTest` `seconds` 와 `loadSeconds` 는 뺄셈 어림이라 다음 정책 재측정 때 실제 값으로 맞춘다. `NetSimDestructionMatrixTest` 는 손실 회선의 수렴(정확성) 테스트라 nogpu 에 남겼고, 호스트 벤치(`NetUDPBenchTest` · `GlyphBenchTest`)는 스위트 종류가 하나뿐이라 호스트로 남겼다.
- 구성 차이 건너뜀 중 `SW_TEST_REQUIRES_FEATURE` 로 아직 안 바꾼 것 — Shipping 의 에디터 · 설정 파일 · 개발 전역 변수 · 서버 팩 오디오 쪽(`TestAppScenario` · `TestConfigManager` · `TestNetClientServer` 등 열 곳 안팎). 필요하면 `BuildFeature` 에 `Editor` · `DevGlobalVariables` 를 더한다.
- JUnit XML 리스너(`ITestRunListener`) — CI 대시보드가 필요해지면.

### LogGateRemainders
제목: 로그 게이트의 남은 것
옛번호: C-33
왜 필요한가: 시나리오 · 호스트 스위트는 선언하지 않은 Warning · Error 로 진다(결정 기록 5-2, `Source/Engine/Automation/README.md`, 2026-10-15). 아래가 남았다.
남은 것:
- CPU 단위 테스트(nogpu)는 아직 Error 도 세지 않는다. 2026-10-15 한 번에 표식 없는 Warning · Error 가 약 1,500 줄(일부러 깨진 카탈로그 픽스처 · 외형 스키마 테스트 · 경로 오류 등). `SW_TEST_DEFENSIVE_SCOPE` 로 감싸거나 원인을 고친 뒤 `HostLogJudge` 를 모든 스위트로 넓힌다.
- 전용 서버를 서버 설정이 없는 게임(`Config/Server/<게임>.json` 이 Empty 뿐)으로 띄우면 Dev 기본값 Warning 두 줄(`Server config … could not be read`, `ServerConfig using cpp defaults`). 서버를 낼 게임이 정해지면 설정 파일을 둔다.
- 에디터 켠 짧은 실행(`-gv_profileFrames=90`)을 다른 GPU 작업과 겹쳐 돌리면 종료 때 `Task fencing timeout (5000 ms) before module reload` Error 가 났다(36 회 중 3 회, 단독 재실행 0 회; 2026-10-16 독점 GPU 단독 DX12 5 회 0 회). 종료 경로(`ModuleHost::drainRenderWorkers`)는 리로드가 아닌데 같은 문구를 쓰고, 5 초 넘게 도는 에디터 배경 작업이 무엇인지 모른다.
함정: 줄 목록은 `ctest -L nogpu` 뒤 LastTest.log 의 `[Warning]` · `[Error]` 를 (호출자, 문구) 로 묶어 본다. 종료 오류가 다시 보이면 `py -3 -m Scripts stacks <pid>` 로 워커 스택을 남긴다.

### ShippingCookSingleGame
제목: Shipping 쿠킹을 그 게임의 팩만 굽게
옛번호: C-46
왜 필요한가: 패키지 내용은 고쳤다(2026-10-18): `MakePackage` 가 `Bin/Packs` 에서 공용 팩과 그 게임의 팩만 고르고(`CookAssets.listPackNameOfGame`, 게임 프리셋의 `_packRoot`), 그 팩이 없으면 실패한다. `CookAssets --game <게임>` 은 그 게임의 팩만 굽는다.
그러나 Shipping 쿠킹이 아직 모든 게임의 팩을 구워 쓰지 않는 게임 팩에 쿠킹 시간만 든다.
남은 것: `cmake/Engine/AssetAndToolTargets.cmake` 의 `swCookArgs` 가 Shipping(`SW_SHIPPING_BUILD`)일 때 `--game ${SW_ACTIVE_GAME}` 을 넘기게 한다. Dev 프리셋은 `-game=` 으로 고르므로 계속 모두 굽는다.
함정: 공용 오디오를 일부 게임만 쓰게 되면 그때 `Config/Game/<게임>.json` 에 쓰는 공용 팩을 적는 선언을 들인다.

## 기능

### RuntimeQualityAdjust
제목: 부하 검증의 남은 것과 자동 품질 조정
옛번호: C-29
왜 필요한가: 부하 검증(2026-10-15)이 들어갔다(쓰는 법은 [검증과 측정](../08_Verification.md) "부하 검증" 절, 결정은 결정 기록 5-2 끝). 첫 측정이 드러낸 것과 남은 것이 있다.
남은 것:
- 개발 배치의 기동 열기 309 ~ 316 번 중 셋은 Shipping 에 없는 일이다 — 키트 DLL 섀도 복사 61(읽고 쓰고 PDB 까지 복사), 모듈 매니페스트 70(`Bin/Modules/*.module.json` 낱개 — 언리얼 `.modules` 처럼 한 파일로 묶을 수 있다), 셰이더 원본 해시 약 30(`.hlsl` · `.hlsli` 를 읽어 쿠킹 스탬프를 대조). LowEnd 흉내(8 ms)에서 이것만 약 1.3 s 다. 배포 기동을 재려면 쿠킹 팩을 올린 Release(`--cooked-dir`)로 부하 시나리오를 돌리는 길을 먼저 만든다.
- 렌더 벤치 · aiskirmish 의 최악 프레임에 13 ~ 20 ms 한 프레임이 판마다 나오다 말다 한다 — 디스크와 무관하다(측정 창 안 파일 열기 0, 흉내를 꺼도 나온다). 어느 구간인지 Tracy(`gv_profileTracePath`)로 그 프레임을 본다.
- 실행 중 자동 품질 조정(언리얼 Dynamic Resolution · 유니티 Adaptive Performance)은 아직 없다. 지금은 `load scale` 이 밖에서 단계를 내려 예산을 지키는 첫 단계를 찾는다.
- 하드웨어 흉내는 코어 수 · 메모리 · 디스크 · 해상도 · 품질까지다. 클럭 · 캐시 · GPU 성능은 흉내 내지 않으니 LowEnd 의 프레임 시간이 Native 보다 짧게 나올 수 있다 — 실제 저사양 기기로 한 번 재서 흉내와의 차이를 적는다.
- 야간 잡(`load.yml`) 첫 실행(38813564584)은 성공했다 — 리눅스에서 OnlineLoadBot 이 서버 키트를 올려 connect × Mid, replication × Mid · LowEnd 가 통과했다(오류율 0, tick p99 약 15 ms). 러너에는 기준선이 없어 알림만 나오므로 기준선 비교는 이 PC 에서만 본다.
착수 조건: 자동 품질 조정은 무거운 게임 장면이 예산(16.7 ms)을 깰 때. 첫 측정에서는 모든 프로필이 GT · RT · GPU p99 예산의 1/10 안팎이라 이득이 보이지 않았다. 깨면 렌더 스케일부터(`gv_renderScale`) 들인다.

## 관찰

### ShardExitCodeAfterPass
제목: 통과 뒤 0 이 아닌 종료 코드와 종료 멈춤
옛번호: E-4, E-10
왜 필요한가: CI Windows Debug `EngineTest_NoGPU_Shard3` 가 모두 통과한 뒤 0 이 아닌 종료 코드로 졌다(run 37457443235 · `28d5627e3`, 한 번 — 직전 실행은 통과). 로그는 `TaskManager Shutdown cleanly` 에서 끝났고 크래시 스택 주석이 없었다.
이 PC Debug 로 같은 조각(`--test_shard=2/3 --host_suites=exclude`) 15 회 순차 · 12 회 4 개 동시 — 재현 안 됨.
- 리눅스 Shipping 에서 같은 증상(E-4 의 두 번째, run 38163565253, `734f7fa08`, `EngineTest_NoGPU_Shard2`): 테스트는 10 초에 끝나 요약까지 찍혔는데 마지막 줄이 없이 180 초 한도로 졌다 — `stop()` 안에서 멈춘 것이다.
- `EngineTest_NoGPU_Shard5`(E-10, 리눅스 Shipping, CI 실행 38439985866): 케이스는 모두 통과하고 요약까지 찍힌 뒤 `[TestHost] stopping: destroying objects`(`TestHostRuntime::stop` 의 `destroyAll`)에서 끝나지 않았다. 같은 실행의 오디오 디코드 태스크 수명 결함(해제된 `AudioClipCache` 를 워커가 씀 — 고쳤다)이 워커를 망가뜨려 태스크 매니저 join 이 멈춘 것인지 미확정이다.
착수 조건: 다시 나면 마지막 줄 `[TestHost] shut down - exit code N`(하네스가 찍는다)이 없으면 `TestHostRuntime::stop` 안(부트스트랩 · 로거 · 메모리 프로파일러 종료), 있으면 정적 소멸자다. `stop()` 이 단계 앞마다 찍는 `[TestHost] stopping: …` 줄로 좁힌다. 종료 경로가 서브시스템 분해로 다시 쓰인 뒤 새 기준에서 다시 본다.

### WindowsLinkFileLock
제목: CI Windows STL 잡의 파일 잠금 연속 실패
옛번호: E-5
왜 필요한가: `334cae46e` · `734f7fa08` 에서 두 번 연속 졌다. 첫 번은 `OnlineLoadBot.exe` 링크("being used by another process"), 둘째는 모듈 링크 뒤 `vcpkg z-applocal` 실행("resource busy or locked"). 둘 다 `BuildTestBinaries`(Test 단계)의 ninja 에서 났다.
`StageModuleRuntimeDlls.cmake` 가 z-applocal 을 다섯 번까지 다시 띄운다. `OnlineLoadBot.exe` 쪽은 막지 못했다.
착수 조건: 근본 의문 — `Build` 단계가 끝난 트리에서 `BuildTestBinaries` 가 또 294 단계를 링크한다(ninja 가 `Re-checking globbed directories` 뒤 `Re-running CMake` 를 도는 것을 로컬 드라이런에서 봤다). 빌드 직후 두 번째 빌드가 빈손이 아닌 이유를 찾으면 Test 단계의 재링크와 잠금 경합이 함께 사라진다.

### WarpPixelTestFailure
제목: CI Windows 러너(WARP)의 픽셀 테스트 실패
옛번호: E-8
왜 필요한가: `RenderPassGPUTest` 를 host 스위트로 빼서 우회했다. 이 PC 에서 `SW_RHI_SOFTWARE_ADAPTER=1`(`-gv_rhiSoftwareAdapter=1`)로 같은 래스터라이저를 고를 수 있다. 2026-10-06 이 PC 의 WARP(DX12 · DX11)로 `RenderPassGPUTest.*` 71 개 픽셀 테스트가 모두 통과했다(Vulkan 은 CPU 디바이스가 없어 빠지고 GL 은 하드웨어) — WARP 자체는 컴퓨트 컬링 · 인디렉트를 한다.
착수 조건: 러너 쪽(WARP 판 · 창 없는 세션)을 다음 CI 실패의 주석으로 가른다.

### MeshModifiersChainFailure
제목: `meshmodifiers` Vulkan 체인 1 회
옛번호: E-14
왜 필요한가: 2026-10-15 에디터 체인(다른 hostgpu 와 GPU 공유 실행 중)에서 frame 585 `MeshModifiers.ReferenceListCount == 1, got 0`. 단독 Vulkan 2/2 통과, 앞선 순차 hostgpu 네 백엔드도 통과했다. 2026-10-16 독점 GPU 단독 Vulkan 5/5 통과.
착수 조건: 다시 나면 참조 목록 항목 추가가 부하 아래 한 프레임 늦는지(고정 프레임 간격) 본다.

### ShaderCookerStampFlake
제목: `ShaderCookerTest.FailedSourcesAreLeftOutOfTheCookStamp` 병렬 실행 1 회 실패
옛번호: E-17
왜 필요한가: `ctest -L nogpu -j` 에서 1 회 실패했다(2026-10-17 `integ2`, Debug, `isCookedOutputCurrent( binDir, goodPath )` 가 false). 혼자 5 회 반복 · 샤드 단독 재실행은 통과했다.
착수 조건: 다시 나면 병렬 실행 중 공유 헤더 캐시(`invalidateSharedHeaderCache`)나 임시 폴더를 다른 케이스와 나누는지 본다.
