# 작업자 규칙

병렬 작업자(에이전트)에게 일을 시킬 때 지시문 맨 앞에 "이 문서를 읽고 따른다"를 넣습니다. 규칙의 원본은 다른 문서에 있고, 이 문서는 작업자가 매번 어기기 쉬웠던 것을 그곳으로 연결합니다.
글은 한국어이고 용어는 [문서 쓰기 지침](../10_WritingDocs.md) 대조표를 따릅니다. 로그와 assert 문자열의 언어는 [AGENTS.md](../../AGENTS.md) "Writing" 이 정합니다.

먼저 읽을 것은 `CLAUDE.md`, [작업 방식](../11_Workflow.md), `AGENTS.md`(목차)와, 일에 맞는 `docs/agents/` 파일(C++ 이름은 `NamingCpp.md`, CMake · Python · HLSL 은 `NamingScripts.md` 등 — 쓰기 전에 그 파일만), 맡은 일의 계획 문서(`py -3 -m Scripts plans --file <이름>`)입니다. 후보는 `Candidates-<영역>.md` 의 그 절만 읽습니다.
main 체크아웃은 읽기만 합니다. 편집과 빌드는 하지 않습니다.

## 1. 워크트리

워크트리를 만들고 지우는 법, 링크 폴더와 vcpkg 잠금, 도우미의 push 금지는 [작업 방식](../11_Workflow.md) 1절 · 4절이 원본입니다. 만드는 명령은 이렇습니다.

```
py -3 -m Scripts worktree-make <이름> --base main
```

## 2. 빌드와 테스트

- 동시 빌드 한도, 빌드 칸 명령, `full-build`, GPU 잠금 모드(공유와 독점)는 [작업 방식](../11_Workflow.md) 2절 · 3절이 원본입니다. 도우미의 빌드와 테스트는 `--below-normal` 로 돌립니다.
  빌드를 맡지 않은 작업자는 "빌드 미확인" 으로 보고합니다.
- ctest 전체를 돌리지 않습니다. 테스트 실행 파일을 필터로 돌립니다. PATH 에 `build\Ninja-Debug\Bin` 을 넣고 `TestBin\<이름>.exe --test_filter=...` 입니다.
- 코드를 먼저 쓰고 빌드는 아껴서 합니다. 빌드가 겹쳐 시간 제한에 걸리면 단독으로 다시 돌려 구분합니다.
- **빌드 로그를 UTF-8 로 쓰고 경고를 `Select-String | Measure-Object` 로 셉니다.** `Start-Process` 의 `*>` 로 쓴 로그는 UTF-16 이라 bash `grep` 이 항상 0 을 돌려줍니다.
  `cmake --build ... 2>&1 | Out-File -Encoding utf8 <로그>` 로 씁니다.

## 3. 화면을 확인하는 방법

- 캡처는 앱 안에서 저장하는 `-gv_screenshot=<경로>.ppm`(`-gv_screenshotFrame=N`, 연속은 `-gv_screenshotCount` · `-gv_screenshotInterval`)을 씁니다. 방법은 [검증과 측정](../08_Verification.md) 에 있습니다.
- PPM 은 PNG 로 바꿔 Read 로 직접 보고, 본 것을 말로 적습니다. 로그가 초록이어도 그림이 틀릴 수 있습니다.
- **앱 창을 전경으로 올려 화면을 복사하는 스크립트를 쓰지 않습니다.** `SetForegroundWindow` · `ShowWindow` · `CopyFromScreen` 방식은 사용자 화면에 창이 튀어나오고 포커스를 뺏으며
  작업표시줄 미리보기가 캡처에 섞입니다. 메모장 같은 편집기를 띄우는 명령(`notepad`, `Start-Process <파일>`, `Invoke-Item`)도 쓰지 않습니다. 로그는 Read 도구로 읽습니다.
- `-gv_screenshot` 은 게임 뷰만 저장하고 에디터 UI 는 담지 못합니다. 에디터 UI(패널 · 플로팅 창)는 `-gv_screenshotEditor=<경로>.png` 나 시나리오 단계 `<CaptureEditorUI file="….png"/>` 로 찍습니다(전경을 쓰지 않는다, [에디터 UX 기반](EditorUX.md) 6 절). 패널 전체는 `engine/automation/editor/uiaudit.scenario.xml` 입니다.
- 에디터는 `-EnableEditor` 가 있어야 뜹니다. 실행 뒤 로그로 에디터 모듈이 로드됐는지 확인합니다.
- 비결정적으로 보이는 실패는 한 번으로 확정하지 않고 3~5 회 다시 돌려 재현율을 적습니다.

## 4. 코드 규칙

- 해결 원칙(구조로 푼다, 폴백 금지, 근본 원인 보고)은 [CLAUDE.md](../../CLAUDE.md) "해결 원칙" 이 원본입니다. 보고에 "근본 원인이 무엇이고 같은 모양이 어디에 더 있는가"를 한 줄 적습니다.
- 이름, 조건, 상수, 스레드, 문자열 서식, 헬퍼, 줄 끝과 스크립트 편집은 [AGENTS.md](../../AGENTS.md) 의 목차가 가리키는 `docs/agents/` 파일이 원본입니다. 새로 생기는 위반은 커밋 훅이 막습니다.
- 커밋과 훅 규칙은 [작업 방식](../11_Workflow.md) 7절, 에이전트 모델 고르기는 [에이전트와 토큰](../12_Agents.md) 이 원본입니다.
- ponytail 과 graphify 사용법은 [CLAUDE.md](../../CLAUDE.md) "보조 도구" 가 원본입니다. 작업자 워크트리에는 그래프 폴더가 없으므로 MCP `query_graph` 로 질의하고, 결과는 파일을 열어 확인합니다.
- 끝난 일을 문서에서 지우는 법은 [작업 방식](../11_Workflow.md) 6절이 원본입니다. 못 한 것은 계획 문서의 단위로 남기거나 후보 절로 적습니다.

## 5. 일감 지시문 틀과 보고

에이전트에게 일을 시킬 때 지시문과 호출에 아래 넷을 정합니다. 에이전트는 문서를 통째로 읽기 쉽고, 끝낼 때 어디를 고쳐야 하는지 몰라 문서가 낡았습니다.

1. **읽을 문서와 절** — 이름으로 적습니다(예: "계획 문서 `Vfx` 의 V1 절만"). 코딩 규칙은 **읽을 AGENTS 파일 이름을 적습니다**(예: "`docs/agents/NamingCpp.md` 와 `CppStyle.md` 만"). 파일이 주제별로 나뉘어 있어 이름만 적어도 통째로 읽는 실수가 없습니다.
2. **끝낼 때 직접 고칠 문서** — 그 에이전트가 고치는 곳과 고치지 않는 곳을 함께 적습니다(계획 문서의 해당 단위와 `진행`, 후보 절, `Roadmap.md` 의 그 줄. `NEXT.md` 와 `README.md` 표는 고치지 않습니다).
3. **끝났다고 말하기 전에 통과시킬 점검** — `py -3 -m Scripts plans --check` 와 `status-board --check README.md`. 코드를 고쳤으면 [작업 방식](../11_Workflow.md) 3절의 검증도 적습니다.

4. **모델과 effort** — [에이전트와 토큰](../12_Agents.md) 표에서 일에 맞는 값을 고르고 `Agent` 호출에 `model` · `effort` 로 지정합니다. 지정하지 않으면 호출한 세션의 레벨을 이어받아 단순한 일에도 비싸집니다.

사용자에게 에이전트를 맡겼다고 알릴 때는 **종류 · 모델 · 레벨**을 한 줄로 함께 적습니다.

끝나면 한국어로 보고합니다. 담을 것은 브랜치와 커밋 해시, 한 일, 돌린 빌드와 테스트의 결과(숫자), **못 한 것과 확인하지 못한 것**(추측은 추측이라고), 병합 때 충돌이 날 곳입니다.
눈으로 본 확인은 무엇을 어떻게 캡처해 무엇을 봤는지 적습니다. 보고에 쓴 숫자는 직접 센 것인지 작업자 보고를 옮긴 것인지 구분합니다.

## 6. 병합 담당이 하는 일

- 병합 절차(통합 브랜치, `full-build`, push 증거, 워크트리 지우기)는 [작업 방식](../11_Workflow.md) 1절 · 3절 · 4절이 원본입니다. 경고와 오류는 UTF-8 로그로 셉니다.
- 병합 전에 `code-reviewer` 에이전트로 읽기 전용 검토를 시킬 수 있습니다. 작업자끼리 부딪히는 결정과 규칙 위반을 찾습니다.
- 문서 충돌은 양쪽을 남기는 것이 보통입니다. 생성 문서(`docs/Config/*`)가 충돌하면 `GenerateConfigReference.py` 로 다시 만듭니다.

## 7. 재현 명령 (이 PC)

- Release 빌드 폴더는 `-DSW_GAMES=ALL` 로 구성돼 있습니다(구성은 `shared-lock acquire vcpkg --owner <이름>` 안에서). 게임 모듈만: `cmake --build build/Ninja-Release --target SWGame_<게임>`.
- 벤치: `cd build/Ninja-Release/Bin && ../TestBin/EngineTest.exe --test_filter=GameObjectBenchTest.<이름> --load_suites=only --host_suites=all`. 합성 A/B: `py -3 -m Scripts perf-compare --app build/Ninja-Release/Bin/App.exe --cases tickCostMovers tickCostSmallScene tickCostChurn tickCostRagdolls --runs 5`.
- 시나리오: `App.exe -game=<게임> -dx12 -scenario=game/<게임>/automation/<이름>.scenario.xml`(종료 코드 0 = 통과). GPU 잠금은 [작업 방식](../11_Workflow.md) 2절.
- 셰이더를 바꾸거나 병합이 `reflection.manifest` 에서 충돌하면 `App.exe --cook-shaders` 후 `ShaderCookRequestTest` 가 알리는 낡은 바이너리를 지웁니다(한 번에 8 개씩 알립니다 — 0 이 될 때까지 반복).
- 린트가 막으면 `py -3 Scripts/lint/fixer/FormatModified.py`, 설정 문서는 `py -3 Scripts/generate/GenerateConfigReference.py`.
- 검증 스크립트는 빌드 종료 코드와 `ctest` 가 읽는 바이너리의 갱신 시각을 함께 봅니다(구성 이름 오타로 옛 바이너리가 "통과"한 적이 있습니다).
