# hostgpu 의 AppBin 잠금 풀기 — 실행별 쓰기 위치 나누기

> 상태: 설계 · 선행: 없음 · 영역: quality · 옛번호: 없음 · 진행: 0/4

> 질문: `AppTest_HostOnly` 조각과 `QaGoldenImages` 가 `AppBin` 잠금으로 서로 직렬인 것을 풀려면 무엇이 먼저 바뀌어야 하는가?
> 독자: GPU 검증 구조를 고치는 작업자와 통합 담당.
> 결론: 잠금은 `Bin` 이 아니라 **에디터 시나리오가 지우고 다시 만드는 고정 이름 폴더**(`Resource/scratch/<이름>`, `Saved/Automation/<시나리오 이름>/`)를 지킨다. 지금 풀면 두 슬롯이 서로의 파일을 지우므로 풀지 않는다. 쓰기 위치를 실행별로 나누는 단위 H1 ~ H3 이 끝난 뒤 H4 에서 잠금을 푼다.

2026-10-25 코드 읽기로 정리했습니다. 이 문서의 표는 코드에서 찾은 쓰기 위치이고, 실행으로 확인한 것은 없습니다(작업자는 빌드 · 실행을 하지 않습니다). 빠진 위치는 H1 의 스냅샷 점검이 잡습니다.

**H1 코드는 있고 실행이 남았습니다(통합에서 확인):** 매니페스트 id `host-write-snapshot`. 도구는 `Scripts/qa/WriteSnapshot.py`, 알려진 위치 표는 `Test/AppTest/HostWriteLocations.json`(2절 표와 같은 W 번호)이다. 표에 없는 쓰기가 나오면 이 문서 2절과 그 JSON 에 같이 적는다. 점검하지 않는 위치: W9 `Bin/Modules` 섀도 복사본(pid 로 구분됨), W12 `GoldenDiff` · `Golden`(`Bin` 밖).

## 1. 지금 구조

- `Test/TestPolicy.json` 의 `AppTest.hostResourceLock = "AppBin"` 이 `AppTest_HostOnly_Shard1..5` 와 Debug 의 `AppTest_Race` 에 `RESOURCE_LOCK` 을 단다. `Test/PythonTest/CMakeLists.txt` 의 `QaGoldenImages` 도 같은 잠금이다.
- ctest 는 같은 `RESOURCE_LOCK` 이름의 항목을 동시에 돌리지 않는다. 그래서 `gpuSlots` = 2 인데도 위 항목들은 한 번에 하나만 돈다. EngineTest 의 호스트 조각은 잠금이 없어 이들과 겹친다.
- Debug `hostgpu` 한 번은 2,254 s 이고 그중 에디터 시나리오 네 백엔드가 1,657 s(497 + 444 + 395 + 321)다(`hostCaseCosts`, 2026-10-16 측정). 시나리오가 늘수록 이 직렬 시간이 선형으로 늘고, 통합이 `hostgpu` 를 한 번 돌리는 동안 다른 작업자의 GPU 실행도 공유 GPU 잠금(`shared-lock gpu`) 슬롯 하나를 오래 쥔다.
- 잠금을 풀고 슬롯 둘로 돌리면 LPT 로 나눈 조각 다섯이 약 1,150 ~ 1,250 s(합 2,254 s 의 절반 남짓)에 끝나리라 계산한다. 측정한 값이 아니다.

## 2. 잠금이 지키는 것 — 코드에서 찾은 쓰기 위치

`App.exe` 는 테스트의 작업 폴더(`build/<프리셋>/Bin`)를 그대로 물려받고(`AppTestUtil::runApp` 은 `ProcessOptions::_workingDirectory` 를 비운다), 그 작업 폴더에서 위로 올라가 `Resource/` 를 찾는다. 상대 경로 `Saved/…` 는 그래서 `Bin/Saved/…` 다.

| # | 위치 | 쓰는 쪽 | 실행마다 이름이 다른가 | 동시 실행 위험 |
|---|---|---|---|---|
| W1 | `Resource/scratch/<이름>/` (저장소 `Resource` 안, git 무시) | 시나리오 `<Scratch from=… as="scratch/assetmanage">` · `scratch/animgraphlayers` (`AutomationScratch::prepare`) | 아니오. 고정 이름이고 시작할 때 `removeDirectory` 로 지운 뒤 다시 만든다 | 높음. 네 백엔드 케이스가 겹치면 다른 프로세스가 쓰는 폴더와 `.meta` guid 를 지운다. 콘텐츠 브라우저 색인도 다른 프로세스의 변경을 따라간다 |
| W2 | `Bin/Saved/Automation/<시나리오 이름>/` (`_outputDirectory`) 와 시나리오 리터럴 `Saved/Automation/editor.*/…` (35 곳) | `AutomationRunner`, `EditorDevCommands`(`asset.open` · `scene.saveAs` · `asset.removeTemp` 이 `Saved/Automation/` 아래만 받는다), 스크린샷 · 캡처 단계 | 아니오. 시나리오 이름이고 백엔드가 이름에 없다 | 높음. 같은 시나리오를 다른 백엔드 케이스가 동시에 돌리면 같은 폴더에 쓰고 지운다 |
| W3 | `Bin/Saved/Automation/<이름>_<백엔드>.log` · `.json`, `chain_<이름>_<백엔드>.*` (`AppTestUtil::runScenarioArgument`) | `AppTestUtil` | 백엔드와 이름은 다르다. 그러나 같은 시나리오를 도는 케이스가 둘이다 | 중간. `EveryScenarioPassesOnEveryBackend` 와 `EveryGamePackScenarioPassesOnEveryBackend` 는 활성 게임 팩 시나리오를 둘 다 돌고, `AppUITest.DemoScreenMatchesAcrossBackends` 는 `uidemo` 를 다시 돈다. `AppTest_Race` 는 `HostOnly` 와 같은 케이스(`ChainedScenariosShareOneProcess` 등)를 한 번 더 돈다. 샤드가 다르면 같은 파일을 동시에 쓴다 |
| W4 | `Bin/Saved/Automation/EditorState/<묶음>_<백엔드>/` (`-gv_editorStateDir`) | `TestAppScenario.cpp` 의 `makeEditorArguments` · `runEditorGroups` · `runEditorCase` | 예. 묶음과 백엔드가 이름에 있다 | 낮음. 이미 갈라져 있다 |
| W5 | 프로젝트 루트 `Saved/Editor/` (레이아웃 · 테마 · 설정 · 단축키 · NetPlay 프리셋) | `-EnableEditor` 로 띄운 App 중 `-gv_editorStateDir` 가 없는 것: `AppSmokeTest.EditorModeStartsAndExitsCleanly` · `EditorSelfTestsPassInsideTheEditor` · 에디터 메모리 · 시작 씬 · 레지스트리 덤프 케이스 | 아니오 | 높음. 두 에디터가 같은 JSON 을 쓰면 깨진 파일 복구 경로가 경고를 낸다(스모크는 `[Error]` 0 건을 단언한다) |
| W6 | 프로젝트 루트 `Saved/DDC`, `CookStore`(`Saved/` 아래) | 쿠킹 | 공유 캐시가 목적이다 | 낮음. 키로 찾는 캐시다. 동시 쓰기의 원자성은 확인하지 않았다 |
| W7 | `Bin/Saved/ShaderCache/…`, `Bin/Saved/ShaderCache/vk_pipeline_cache.bin` | `ShaderCache.cpp`, `VulkanRHIDeviceInit.cpp` | 공유 캐시가 목적이다 | 낮음. SmokeTest 머리말이 Vulkan 캐시는 원자적으로 쓴다고 적었다. 셰이더 캐시도 같은지 확인하지 않았다 |
| W8 | `Bin/Saved/Logs/LOG_<시각>_<세션>.txt` + 크래시 리포트 | `FileLogOutput` (실행 파일 폴더 기준) | 예. 세션 ID 가 이름에 있다 | 없음 |
| W9 | `Bin/Modules/<모듈>_temp_p<pid>_…/` 섀도 복사본 | `ModuleShadowCopy` (핫 리로드 시나리오) | 예. pid 가 이름에 있다 | 없음. 남은 사본은 pid 로 치운다 |
| W10 | 시나리오가 쓰는 그 밖의 `Saved/` 하위: `Saved/BugIt/`, `Saved/Profiler/ProfilerCapture.txt`, `Saved/Traces/`, `Saved/StateHashes/netsync`, `Saved/RenderDoc/`, `Saved/Packages/`, 스크린샷 기본 경로 `Saved/Screenshots/` | `bugit` · `profilercapture` · `netsync` · `renderdocbutton` · `packaging` 시나리오, `ScreenshotPathUtil`, `BugItReport` | 대부분 아니오. 시각이 이름에 든 것만 예 | 중간. 고정 이름은 W2 와 같은 위험이다 |
| W11 | `%LOCALAPPDATA%/SWEngine/<game>/` (`UserDataPath`: 설정 · 로컬 저장 · 장치 키) | 게임 코드가 저장하는 시나리오 | 아니오 | 알 수 없음. 저장하는 시나리오가 있는지 코드로 확인하지 않았다 |
| W12 | `Test/AppTest/Golden/…`(기준 갱신 모드일 때만), `build/<프리셋>/GoldenDiff/<이름>.ppm` (실패 시 사본) | `AppSmokeTest` 의 골든 케이스, `QaGoldenImages` (`--diff-dir`) | 아니오 | 낮음. 기준 갱신(`bUpdate`)은 사람이 켠 실행에서만. 실패 사본은 이름이 같다 |
| W13 | 임시 폴더 `%TEMP%/sw_<pid>/…` | `test::makeTempPath` · `makeTempDirectory` (스모크 스크린샷 · 보고서), `QaGoldenImages` 의 `tempfile` | 예 | 없음. `QaGoldenImages` 는 App 이 쓰는 위치가 W7 · W8 · W9 뿐이다 |

읽기만 하는 공유 자원은 문제가 없습니다. `Resource` 의 추적 파일, `Bin/Modules` 의 원본 DLL, 쿠킹한 셰이더, `Test/AppTest/Golden` 의 기준 이미지입니다.

골든 이미지 비교는 `Bin` 상대 경로를 가정하지 않습니다. 기준은 `Test/AppTest/Golden` 을 작업 폴더에서 위로 올라가며 찾고(`findGoldenDirectory`), 캡처는 `test::makeTempPath` 나 `Saved/Automation/<시나리오>/` 로 간다. 단 `ShippingFrameMatchesGoldenImage` 는 고정 경로 `Saved/Automation/game.empty.shippingframe/shippingframe.ppm` 을 읽으므로 W2 가 갈라지면 읽는 경로도 같이 바꿔야 한다.

## 3. 풀지 않은 이유

잠금을 풀려면 W1 · W2 · W3 · W5 · W10 이 실행마다 달라야 합니다. 이 가운데 W2 · W10 의 `Saved/…` 는 엔진이 상대 경로로 직접 쓰고 시나리오 35 곳이 같은 문자열을 리터럴로 가집니다. 이것을 한 번에 돌리면서 빌드 · 실행 없이 정확함을 보증할 수 없습니다. 또 W11 처럼 쓰는 시나리오가 있는지 모르는 위치가 남아 있습니다. 확인하지 못한 위치가 남으면 풀지 않는다는 지시에 따라 이 문서만 남깁니다.

잠금을 좁히는 것(예: 에디터 케이스만 잠그기)은 이득이 작습니다. 에디터 네 케이스가 1,657 s 로 대부분이고 네 케이스끼리 W1 · W2 로 서로 겹치므로, 나머지 약 600 s 만 겹쳐도 2,254 s 가 1,700 s 안팎으로 줄 뿐입니다. 샤드가 비용으로 나뉘어 케이스별 잠금을 줄 수도 없습니다.

## 4. 설계

### 4.1 실행별 쓰기 루트 하나로 모은다

쓰기 위치마다 인자를 따로 만들지 않고 **실행 루트 하나**를 둔다. 상대 `Saved/…` 가 엔진 안팎에서 작업 폴더에 묶여 있으므로, 가장 적게 고치는 구조는 다음 둘 중 하나다.

- **A. 실행마다 작업 폴더를 나눈다(권장).** `AppTestUtil` 이 App 을 `%TEMP%/sw_<pid>/app_<n>/` 에서 띄우고(`ProcessOptions::_workingDirectory`), 엔진은 프로젝트 루트를 작업 폴더에서 찾지 않고 `-gv_projectRoot=<절대 경로>`(또는 환경 변수) 로 받는다. 값이 없으면 지금처럼 위로 올라가 찾는다(폴백이 아니라 기존 동작의 한 갈래다). 그러면 `Saved/Automation` · `Saved/BugIt` · `Saved/Screenshots` · 시나리오 리터럴이 코드 변경 없이 실행별 폴더로 간다. 대가: 작업 폴더 상대 캐시(`Saved/ShaderCache`, W7)가 매 실행마다 비므로, ShaderCache 와 Vulkan 파이프라인 캐시 경로를 작업 폴더가 아닌 프로젝트 루트 기준으로 옮겨야 한다. 로그(W8)와 섀도 복사본(W9)은 실행 파일 폴더 기준이라 그대로다.
- **B. `Saved` 루트를 인자로 바꾼다.** `path::kSavedFolder` 를 쓰는 곳 여섯과 `Saved/Automation/` 을 상대 경로로 검사하는 에디터 명령을 한 함수(`SavedPaths`)로 모으고 `-gv_savedDir` 로 바꾼다. 시나리오 리터럴은 그대로 두고 명령이 그 함수로 푼다. A 보다 엔진 변경이 크지만 작업 폴더를 건드리지 않는다.

상용 엔진과 비교: 자동화 러너(언리얼 Gauntlet 등)는 인스턴스마다 저장 · 로그 폴더를 따로 주어 같은 프로젝트를 병렬로 돌리는 구조라고 알고 있다. 같은 방향이다. 인자 이름은 확인하지 않았다.

### 4.2 scratch 이름은 실행마다 다르게

W1 은 쓰기 루트로 풀리지 않습니다. `Resource/scratch` 가 저장소 `Resource` 안이기 때문입니다. 시나리오의 `<Scratch as="scratch/assetmanage">` 를 실행 식별자가 붙은 이름으로 바꿔야 합니다. `AutomationScratch::prepare` 가 `as` 에 실행 식별자(`-scenario-instance=<id>`)를 덧붙이고 같은 시나리오의 `scratch/assetmanage/…` 리소스 id 를 읽을 때 같은 규칙으로 바꾸는 방법이 가장 작다. 대안으로 scratch 를 `Resource` 밖 디스크 도메인(`ResourcePaths::mountDiskDomain`)으로 옮기는 방법이 있으나, 콘텐츠 브라우저가 `Resource` 를 훑어 만드는 역색인에 들어가지 못해 `assetmanage` 가 검사하는 동작이 바뀐다.

### 4.3 에디터 상태 폴더는 입구에서 붙인다

W5 는 `AppTestUtil::makeAppCommand` 가 인자에 `-EnableEditor` 가 있고 `-gv_editorStateDir` 가 없으면 실행별 임시 폴더를 붙이게 하면 풀립니다(`#if !defined( SW_SHIPPING )`. 배포본은 `gv_` 를 몰라 읽기 오류 11 로 끝난다). 입구가 하나라 케이스가 빠뜨릴 수 없다. `EditorSelfTestsPassInsideTheEditor` 가 검사하는 `Config/Editor/imgui.ini` 불변은 그대로 유효하다.

## 5. 단위

| 단위 | 내용 | 건드릴 폴더 | 선행 | 검증 | 측정 |
|---|---|---|---|---|---|
| H1 | 쓰기 위치 스냅샷 점검. 테스트가 App 한 번을 돌리기 전후로 `Bin/Saved`, `Resource`, 프로젝트 루트 `Saved`, `%LOCALAPPDATA%/SWEngine`, `%TEMP%` 의 파일 목록(경로 · 크기 · 수정 시각)을 비교해 새로 쓰였거나 바뀐 경로를 출력한다. 시나리오마다(엔진 · 게임 팩 · 에디터) 돌려 2절 표에 없는 위치를 찾는다 | `Test/AppTest`, `Scripts/qa` | 없음 | 통합 빌드 후 hostgpu 한 번 | 불필요 |
| H2 | 실행별 작업 폴더(4.1 A 또는 B 중 H1 결과로 고른다)와 ShaderCache 경로 이동, `AppTestUtil` 이 만드는 상대 경로(`makeReportPath`, `ShippingFrameMatchesGoldenImage`, `AppUITest::kOutputFolder`)를 실행 폴더 기준으로 바꾼다 | `Source/Engine/Resource`, `Source/Engine/Graphics`, `Test/AppTest`, `Scripts/qa` | H1 | `-L hostgpu` Shipping 전체, `-L nogpu`, `-L lint` | 필요합니다. 첫 실행의 셰이더 컴파일 시간이 늘지 않았는지 `AppSmokeTest.EveryBackendStartsRendersAndExitsCleanly` 의 시간을 전후로 비교 |
| H3 | `<Scratch>` 이름에 실행 식별자(4.2)와 `makeAppCommand` 의 에디터 상태 폴더 자동 부착(4.3) | `Source/Engine/Automation`, `Test/AppTest`, `Resource/engine/automation/editor` | H1 | `assetmanage` · `animgraphlayers` 를 네 백엔드에서 동시에 돌려 통과 | 불필요 |
| H4 | `hostResourceLock` · `RESOURCE_LOCK AppBin` 을 지우고 `_hostShardsReason` · `_gpuSlotsReason` 을 고친다. 남은 공유 위치가 있으면 그 잠금만 남기고 `_…Reason` 에 사유를 적는다 | `Test/TestPolicy.json`, `Test/PythonTest/CMakeLists.txt`, `cmake/Engine/TestTargets.cmake` | H2, H3 | 아래 6절 | 필요합니다. Debug `hostgpu` 의 시간(기준 2,254 s) |

## 6. 풀 때 통합이 확인할 것

GPU 경합은 프레임 시간과 타이밍에 기대는 단언, 골든 이미지, 시한이 있는 시나리오를 흔들 수 있습니다. `gpu-verifier` 가 다음을 슬롯 둘로 동시에 돌려 잠금이 있던 결과와 같은지 봅니다.

1. 에디터 시나리오 두 백엔드를 동시에(`EditorScenariosPassOnOpenGL` + `EditorScenariosPassOnVulkan`, 가장 길고 `Resource/scratch` 와 `Saved/Automation` 을 쓴다). 결과는 각각 단독 실행과 같은 통과 수.
2. `EveryScenarioPassesOnEveryBackend` + `AppUITest.DemoScreenMatchesAcrossBackends`(같은 `uidemo` 를 다른 프로세스가 돈다). 레이아웃 · 휘도 단언이 단독 실행과 같은지.
3. `AppSmokeTest.BenchFrameMatchesGoldenImage` 를 `QaGoldenImages` 와 동시에. 골든 비교가 같은 오차인지.
4. 같은 구성에서 세 번 반복해 재현율을 적는다. 한 번 지면 단독으로 다시 돌려 경합 탓인지 가른다.

`DemoScreenMatchesAcrossBackends`, `RecordedPlayReplaysOnEveryBackend`, `ChainedScenariosShareOneProcess` 는 시한 · 프레임 수에 기대므로 슬롯 둘일 때 시간 초과가 나는지 먼저 봅니다.

## 7. 규칙 후보

테스트가 띄우는 프로세스는 `Bin` · 고정 파일 이름 · 고정 포트에 쓰지 않는다. 쓰기 위치는 실행 식별자(pid · 백엔드 · 임시 폴더)가 들어간 경로여야 하고, 읽기 전용 공유 자원(`Resource` 의 추적 파일, 쿠킹 산출물)만 공유한다. 문서화는 [테스트 README](../../Test/README.md) 와 `docs/08_Verification.md` 를 맡은 작업자가 한다.
