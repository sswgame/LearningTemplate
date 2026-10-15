# 작업자 규칙

병렬 작업자(에이전트)에게 일을 시킬 때 지시문 맨 앞에 "이 문서를 읽고 따른다"를 넣습니다. 작업 방식의 정본은 [작업 방식](../11_Workflow.md)이고, 이 문서는 그중
작업자가 매번 어기기 쉬웠던 것을 모았습니다. 글은 한국어, 로그와 assert 문자열은 영어입니다. 용어는 [문서 쓰기 지침](../10_WritingDocs.md) 대조표를 따릅니다.

먼저 읽을 것은 `CLAUDE.md`, 작업 방식, `AGENTS.md`(C++ · CMake · Python 을 쓰기 전), 맡은 일의 백로그 항목과 계획 문서입니다.
main 체크아웃은 읽기만 합니다. 편집과 빌드는 하지 않습니다.

## 1. 워크트리

```
py -3 -m Scripts worktree-make <이름> --base main
```

- `--base main` 을 반드시 줍니다. 주지 않으면 `origin/main` 에서 갈라지는데 로컬 main 이 push 전이면 낡은 기준입니다.
- 브랜치는 `wt/<이름>`, 폴더는 `D:\w\<이름>` 에 생기고 `Tools/LLVM` · `Ninja` · `vcpkg` · `_cache` 와 `build/vcpkg_installed` · `vcpkg_cache` 가 main 의 것으로 링크됩니다.
  이 링크는 지우거나 고치지 않습니다. 직접 정션을 걸지 않습니다.
- `.claude/skills` 는 추적하지 않아 새 워크트리에 없습니다. 에이전트가 ponytail · graphify 스킬을 쓰려면 세션을 시작하기 전에 복사합니다.
  `Copy-Item -Recurse -Force D:\Projects\Personal\LearningTemplate\.claude\skills D:\w\<이름>\.claude\skills`
- `cmake --preset` 처럼 vcpkg 를 부르는 일은 먼저 `py -3 -m Scripts shared-lock acquire vcpkg --owner <이름>` 으로 잠금을 잡고, 끝나면 `release` 합니다.
- 다른 워크트리와 main 을 건드리지 않습니다. push 하지 않습니다.
- 지울 때는 병합이 맞는지 `git merge-base --is-ancestor wt/<이름> main` 으로 확인한 뒤 `py -3 -m Scripts worktree-remove <이름>` 을 씁니다. 이 도구는 브랜치도 지웁니다.

## 2. 빌드와 테스트

- 동시 빌드는 최대 둘입니다. `all AllTests` 는 통합 담당만 합니다. 작업자는 필요한 단일 타깃(EngineTest · EditorTest · CoreTest · 모듈 DLL)만 짓고 경고 0 으로 맞춥니다.
- ctest 전체를 돌리지 않습니다. 테스트 실행 파일을 필터로 돌립니다. PATH 에 `build\Ninja-Debug\Bin` 을 넣고 `TestBin\<이름>.exe --test_filter=...` 입니다.
- 코드를 먼저 쓰고 빌드는 아껴서 합니다. 빌드가 겹쳐 시간 제한에 걸리면 단독으로 다시 돌려 구분합니다.
- **빌드 로그를 UTF-8 로 쓰고 경고를 `Select-String | Measure-Object` 로 셉니다.** `Start-Process` 의 `*>` 로 쓴 로그는 UTF-16 이라 bash `grep` 이 항상 0 을 돌려줍니다.
  `cmake --build ... 2>&1 | Out-File -Encoding utf8 <로그>` 로 씁니다.
- GPU 와 창을 쓰는 실행(앱 시나리오, 스크린샷, `hostgpu` 테스트)은 GPU 잠금을 잡고 합니다. 기능 확인(결정적 설정)은 공유 모드로 동시에 두 프로세스까지,
  Release 측정 · 부하 기준선 · 실제 장치 제거 실험 · 실제 마우스와 창 포커스에 기대는 시나리오는 독점으로 혼자 돌립니다.
  `py -3 -m Scripts shared-lock acquire gpu --owner <이름> --shared`(독점은 `--shared` 없이)로 잠그고, 끝나면(실패해도) 같은 모드로 `release` 합니다. 잠금을 쥔 채 오래 있지 않습니다.
  GPU 실행만 맡기려면 `gpu-verifier` 에이전트를 씁니다.

## 3. 화면을 확인하는 방법

- 캡처는 앱 안에서 저장하는 `-gv_screenshot=<경로>.ppm`(`-gv_screenshotFrame=N`, 연속은 `-gv_screenshotCount` · `-gv_screenshotInterval`)을 씁니다. 방법은 [검증과 측정](../08_Verification.md) 에 있습니다.
- PPM 은 PNG 로 바꿔 Read 로 직접 보고, 본 것을 말로 적습니다. 로그가 초록이어도 그림이 틀릴 수 있습니다.
- **앱 창을 전경으로 올려 화면을 복사하는 스크립트를 쓰지 않습니다.** `SetForegroundWindow` · `ShowWindow` · `CopyFromScreen` 방식은 사용자 화면에 창이 튀어나오고 포커스를 뺏으며
  작업표시줄 미리보기가 캡처에 섞입니다. 메모장 같은 편집기를 띄우는 명령(`notepad`, `Start-Process <파일>`, `Invoke-Item`)도 쓰지 않습니다. 로그는 Read 도구로 읽습니다.
- `-gv_screenshot` 은 게임 뷰만 저장하고 에디터 UI 는 담지 못합니다. 에디터 UI 를 보려면 전경을 훔치지 않는 창 캡처가 필요하고, 아직 없습니다. 에디터 UI 확인은 못 했다고 보고합니다.
- 에디터는 `-EnableEditor` 가 있어야 뜹니다. 실행 뒤 로그로 에디터 모듈이 로드됐는지 확인합니다.
- 비결정적으로 보이는 실패는 한 번으로 확정하지 않고 3~5 회 다시 돌려 재현율을 적습니다.

## 4. 코드 규칙

새로 생기는 위반은 커밋 훅이 막습니다.

- 조건 세 부분 이상: `if` · `while` 조건이 `&&` · `||` 로 세 부분 이상이면 먼저 의미 있는 `const bool bXxx = ...;` 로 이름을 붙입니다(`CheckConditionParts`).
- 상수: 시간 · 바이트 단위는 `constant::kNanosecondsPerSecond` · `kMillisecondsPerSecond` · `kSecondsPerMinute` · `kBytesPerKilobyte` · `kBytesPerMegabyte` 등(`Core/Common/Defines.h`)입니다.
  새 상수의 자리는 `AGENTS.md` "Constants" 절을 따릅니다.
- `std::thread` 를 직접 쓰지 않고 `sw::thread` 를 씁니다. 정적 등록 표는 `RegistrationList` 입니다. 사용자 설정 쪽 이름은 `Handler` 입니다.
- 별칭과 옛 이름 호환을 두지 않고 데이터를 다시 씁니다. 새 전용 함수를 늘리기보다 기존 모양을 고칩니다.
- ponytail 은 항상 켜져 있어 가장 작은 완전한 변경을 요구합니다. 요청이 구조 개편이면 그 요청을 따릅니다.
- 코드 질문은 먼저 `graphify query "<질문>" --graph D:\Projects\Personal\LearningTemplate\graphify-out\graph.json`(PATH 에 사용자 Scripts 폴더)로 범위를 좁힙니다.
  그래프는 낡았을 수 있으니 결과는 파일을 열어 확인합니다.
- 줄 끝: Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있습니다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 봅니다.
  스크립트가 필요하면 heredoc 대신 Write 도구로 scratchpad 파일에 쓰고 `py -3 -I` 로 실행합니다.
- 서식은 `py -3 Scripts/lint/fixer/FormatModified.py` 입니다. **커밋 훅을 우회하지 않습니다(`--no-verify` 금지).** 실패하면 고칩니다.
- 커밋은 단계별로 나누고, 메시지는 문제점 → 해결방안 → 결과 순으로 프로그래머 용어로 씁니다. 끝에 `Co-Authored-By` 줄을 모델에 맞게 답니다.
- 끝난 백로그 · 인계 항목은 지우고 교훈은 코드 헤더 주석이나 결정 기록으로 옮깁니다. 못 한 것은 구체 항목으로 백로그에 남깁니다.

## 5. 보고

끝나면 한국어로 보고합니다. 담을 것은 브랜치와 커밋 해시, 한 일, 돌린 빌드와 테스트의 결과(숫자), **못 한 것과 확인하지 못한 것**(추측은 추측이라고), 병합 때 충돌이 날 곳입니다.
눈으로 본 확인은 무엇을 어떻게 캡처해 무엇을 봤는지 적습니다. 보고에 쓴 숫자는 직접 센 것인지 작업자 보고를 옮긴 것인지 구분합니다.

## 6. 병합 담당이 하는 일

- 끝난 브랜치를 통합 브랜치에 모아 전체 Debug 빌드를 하고, UTF-8 로그로 경고와 오류를 센 뒤 main 에 병합합니다. 병합 성공을 확인한 뒤에만 워크트리를 지웁니다.
- 병합 전에 `code-reviewer` 에이전트로 읽기 전용 검토를 시킬 수 있습니다. 작업자끼리 부딪히는 결정과 규칙 위반을 찾습니다.
- 문서 충돌은 양쪽을 남기는 것이 보통입니다. 생성 문서(`docs/Config/*`)가 충돌하면 `GenerateConfigReference.py` 로 다시 만듭니다.
