# 이미 있는 기능으로 옮기기

> 상태: 구현중 · 선행: 없음 · 영역: quality · 옛번호: 없음 · 진행: 9/16

> 질문: Core · Engine 에 이미 있는 기능을 두고 직접 구현하거나 C 런타임을 직접 부르는 곳은 몇 군데이고, 어떤 순서로 옮기는가?
> 독자: 아래 단위를 맡는 작업자와 단위를 나눠 주는 통합 담당.
> 결론: 게이트가 세는 직접 호출은 64건에서 22건으로 줄었고, 남은 22건은 다른 작업자 브랜치에 이미 고쳐져 병합을 기다린다. 남은 단위는 병합 뒤 정리 넷, 게이트 합치기, 16진 쓰기, containsName 별칭이다.

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

남은 22건은 `wt/corebase` · `wt/errh` 가 이미 커밋했다. 운영체제 난수는 `SecureRandom`, stderr 는 낮은 층 보고, 숫자 읽기는 `StringUtil::parse*` 로 옮겼다. 이 브랜치에는 그 변경이 없어 기준선을 낮추면 게이트가 실패한다. 그래서 통합이 병합한 뒤 `--list` 로 다시 세어 낮춘다.

게이트로 만들지 않고 카탈로그에만 둔 것은 아래와 같습니다. 모양만으로는 정당한 코드와 가를 수 없거나 지금 0건이라 지킬 것이 없습니다.

- 테스트의 `std::this_thread::sleep_for`(150건, 대부분 `Test/`). 기다림 루프는 `Deadline` 으로 바꿀 수 있지만 일정한 간격으로 쉬는 테스트가 정당하게 섞여 있습니다.
- `std::function` · `std::async` · `assert` · `malloc`(구현 파일 `Memory.cpp` 밖 0건).
- 문자열 리터럴 UI 글(지역화), 게임 코드의 키 직접 확인(입력 맵).

## 2. 끝난 단위에서 정한 것

- **C 함수의 `[[deprecated]]` 재선언은 쓰지 않습니다**(DeprecatedCRuntime). 시험 결과와 이유는 [결정 기록 5-3](../decisions/5-3_CorePlatform.md) 에 있습니다. 그래서 C 이름도 게이트가 계속 맡습니다. Windows 에서 이미 막혀 glibc 의 `_FORTIFY_SOURCE` 와 서드파티 헤더는 보지 않았습니다.
- 명령줄 도구의 결과 글은 `Core/Process/StandardOutput.h` 로, 테스트 본문의 진단 줄은 `test::printTestNote`(`Test/TestFramework/TestReport.h`)로 씁니다.
- 크기를 0 으로 알리는 의사 파일(`/proc`)은 `FileUtil::readStreamTextFile` 로 읽습니다. `PosixProcess.cpp` 의 `/proc/self/status` 직접 `::open` 은 그대로 둡니다. Process 층(6)은 File 층(8)을 볼 수 없고, 쓰는 곳이 디버거 감지 한 곳뿐이라 FileUtil 을 아래 층으로 내리는 비용이 더 큽니다.
- 서식 오류를 알리는 `formatString.h` 와 CRT 누수 보고인 `MemoryProfiler.cpp` 는 로거를 쓸 수 없어 허용 목록에 올렸습니다. 잠긴 파일을 흉내 내는 `TestResource.cpp` 의 `std::ifstream` 도 같습니다.
- 테스트 섞기는 `std::shuffle` 대신 `XorShift32` 피셔-예이츠입니다. `std::shuffle` 은 표준 라이브러리마다 알고리즘이 달라 같은 씨앗이 플랫폼마다 다른 순서를 돌았습니다.
- 실수 목록 읽기는 `StringUtil::parseExactFloats` 하나입니다(FloatListParse). 각 보간은 `MathUtil::lerpAngle`, 각 감기는 `MathUtil::wrapAngle` 하나입니다(AngleLerp).
- **데이터 XML 손 읽기의 정본은 `XMLReadUtil` 입니다**(GameDataXMLAlias). `GameDataXML` 에는 카탈로그 · 리플렉션 묶음과 커브 읽기만 남았습니다. 커브(`readFloatCurve`)는 엔진의 Serialization 과 Utility 가 같은 티어라 어느 쪽에도 둘 수 없어 GameFramework 에 둡니다. 속성 유무는 `XMLNode::hasAttribute` 입니다.
- **글 나누기의 정본은 `StringUtil::split` 입니다**(SplitCanonical). 시작 위치를 받는 손 루프는 corebase 가 고치는 세 파일만 남았습니다. 한 번 가르는 키=값 읽기는 corebase 의 `StringUtil::trySplitKeyValue` 가 맡습니다.
- **경로의 정본은 `FileUtil` 입니다**(PathCanonical). 루트 아래 상대 경로는 `tryMakePathUnderRoot` 입니다. `makeRelativePath` 는 `std::filesystem::relative` 라 루트 밖이면 `../` 를 만듭니다. 감사 목록 가운데 아래 다섯은 정당한 쓰임이라 두었습니다.
  - Logger: Log 층이라 File 층을 못 봅니다.
  - ResourcePakReader: 폴더 경계 규칙이 다릅니다.
  - AssetFormat: 경로가 아니라 확장자 이름 글입니다.
  - EditorLogCommands: 로그 줄 안에서 경로를 찾습니다.
  - AssetImportStamp: 조상 키를 걸어 올라갑니다.
- PackCooker 의 `splitPath` 는 이름이 FileUtil 과 같아도 경로를 마디 목록으로 나누는 다른 일입니다.

## 3. 단위를 나눈 규칙

- 단위 사이에 겹치는 파일이 없습니다. 한 파일에 규칙이 둘이면 그 파일 전체를 한 단위가 맡습니다.
- 줄인 단위가 기준선을 낮춥니다. 다른 브랜치가 줄인 몫은 병합 뒤 통합이 낮춥니다.
- 첫 단계는 재측정입니다. `--list` 로 맡은 파일의 줄을 다시 보고, 이 문서의 수와 다르면 보고에 적습니다.
- 대체 API 가 없으면 호출부에 직접 짜지 않고 Core · Engine 에 하나 더한 뒤 카탈로그 표의 그 줄을 고칩니다.

## 4. 남은 단위

### ReuseExistingFacilities.OsEntropy
random 다섯. `wt/corebase` 가 `SecureRandom` 으로 옮겼다. 병합 뒤 기준선만 낮춘다.

### ReuseExistingFacilities.LowLevelStderr
stdout 둘(`Memory.cpp` · `TaskManager.cpp`). `wt/corebase` 가 `SW_LOW_LEVEL_ERROR` 로 옮겼다. 병합 뒤 기준선만 낮춘다.

### ReuseExistingFacilities.EngineStderr
stdout 여덟. `wt/corebase` · `wt/errh` 가 옮겼다. `AutomationHangWatch.cpp` 는 corebase 의 게이트가 이유를 적어 허용했다. 병합 뒤 기준선만 낮춘다.

### ReuseExistingFacilities.EngineNumberParse
numberParse 일곱. `wt/corebase` 가 `StringUtil::parse*` 로 옮겼다. 병합 뒤 기준선만 낮춘다.

### ReuseExistingFacilities.GateMerge
`wt/corebase` 의 `CheckCoreBypassCalls`(parse · random · stderr)와 이 브랜치의 `CheckFacilityBypass`(stdout · random · fileOpen · processSpawn · numberParse)가 같은 일을 한다. 병합 뒤 `CheckFacilityBypass` 하나로 합친다. corebase 쪽의 모양(`random_device` · `stderr` · `from_chars`)과 허용 목록 줄을 규칙 표로 옮기고 다른 게이트를 지운다.

### ReuseExistingFacilities.HexEncode
`"0123456789abcdef"` 표를 둔 손 16진 쓰기 21 곳을 `Core/String/HexUtil.h` 로 옮긴다.
- Core: UUID · LogContext · CrashContext · ModuleBuildID · Color
- Engine: SceneStateHash · ShaderCookStamp · ProfilerTraceExport · SerializeReflectAny
- GameFramework: HTTPTypes · LedgerTypes · OpenSSLNetSecurityProvider · ServiceKeyUtil · LoginStoreLogic · GuildService
- Editor: ModelImportMaterial · EditorScenarioSteps
- Test: OnlineMessageTestUtil

Core/Log(티어 5)는 Core/String(티어 6)을 못 보므로 LogContext 는 허용 목록이 맞는지 먼저 본다.

### ReuseExistingFacilities.ContainsNameAlias
`AppearanceXMLUtil::containsName` 은 `VectorUtil::contains` 의 별칭이다. 남은 다섯 호출(AppearanceDatabase 둘 · AppearanceResolver · AppearanceSelection · CharacterAppearanceComponent)은 목록에 넣기 전 중복 검사라 그대로 옮기면 `CheckContainsPushBack` 이 잡는다. 별칭이 게이트를 비켜 가게 하고 있었다. 다섯을 넣은 순서를 지키는 집합 곁 목록으로 고친 뒤 별칭을 지운다. AppearanceResolver 의 숨김 영역은 순서에 뜻이 없는지 먼저 본다.
