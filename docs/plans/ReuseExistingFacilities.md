# 이미 있는 기능으로 옮기기

> 상태: 구현중 · 선행: 없음 · 영역: quality · 옛번호: 없음 · 진행: 6/13

> 질문: Core · Engine 에 이미 있는 기능을 두고 직접 구현하거나 C 런타임을 직접 부르는 곳은 몇 군데이고, 어떤 순서로 옮기는가?
> 독자: 아래 단위를 맡는 작업자와 단위를 나눠 주는 통합 담당.
> 결론: 게이트가 세는 직접 호출은 64건에서 22건으로 줄었고 남은 것은 corebase · jsonwriter 작업자의 파일이다. 남은 새 단위는 GameDataXML 별칭 · split 정본 · 경로 정본 셋이다.

쓸 것과 쓰지 말 것의 표는 [ExistingFacilities](../agents/ExistingFacilities.md) 한 곳에 있습니다. 새로 늘지 않게 막는 게이트는 `Scripts/lint/gate/CheckFacilityBypass.py` 이고, 규칙마다 형식으로 막지 못하는 이유(`whyNotType`)를 적었습니다. 기준선과 허용 목록은 `Scripts/lint/rules/CheckFacilityBypass.toml` 에 있습니다.

## 1. 지금 위반 수

`py -3 Scripts/lint/gate/CheckFacilityBypass.py --list` 로 셉니다. 허용 목록(그 기능을 구현하는 파일과 이유가 적힌 자리)은 세지 않습니다.

| 규칙 | 쓸 것 | 처음 | 지금 | 남은 파일 |
| --- | --- | --- | --- | --- |
| stdout | `SW_LOG_*` · `SW_LOW_LEVEL_*` · `StandardOutput` · `test::printTestNote` | 31 | 10 | Memory · TaskManager · AutomationHangWatch · GameTimeScale · Engine · OnlinePushRelay · OnlineProtocolRegistry · ModuleImageHostSubsystem |
| random | `MathUtil::getRandom*` · `XorShift32` | 6 | 5 | UUID · CrashContext · NetChallenge · NetRequest · EditorNetPlaySession |
| fileOpen | `FileUtil::read*` · `readStreamTextFile` | 2 | 0 | 없음 |
| processSpawn | `sw::Process` | 1 | 0 | 없음 |
| numberParse | `StringUtil::parse*` | 24 | 7 | ProfilerTraceExport · TracyProfilerBackend · X11Window · PostgresDriver |
| 합 | | 64 | 22 | |

남은 22건은 모두 다른 작업자가 지금 고치는 파일입니다. corebase 가 `SecureRandom`(random 다섯), 낮은 층 보고(stdout 여섯), 숫자 파싱(numberParse 일곱)을 옮기고 있고, `AutomationHangWatch.cpp` 는 jsonwriter 가 고칩니다. 병합 뒤 통합이 기준선을 0 에 가깝게 낮춥니다.

게이트로 만들지 않고 카탈로그에만 둔 것은 아래와 같습니다. 모양만으로는 정당한 코드와 가를 수 없거나 지금 0건이라 지킬 것이 없습니다.

- 테스트의 `std::this_thread::sleep_for`(150건, 대부분 `Test/`). 기다림 루프는 `Deadline` 으로 바꿀 수 있지만 일정한 간격으로 쉬는 테스트가 정당하게 섞여 있습니다.
- `std::function` · `std::async` · `assert` · `malloc`(구현 파일 `Memory.cpp` 밖 0건).
- 문자열 리터럴 UI 글(지역화), 게임 코드의 키 직접 확인(입력 맵).

## 2. 끝난 단위에서 정한 것

- **C 함수의 `[[deprecated]]` 재선언은 쓰지 않습니다**(DeprecatedCRuntime). 시험 결과와 이유는 [결정 기록 5-3](../decisions/5-3_CorePlatform.md) 에 있습니다. 그래서 C 이름도 게이트가 계속 맡습니다. Windows 에서 이미 막혀 glibc 의 `_FORTIFY_SOURCE` 와 서드파티 헤더는 보지 않았습니다.
- 명령줄 도구의 결과 글은 `Core/Process/StandardOutput.h` 로, 테스트 본문의 진단 줄은 `test::printTestNote`(`Test/TestFramework/TestReport.h`)로 씁니다.
- 크기를 0 으로 알리는 의사 파일(`/proc`)은 `FileUtil::readStreamTextFile` 로 읽습니다. 같은 일을 하는 `PosixProcess.cpp` 의 `/proc/self/status` 읽기는 Process 층(6)이 File 층(8)을 볼 수 없어 그대로 둡니다.
- 서식 오류를 알리는 `formatString.h` 와 CRT 누수 보고인 `MemoryProfiler.cpp` 는 로거를 쓸 수 없어 허용 목록에 올렸습니다. 잠긴 파일을 흉내 내는 `TestResource.cpp` 의 `std::ifstream` 도 같습니다.
- 테스트 섞기는 `std::shuffle` 대신 `XorShift32` 피셔-예이츠입니다. `std::shuffle` 은 표준 라이브러리마다 알고리즘이 달라 같은 씨앗이 플랫폼마다 다른 순서를 돌았습니다.
- 실수 목록 읽기는 `StringUtil::parseExactFloats` 하나입니다(FloatListParse). 각 보간은 `MathUtil::lerpAngle`, 각 감기는 `MathUtil::wrapAngle` 하나입니다(AngleLerp).

## 3. 단위를 나눈 규칙

- 단위 사이에 겹치는 파일이 없습니다. 한 파일에 규칙이 둘이면 그 파일 전체를 한 단위가 맡습니다.
- 줄인 단위가 기준선을 낮춥니다. 여러 작업자가 같은 줄을 고치면 병합 때 작은 값을 남깁니다.
- 첫 단계는 재측정입니다. `--list` 로 맡은 파일의 줄을 다시 보고, 이 문서의 수와 다르면 보고에 적습니다.
- 대체 API 가 없으면 호출부에 직접 짜지 않고 Core · Engine 에 하나 더한 뒤 카탈로그 표의 그 줄을 고칩니다.

## 4. 남은 단위

### ReuseExistingFacilities.OsEntropy
random 다섯. corebase 가 Core/Math 에 `SecureRandom` 을 만들고 있으니 그것을 씁니다. 별도 API 를 더하지 않습니다.
파일: `Source/Core/UUID/UUID.cpp`, `Source/Core/Network/Connection/NetChallenge.cpp`, `Source/Core/Network/Message/NetRequest.cpp`, `Source/Core/Diagnostics/CrashContext.cpp`, `Source/Editor/Common/Workspace/EditorNetPlaySession.cpp`.

### ReuseExistingFacilities.LowLevelStderr
stdout 둘(`Memory.cpp` · `TaskManager.cpp`)을 `SW_LOW_LEVEL_ERROR` 로 옮깁니다. corebase 작업 트리에서 진행 중입니다.

### ReuseExistingFacilities.EngineStderr
stdout 여덟. 기동 실패는 `SW_LOW_LEVEL_ERROR` 로 옮기고, `AutomationHangWatch.cpp` 는 멈춘 실행기의 감시자라 로거를 거치면 안 되면 이유를 적어 허용 목록에 올립니다. `HeadlessSubsystem.cpp` 와 `Tools/OnlineLoadBot/Main.cpp` 는 끝났습니다.
파일: `Source/Engine/Automation/AutomationHangWatch.cpp`(jsonwriter), `Source/Engine/Config/GameTimeScale.cpp`, `Source/Engine/Engine.cpp`, `Source/GameFramework/Base/Online/Identity/OnlinePushRelay.cpp`, `Source/GameFramework/Base/Online/Service/OnlineProtocolRegistry.cpp`, `Source/ModuleHost/ModuleImageHostSubsystem.cpp`(corebase · errh).

### ReuseExistingFacilities.EngineNumberParse
numberParse 일곱. corebase 브랜치(`wt/corebase`)가 이미 `StringUtil::parse*` 로 옮겼습니다. 병합 뒤 기준선만 낮춥니다.

### ReuseExistingFacilities.GameDataXMLAlias
`GameDataXML` 의 읽기 별칭(`parseFloat2` · `readOptionalFloat2` 같은 이름)은 엔진의 `XMLReadUtil` 과 같은 일을 하고 호출부가 약 850 곳입니다. 먼저 `GameDataXML` 에만 있는 읽기를 `XMLReadUtil` 로 올리고, 다음에 호출부를 옮겨 별칭을 지웁니다. 호출부가 많아 한 작업자가 이 일만 맡습니다.
첫 단계: `git grep -c "GameDataXML::"` 로 호출부를 다시 세고, `XMLReadUtil` 에 없는 함수 목록을 만듭니다.

### ReuseExistingFacilities.SplitCanonical
글을 구분자로 나누는 일은 `StringUtil::forEachToken` · `string_splitter` 가 있는데 호출부가 `find` · `substr` 루프를 손으로 씁니다. 정본을 `StringUtil::split`(벡터를 돌려주는 형태) 하나로 정하고 손 루프를 옮깁니다. 감사 보고 R9 의 목록으로 재측정하고 시작합니다.

### ReuseExistingFacilities.PathCanonical
경로 붙이기 · 확장자 · 구분자 바꾸기를 호출부가 문자열로 직접 합니다. 정본은 `FileUtil::joinPath` · `getExtension` · `normalizeSeparators` 입니다. 감사 보고 R10 의 목록으로 재측정하고 옮깁니다.
