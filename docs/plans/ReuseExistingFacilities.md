# 이미 있는 기능으로 옮기기

> 상태: 구현대기 · 선행: 없음 · 영역: quality · 옛번호: 없음 · 진행: 0/8

> 질문: Core · Engine 에 이미 있는 기능을 두고 C 런타임 · 표준 라이브러리를 직접 부르는 곳은 몇 군데이고, 어떤 순서로 옮기는가?
> 독자: 아래 단위를 맡는 작업자와 단위를 나눠 주는 통합 담당.
> 결론: 직접 호출은 64건이다(2026-10-27 기준선). 단위마다 먼저 형식(대체 API · 레이어 경계)으로 막을 길을 만들고 호출부를 옮긴다. 기준선은 단위가 아니라 통합이 낮춘다.

쓸 것과 쓰지 말 것의 표는 [ExistingFacilities](../agents/ExistingFacilities.md) 한 곳에 있습니다. 새로 늘지 않게 막는 게이트는 `Scripts/lint/gate/CheckFacilityBypass.py` 이고, 규칙마다 형식으로 막지 못하는 이유(`whyNotType`)를 적었습니다. 기준선과 허용 목록은 `Scripts/lint/rules/CheckFacilityBypass.toml` 에 있습니다.

## 1. 지금 위반 수

`py -3 Scripts/lint/gate/CheckFacilityBypass.py --list` 로 셉니다. 허용 목록(그 기능을 구현하는 파일)은 세지 않습니다.

| 규칙 | 쓸 것 | Core | Engine | GameFramework | Editor | ModuleHost | Tools | Test | 합 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stdout | `SW_LOG_*` · `SW_LOW_LEVEL_*` | 7 | 6 | 2 | 0 | 1 | 1 | 14 | 31 |
| random | `MathUtil::getRandom*` · `XorShift32` | 4 | 0 | 0 | 1 | 0 | 0 | 1 | 6 |
| fileOpen | `FileUtil::read*` · `write*` | 0 | 1 | 0 | 0 | 0 | 0 | 1 | 2 |
| processSpawn | `sw::Process` | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| numberParse | `StringUtil::parse*` | 0 | 6 | 1 | 0 | 0 | 0 | 17 | 24 |
| 합 | | 12 | 13 | 3 | 1 | 1 | 1 | 33 | 64 |

이미 다른 게이트가 막아 이 표에 넣지 않은 것은 시계 · 환경 변수 · `std::thread` · 맨 할당 · `std::filesystem` · 서드파티 헤더입니다. 게이트 이름은 카탈로그 표의 "Basis" 칸에 있습니다.

게이트로 만들지 않고 카탈로그에만 둔 것은 아래와 같습니다. 모양만으로는 정당한 코드와 가를 수 없거나 지금 0건이라 지킬 것이 없습니다.

- 테스트의 `std::this_thread::sleep_for`(150건, 대부분 `Test/`). 기다림 루프는 `Deadline` 으로 바꿀 수 있지만 일정한 간격으로 쉬는 테스트가 정당하게 섞여 있습니다.
- `std::function` · `std::async` · `assert` · `malloc`(구현 파일 `Memory.cpp` 밖 0건).
- 문자열 리터럴 UI 글(지역화), 게임 코드의 키 직접 확인(입력 맵).

## 2. 단위를 나눈 규칙

- 단위 사이에 겹치는 파일이 없습니다. 한 파일에 규칙이 둘이면 그 파일 전체를 한 단위가 맡습니다(`TestLoaderFuzz.cpp` 는 TestOutput).
- 단위는 `CheckFacilityBypass.toml` 을 고치지 않습니다. 줄어든 기준선은 전체 훑기가 참고로만 알리고 실패하지 않으므로, 통합이 병합 뒤 한 번에 낮춥니다(여러 단위가 같은 줄을 고쳐 충돌하지 않게).
- 첫 단계는 재측정입니다. `--list` 로 맡은 파일의 줄을 다시 보고, 이 문서의 수와 다르면 보고에 적습니다.
- 대체 API 가 없으면 호출부에 직접 짜지 않고 Core · Engine 에 하나 더한 뒤 카탈로그 표의 그 줄을 고칩니다.

## 3. 단위

### ReuseExistingFacilities.OsEntropy
random 6건. 운영체제 난수를 받는 API 가 없어 네 곳이 `std::random_device` 를 따로 엽니다. `MathUtil` 에 운영체제 난수 함수(예: `getEntropyBits64`)를 더하고 구현은 지금 `<random>` 을 쓰는 유일한 자리인 `MathUtil.cpp` 에 둡니다. 옮긴 뒤 `<random>` 은 그 파일에만 남습니다.
파일: `Source/Core/Math/MathUtil.h` · `MathUtil.cpp`, `Source/Core/UUID/UUID.cpp`, `Source/Core/Network/Connection/NetChallenge.cpp`, `Source/Core/Network/Message/NetRequest.cpp`, `Source/Core/Diagnostics/CrashContext.cpp`, `Source/Editor/Common/Workspace/EditorNetPlaySession.cpp`, `Test/TestFramework/TestSelection.cpp`(씨앗 있는 섞기는 `XorShift32`).
주의: `UUID.cpp` 는 스레드마다 장치를 둔 이유(스레드 안전 약속 없음)를 주석에 적었습니다. 새 함수가 같은 보장을 지키는지 확인합니다.

### ReuseExistingFacilities.LowLevelStderr
stdout 7건, Core 안의 `fprintf( stderr )` · `fputs`. `SW_LOW_LEVEL_ERROR` 로 옮깁니다. 이 매크로는 로거가 보고자를 등록하기 전에는 stderr 로 씁니다.
파일: `Source/Core/Container/formatString.h`, `Source/Core/Memory/Memory.cpp`, `Source/Core/Diagnostics/MemoryProfiler.cpp`, `Source/Core/Task/TaskManager.cpp`.
주의: `Memory.cpp` 와 `MemoryProfiler.cpp` 는 할당 경로입니다. 보고자가 로거로 가며 할당하면 되돌아 들어오므로, 그 경로에서 할당이 없는지 먼저 봅니다. 되돌아 들어오면 이유를 적어 허용 목록에 올리는 것이 답입니다.

### ReuseExistingFacilities.EngineStderr
stdout 10건, 엔진 · 게임 프레임워크 · 모듈 호스트 · 도구의 기동 실패와 명령줄 출력. 기동 실패는 `SW_LOW_LEVEL_ERROR` 로 옮깁니다. 사용법 글처럼 표준 출력이 계약인 출력은 Core 에 표준 출력 쓰기 API(예: `Core/Process/StandardOutput.h`)를 하나 더해 그것만 허용 목록에 둡니다.
파일: `Source/Engine/Automation/AutomationHangWatch.cpp`, `Source/Engine/Config/GameTimeScale.cpp`, `Source/Engine/Engine.cpp`, `Source/Engine/HeadlessSubsystem.cpp`, `Source/GameFramework/Base/Online/Identity/OnlinePushRelay.cpp`, `Source/GameFramework/Base/Online/Service/OnlineProtocolRegistry.cpp`, `Source/ModuleHost/ModuleImageHostSubsystem.cpp`, `Tools/OnlineLoadBot/Main.cpp`.
주의: `AutomationHangWatch.cpp` 는 실행기가 멈췄을 때 도는 감시자입니다. 로거가 멈춘 원인일 수 있으므로 로거를 거치지 않아야 하면 이유를 적어 허용 목록에 올립니다.

### ReuseExistingFacilities.TestOutput
stdout 14건과 numberParse 1건, 테스트 본문의 출력. 테스트 프레임워크에 진단 출력 API 가 있으면 그것을, 없으면 `Test/TestFramework` 에 하나 더해 씁니다. 퍼저의 대상 목록 출력은 표준 출력이 계약이므로 EngineStderr 의 표준 출력 API 를 씁니다(선행: EngineStderr).
파일: `Test/ReflectionTest/TestReflectionSerialization.cpp`, `Test/FuzzTest/LoaderFuzzer.cpp`, `Test/CoreTest/TestTestFramework.cpp`, `Test/EngineTest/Destruction/TestDestructionDamage.cpp`, `Test/EngineTest/GameFramework/Kits/Online/TestMatchQueue.cpp`, `Test/EngineTest/Resource/TestLoaderFuzz.cpp`.

### ReuseExistingFacilities.EngineNumberParse
numberParse 7건. `StringUtil::parseInt64` · `parseUint64` · `parseDouble` 로 옮깁니다. 이 함수들은 실패를 돌려주므로 지금 0 으로 넘어가던 잘못된 입력이 오류가 됩니다. 호출부마다 실패 처리를 정합니다.
파일: `Source/Engine/Profiling/ProfilerTraceExport.cpp`, `Source/Engine/Profiling/Tracy/TracyProfilerBackend.cpp`, `Source/Engine/Window/Linux/X11Window.cpp`, `Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresDriver.cpp`.

### ReuseExistingFacilities.TestNumberParse
numberParse 16건, 테스트와 테스트 실행기의 인자 · 출력 해석.
파일: `Test/TestFramework/TestRunOptions.cpp`, `Test/AppTest/TestAppSmoke.cpp`, `Test/AppTest/TestAppUI.cpp`, `Test/CoreTest/Process/TestCrashReport.cpp`, `Test/EditorTest/Common/Asset/TestSyntheticRigImport.cpp`, `Test/EditorTest/Common/Workspace/TestEditorNetPlaySession.cpp`, `Test/EngineTest/GameFramework/Kits/Network/TestNetSimLoad.cpp`.

### ReuseExistingFacilities.FileAndProcess
fileOpen 2건과 processSpawn 1건.
파일: `Source/Engine/Observability/ProcessMetrics.cpp`(`/proc` 읽기를 `FileUtil::readTextFile` 로), `Test/EngineTest/Resource/TestResource.cpp`(`std::ifstream`), `Source/Core/File/Linux/LinuxFileDialog.cpp`(`popen` 을 `Process::execute` 의 출력 델리게이트로).
주의: `/proc` 파일은 크기를 0 으로 알립니다. `FileUtil::readTextFile` 이 크기만큼만 읽으면 빈 글이 됩니다. 먼저 끝까지 읽는지 확인합니다. `TestResource.cpp` 는 파일을 열어 둔 채 잠그는 테스트일 수 있으므로, 그렇다면 이유를 적어 허용 목록에 올립니다.

### ReuseExistingFacilities.DeprecatedCRuntime
선행: OsEntropy · LowLevelStderr · EngineStderr · TestOutput · EngineNumberParse · TestNumberParse · FileAndProcess.
형식으로 막는 길을 시험합니다. C 함수(`printf` · `fprintf` · `puts` · `fopen` · `rand` · `atoi` · `strtol` · `system` · `popen`)는 pch 뒤에 `[[deprecated]]` 를 붙여 다시 선언할 수 있습니다. 그다음 `-Werror=deprecated-declarations` 를 엔진 타깃에 걸면 컴파일러가 막습니다. 허용 목록 파일은 그 줄만 경고를 끕니다.
먼저 확인할 것은 셋입니다. ① Windows UCRT 의 `printf` 는 인라인 정의라 재선언이 받아들여지는지 봅니다. ② glibc 의 `_FORTIFY_SOURCE` 가 이름을 매크로로 바꾸는지 봅니다. ③ pch 뒤에 include 되는 서드파티 헤더가 이 이름을 쓰는지 봅니다. 셋을 `syntax-check` 로 두 플랫폼에서 확인합니다.
되면 게이트에서 C 이름을 빼고 `std::` 이름(`std::cout` · `std::ifstream` · `std::mt19937` · `std::random_device` · `std::stoi`)만 남깁니다. `std` 이름공간의 재선언은 미정의 동작이므로 그 이름들은 게이트가 계속 맡습니다. 안 되면 이유를 결정 기록(`docs/decisions/5-3_CorePlatform.md`)에 한 줄 남깁니다.
파일: `Source/Core/Common/StdHeaders.h`, 새 헤더 하나, `Source/Core/CMakeLists.txt` 의 경고 플래그, `Scripts/lint/gate/CheckFacilityBypass.py` · `rules/CheckFacilityBypass.toml`.
