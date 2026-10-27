# 작업 방식

> 질문: 이 저장소에서 일을 어떻게 나누고, 빌드하고, 검증하고, 내보내는가?
> 독자: 작업자(에이전트), 통합 담당, 사람.
> 결론: 기본은 한 에이전트가 맡은 묶음을 단계별로 나눠 진행한다(단계마다 코드 → 린트 → `syntax-check` → 커밋). 빌드 · 검증 · 측정은 새 일마다 하지 않고 묶음이 다 끝난 뒤 같은 에이전트가 각각 한 번만 돌리고 main 에 넣고 push 한다. 병렬 작업자는 사용자가 시킬 때만 쓰며, 그때는 작업자가 커밋하고 멈추고 통합 담당이 모아 검증한다.

**진행 구조(사용자 결정 2026-10-27).** 병렬 작업자 + 통합 대기를 기본으로 쓰지 않습니다. 한 에이전트가 일을 몰아 받아 단계로 나누고, 단계 사이에는 빌드 · 링크 · 테스트를 하지 않습니다. 묶음이 끝나면 같은 에이전트가 `full-build` → 검증 → 병합 → push 를 3절 순서로 한 번 합니다. 긴 검증은 `run_in_background` 로 돌리고, 에이전트가 5 분 안에 답이 없으면 직접 한다는 규칙은 그대로입니다. 아래 "작업자" · "통합 담당" 은 병렬로 나눌 때의 두 역할이며 한 에이전트가 두 역할을 차례로 맡을 수 있습니다.

규칙의 원본은 한 곳씩입니다. 코딩 규칙은 [AGENTS.md](../AGENTS.md) 목차의 `docs/agents/`, 검증 명령과 측정법은 [검증과 측정](08_Verification.md), 정한 방향과 기각한 안은 [결정 기록](09_Decisions.md), 남은 일은 [계획 문서](plans/Roadmap.md)입니다. 이 문서는 진행 방식만 적습니다.

| 역할 | 하는 일 | 하지 않는 일 |
|---|---|---|
| 작업자 | 자기 워크트리에서 코드를 짜고, 고친 파일을 린트하고, 계획 문서를 고치고, 커밋한 뒤 보고하고 멈춘다 | 빌드, 테스트, 실행, 병합, push, 다른 워크트리와 main 체크아웃 수정 |
| 통합 담당 | 준비된 브랜치를 통합 워크트리에 병합하고 충돌을 풀고, 3절의 검증을 돌리고, main 에 넣고 push 한다 | 검증 전 push |

## 1. 일을 나누는 방식 — 워크트리와 통합

큰 작업 배치는 단위로 나누어 워크트리 하나에 브랜치 하나씩 진행합니다.

```powershell
py -3 -m Scripts worktree-make <이름> --base main      # <드라이브>:\w\<이름>(Windows) · <main 의 부모>/LT-wt/<이름>, 브랜치 wt/<이름>
py -3 -m Scripts worktree-make <이름> --base wt/apply5 # 다른 브랜치에서 갈라 만든다
py -3 -m Scripts worktree-remove <이름>                # 공유 링크를 먼저 끊고 지운다. 다른 세션이 쓰는 중이면 건너뛴다
py -3 -m Scripts worktree-list                         # 워크트리마다 브랜치와 주인
py -3 -m Scripts shared-lock acquire vcpkg --owner <이름> --purpose "configure"   # vcpkg 를 부르기 전
py -3 -m Scripts shared-lock release vcpkg --owner <이름>                          # 끝나면 바로 놓는다(status 로 누가 쓰는지 본다)
```

- **`--base` 를 반드시 줍니다.** 주지 않으면 `origin/main` 에서 갈라져 로컬 main 이 push 전이면 낡은 기준입니다. 지울 때는 `git merge-base --is-ancestor wt/<이름> main` 으로 병합을 확인한 뒤 `worktree-remove` 를 씁니다(브랜치도 지웁니다).
- `worktree-remove` 는 주인 프로세스가 살아 있고 내 것이 아니면 지우지 않고(`--even-if-owned` 는 사용자가 시킨 때만), 주인 기록이 없으면 지우기 전에 사용자에게 묻습니다. 사람 셸은 `worktree-make --pid $PID` 입니다.
- 검증 출력(로그 · 캡처 · 측정값)은 워크트리 안의 임시 폴더나 세션 스크래치패드에 둡니다.
- `.claude/skills` 는 추적하지 않으므로 스킬을 쓰려면 세션 시작 전에 main 체크아웃에서 워크트리의 같은 위치로 복사합니다.
- 링크 폴더(`Tools/*`, `build/vcpkg_*`)는 main 체크아웃과 나눠 쓰므로 지우거나 고치지 않습니다. `vcpkg.json`, 포트, `cmake/Modules/Toolchain/Vcpkg/*` 는 통합 담당이 main 에서 먼저 바꿉니다.
- **vcpkg 를 부르는 일**(`cmake --preset`, `--clean-first` 빌드, vcpkg 설치)은 먼저 `shared-lock acquire vcpkg` 로 잡고 끝나면 놓습니다(의존성이 다른 트리가 같은 `build/vcpkg_installed` 에 설치하면 main 빌드가 링크 오류로 섭니다). 기다려도 안 풀리면 사용자에게 알립니다.
- main 체크아웃은 읽기 전용이고, 통합 담당만 검증을 마친 커밋을 fast-forward 로 넣습니다.

### 작업자

- 자기 워크트리 안에서만 일합니다. 전체 빌드 · 링크 · 테스트 · 실행 · 병합 · push 는 하지 않고, 코드를 읽고 쓴 뒤 린트하고 `py -3 -m Scripts syntax-check` 로 **바꾼 파일만** 문법 검사(`-fsyntax-only`)를 하고 커밋하고 멈춥니다. 이 검사가 못 잡는 곳(템플릿 인스턴스화 · 링크 · 다른 파일로의 파급)은 보고에 "컴파일 확인 필요" 로 적습니다(쓰는 법 · 한계: [작업자 규칙](plans/WorkerRules.md) 2절).
- 같은 파일을 건드리는 단위는 한 작업자에게 몰아 줍니다. 동시에 띄우는 수의 상한은 [에이전트와 토큰](12_Agents.md) 4절입니다.
- 기계적 변경(이동, 이름 바꾸기, 일괄 재작성)은 커밋만 단계별로 나눕니다.
- **Source 폴더 · 파일 이동은 `py -3 -m Scripts move-sources --map <표.toml> [--dry-run]` 로만 합니다.** 표는 `[move]` 의 `"옛" = "새"`(Source 기준)이고, 추적 텍스트의 옛 경로 · include 를 고쳐 `git mv` 하며 옛 경로가 남거나 CMake 글롭이 맞히는 파일이 줄거나, 변수를 못 푼 글롭 · 상대 경로(`Common/Workspace`, `add_subdirectory(Old)`)가 옮기는 폴더에 닿을 수 있으면 쓰기 전에 멈춥니다. 그런 파일은 손으로 고친 뒤 표의 `[reviewed]`(`"파일" = "이유"`)에 적습니다. 손댄 파일 목록(`touched`)을 찍으니 병렬 단위끼리 교집합을 봅니다.
- 사용 한도(`rate_limit`)로 끊기면 워크트리 작업은 남습니다. 한도가 풀린 뒤 `SendMessage` 로 이어서 시킵니다.

### 통합 담당

순서(모으기 → Debug → Shipping → Release → verify-batch → push → NEXT 덮어쓰기 → 정리 → 보고)는 `/integrate` 스킬이 정본이고, 아래는 규칙과 이유입니다.

- **준비된 브랜치부터 한 통합 워크트리에 병합하고, 빌드 · 검증 · 측정은 그 워크트리에서 각각 한 번만 돕니다.** 브랜치 하나가 끝났다고 따로 병합하거나 전체 빌드를 하지 않고, 검증 전에는 push 하지 않습니다.
- **앞선 통합이 긴 단계(GPU 검증)에 있어도 기다리지 않고 선행 통합을 얹습니다.** 새로 끝난 브랜치는 `worktree-make 통합-다음 --base wt/<앞선 통합>` 으로 그 위에 병합해 충돌을 풀고 `-k 0` 컴파일과 lint · nogpu 까지만 돕니다. GPU 실행 · main 병합 · push 는 하지 않고 빌드는 코어 수를 절반으로 줄입니다. 앞선 통합이 main 에 들어가면 그 위로 다시 얹고 컴파일 확인과 GPU 검증만 한 번 돕니다. 앞선 통합의 워크트리와 브랜치는 건드리지 않습니다.
- 충돌은 병합하면서 풉니다. 생성 산출물(셰이더 매니페스트, 쿠킹 바이너리, `import.stamp`)은 한쪽을 고르지 않고 `App --cook-shaders` 나 `--import-models` 로 다시 만들고, 생성 문서(`docs/Config/*`)는 `GenerateConfigReference.py` 로 다시 만듭니다. 문서 충돌은 양쪽을 남기는 것이 보통입니다.
- 넓게 건드리는 변경(폴더 이동, 헤더, 매크로 철자)을 먼저 넣습니다. 병합 전에 `code-reviewer` 로 읽기 전용 검토를 시킬 수 있고, 병합이 끝난 워크트리는 바로 지웁니다.

## 2. 빌드와 CPU

- **동시에 빌드하는 작업은 최대 둘입니다**(작업자는 빌드하지 않으므로 통합 담당뿐). 전체 빌드가 겹치면 CPU 가 100 % 에 붙어 테스트 시간 제한이 가짜로 집니다.
- **빌드 슬롯 잠금(`build`, 공유 슬롯 둘)이 한도를 강제합니다.** 검증 빌드는 `full-build`(3절)가 스스로 잡고, 그 밖의 빌드는 이렇게 돌립니다. 슬롯 대기가 시한(기본 30 분)을 넘기면 사용자에게 알립니다.
  `py -3 -m Scripts shared-lock run build --shared --owner <이름> [--below-normal] -- cmake --build --preset Ninja-Debug --target <타깃>`
- 다른 일과 겹치는 빌드와 테스트는 `--below-normal` 로 돌립니다. `--target` 없는 빌드는 테스트를 짓지 않으므로 테스트를 돌리려면 테스트까지 먼저 짓습니다.
- **게임 코드는 `Ninja-Debug` 한 빌드가 모두 짓습니다(`SW_GAMES=ALL`).** 게임 실행 확인은 `App.exe -game=<게임>` 입니다. 게임을 줄인 구성(`Ninja-Debug-Lean`)으로 지었으면 "게임 빌드 미확인" 으로 보고합니다.
- **GPU 실행은 잠금 모드로 나눕니다.** 결정적 설정으로 기능만 보는 시나리오 · `hostgpu` · 캡처는 공유 모드(동시에 두 프로세스), Release 측정 · 부하 기준선 · 장치 제거(TDR) 실험 · 실제 마우스와 창 포커스에 기대는 시나리오는 독점입니다. `py -3 -m Scripts shared-lock acquire gpu --owner <이름> --shared`(독점은 `--shared` 없이)로 잡고, 끝나면(실패해도) `release` 합니다. GPU 실행만 맡기려면 `gpu-verifier` 를 씁니다.
- 컴파일러 경고 전체 조사(`Scripts/lint/report/RunBuildWarnings.py`)는 사용자가 요청할 때만 돌립니다.
- 스크립트로 파일을 고칠 때의 규칙은 [EditingTraps](agents/EditingTraps.md)가 원본입니다.

## 3. 검증 단계

명령과 측정법은 [검증과 측정](08_Verification.md)이 원본이고, 여기에는 언제 어디까지 돌리는지만 적습니다.

**검증 빌드는 늘 전체입니다 — `py -3 -m Scripts full-build --owner <이름>`(`all AllTests`, 빌드 슬롯을 잡는다).** 바꾼 영역의 테스트만 지으면 다른 영역이 쓰는 헤더 · 매크로를 바꾼 것이 병합 뒤에야 깨집니다. "검증했다" 고 보고하는 빌드는 통합 담당이 병합 결과에서 한 번 돌린 `full-build` 입니다.

- `full-build` 는 커밋하지 않은 변경이 없을 때만 짓고, 성공하면 그 커밋의 빌드 입력(마크다운 · `.claude/` 등 빌드가 쓰지 않는 파일을 뺀 트리)의 증거를 공통 `.git` 에 남깁니다. 문서만 바꾼 커밋은 직전 증거가 맞습니다.
- **main 으로의 push 는 pre-push 훅이 그 증거를 요구합니다**(`Scripts/setup/InstallGitHooks.py` 로 설치). `full-build` · `py -3 -m Scripts test` 는 로그를 `.git/sw-build-logs/` 에 쓰고 요약만 보이며 전부 보려면 `--verbose` 입니다.

검증은 통합 워크트리에서 구성별로 한 번씩 돕니다: Debug → Shipping → Release → push 뒤 CI(리눅스). 같은 구성이 필요한 일은 그 구성에서 이어서 하고, 실패하면 그 단계부터 다시 합니다. 확인 · 측정 후보마다 따로 돌리지 않으며 측정 질문은 Release 단계에 한 줄로 더합니다. 이번 묶음의 실제 표는 [NEXT](plans/NEXT.md)에 있습니다.

| 단계 | Debug | Shipping | 그 밖 |
|---|---|---|---|
| 작업자 | 고친 파일의 린트(포맷 포함)만 | — | — |
| 통합(배치마다) | `full-build` 경고 0, `-L lint`, `-L nogpu`, `-L race` | 빌드, `-L nogpu`, `-L hostgpu`, 확인 대기 전부 | `Empty` 렌더 테스트 씬 + 개발 중인 게임 하나(지금 Shooter3D) × 네 백엔드 |
| 큰 단계 끝(출시 · 큰 마일스톤 직전) | 위 + `-L hostgpu` 전체 한 번 | 위와 같음 | 모든 게임 × 모든 백엔드, Release 빌드와 성능 기준, 전용 서버 전체(리눅스 빌드는 CI) |

- **기능 시나리오(`hostgpu`)는 Shipping 이 기본입니다.** 레이스 검사(`DataRaceDetector`)는 `SW_DEBUG` 에서만 켜지므로 Debug 는 묶음 `race`(`Test/TestPolicy.json` 의 `raceCases`)만 돕니다. 게임 선택이 필요한 검증은 Shipping 이 `-game=` 을 무시하므로 Release(`SW_GAMES=ALL`)로 합니다.
- 통합 담당의 ctest 는 `-j` 로 돌리고, 끝나면 남은 `Server` · `ServerTest` · `EngineTest` 프로세스가 없는지 봅니다. CI 러너는 `nogpu` 만 돌며 `race` 는 GPU 있는 PC 의 Debug 가 맡습니다(리눅스 `CI-Debug-TSAN` 이 보조).
- 엔진 런타임 전체를 바꾸는 변경이면 배치 안에서도 큰 단계 끝의 검증을 돌립니다. 의존성을 올렸다면 걸린 기능(TLS, Tracy, 검증 레이어)도 봅니다. 에디터 변경은 에디터 시나리오(`-EnableEditor`)를 하나 더해 AppScenarioTest 로 확인합니다.
- **"확인 대기"는 쌓지 않고 통합 단계에서 한 번에 돌립니다.** 작업자는 GPU · 눈 확인 없이 "GPU 확인 대기"로 보고하고, 통합이 `full-build` 직후 대기 항목을 `gpu-verifier`(Shipping, 네 백엔드)로 돌려 계획 문서에 반영합니다.
- **측정 항목은 매니페스트 하나(`Test/QA/VerifyBatch.json`)에 두고 `py -3 -m Scripts verify-batch --owner <이름>` 한 명령으로 돌립니다.** 구성별로 모아 돌리고, 한 항목이 져도 나머지를 돌려 표 하나를 남깁니다. **기본 실행은 정합성 검증뿐입니다** — 측정(`measure` 종류: `perf` · `load` · `perf-compare` · 네트 벤치)은 `--measure`(더하기)나 `--measure-only`(측정과 그 구성의 빌드 · 쿠킹만)가 있을 때만 돌고, 성능에 닿는 변경에서만 부릅니다(측정은 GPU 독점이라 혼자 돌고 Release 한 번에 40 ~ 45 분이다). 통과 · 실패를 판정하는 측정 종류 항목은 `functional` 이라 기본에 듭니다. `--fail-fast` 는 단계(빌드 → 쿠킹 → ctest → 테스트 → 골든 → 측정) 하나가 끝났을 때 진 것이 있으면 뒤 단계(뒤 구성 포함)를 돌리지 않고 `건너뜀 앞 단계 …` 로 표에 올립니다. 항목의 `requires` 선행이 통과하지 못하면 그 항목은 돌지 않고 `건너뜀 선행 …` 입니다(선행이 남긴 보고를 읽는 항목의 연쇄 실패를 막는다). `--failed-from` 은 이 건너뜀도 다시 고릅니다. 시나리오 전부(`app-scenarios-release`)는 `ctest -L hostgpu` 의 샤드로 돌고 보고는 `build/<프리셋>/AppRuns/shard<k>of<n>/<케이스>/Saved/Automation` 아래에 남습니다. `test` 항목의 `shards` 는 같은 필터를 `--test_shard=k/n` 조각으로 나눠 GPU 칸 수(`gpuSlots`)만큼 겹쳐 돌리고 결과 하나로 합칩니다 — 그 실행 파일의 정책에 같은 `Bin` 잠금(`hostResourceLock`)이 남아 있으면 조각은 순서대로 돕니다. `ctest` 항목은 혼자 돕니다(`ctest -j` 둘이 겹치면 CPU 경합으로 시간 제한이 빠듯한 린트가 가짜로 집니다). 확인 대기는 글 대신 `매니페스트 id` 와 항목 id 로 적고(`plans --check` 와 `CheckVerifyPending` 이 막습니다), 명령이 없는 확인은 사람이 해야 하면 `manual`, 아직 명령을 못 만들었으면 `need_command` 에 이유를 적습니다(일회성 대기는 명령이 있거나 `manual` 이어야 하고, 명령 없는 항목은 어느 문서의 `매니페스트 id` 줄이 주인으로 가리킵니다).
- **main 앞의 증거는 `integ-gate` 가 요구합니다.** 병합 결과에서 `py -3 -m Scripts integ-gate run --owner <이름>` 이 `-k 0` 으로 끝까지 짓고(빌드가 서지 않았으면 `ctest -L "nogpu|lint"`) 결과를 `<공통 .git>/sw-integration-evidence/` 에 남깁니다. push 앞에 `integ-gate check` 가 0 인지 봅니다(pre-push 훅에는 아직 안 걸었습니다).
- **GPU 항목은 정해진 시각에 몰아 돌립니다.** 통합 워크트리에서 `py -3 -m Scripts verify-batch --owner nightly --config Shipping --config Release --measure`(먼저 `--dry-run`; 측정을 빼려면 `--measure` 를 뺀다)로 돌리면 `build/verify-batch/<시각>/result.md` 에 표가 남고 다음 통합이 읽습니다. 예약 작업 등록은 사용자 PC 설정입니다.
- **손 확인을 사용자에게 넘기지 않습니다.** 화면에서 확인할 일은 자동화 시나리오(`Resource/<영역>/automation/*.scenario.xml`)로 만들고, 못 하는 것만 이유와 함께 `Candidates-<영역>.md` 에 씁니다.
- 성능 주장은 Release 측정값으로만 합니다([검증과 측정](08_Verification.md) 1 · 2절).

## 4. 내보내기 — push 와 CI

- **검증을 마친 main 은 묻지 않고 push 합니다.** 통합 워크트리에서 검증한 커밋을 main 에 fast-forward 로 넣고 push 하며, 사용자에게는 들어간 것 · 검증 수치 · 상용 엔진(언리얼, 유니티, Godot)과 비교한 위치와 빠진 것만 짧게 알립니다.
- 로컬 빌드 · 검증이 통과하면 CI 를 기다리지 않고 `git fetch` 로 origin 을 본 뒤 force 없이 push 한다(사용자 승인).
- 작업자는 push 하지 않습니다.
- main push 가 "전체 빌드 증거가 없다" 로 막히면 그 커밋에서 `full-build` 를 돌립니다. 훅을 우회하지 않습니다.
- 뒤에 문제가 드러나면 그 단위를 되돌립니다. 배치 사이에 사용자 확인을 기다리지 않습니다.
- **push 뒤 CI 결과를 기다리며 멈추지 않습니다.** 사용자가 실패를 알려 주거나 세션 시작에 세션 가드가 main CI 실패(`py -3 -m Scripts ci-jobs --summary`)를 알리면 그때 원인을 찾고, 며칠 이어진 실패는 먼저 봅니다. 읽는 법은 [검증과 측정](08_Verification.md) 4절입니다.

## 5. 결정하는 방식

- **기술 결정은 상용 엔진의 같은 기능과 비교해 정하고, 근거 한 줄과 함께 [결정 기록](09_Decisions.md) 5절이 가리키는 영역 파일(`docs/decisions/5-<번호>_*.md`)의 끝에 남깁니다.** 비교 대상은 언리얼, 유니티, Godot 이고 온라인 기능은 PlayFab, EOS, Nakama 도 봅니다. 우리 구조가 더 낫거나 확장성이 좋으면 따르지 않고 더 발전시킵니다.
- 사용자에게는 "정한 것" 으로만 알리고 결정 표를 올려 고르게 하지 않습니다. **"사용자 결정 대기" 를 인계 문서에 쌓지 않습니다.** 판정이 필요하면 `design-reviewer` 로 비교해 그 자리에서 정합니다.
- 사용자에게 묻는 것은 법과 라이선스, 외부 공개(PR, 게시), 비용이나 계정이 드는 연동, 사용자가 보류로 정한 항목, 되돌리기 어려운 큰 데이터 삭제뿐입니다.
- 게임 리소스는 CC0 급만 넣습니다([결정 기록](09_Decisions.md) 5-1). 이름을 바꿀 때 옛 이름 별칭을 두지 않고 데이터를 다시 씁니다(5-4).

## 6. 할 일을 다루는 방식

할 일은 아래 네 곳에만 둡니다. 한 사실은 한 곳에만 쓰고 나머지는 링크합니다. 역할 표는 [문서 지도](02_DocumentMap.md) 맨 위에 있습니다.

| 곳 | 담는 것 |
|---|---|
| `docs/plans/<이름>.md` | 착수한 일의 설계, 단위, 계약. 둘째 줄 머리 줄에 상태 · 선행 · 영역 · 옛번호 · 진행(`n/m`, 단계 표(`| 단계 | … | 상태 |`)를 두는 문서는 `표` 로 써서 표에서 세게 합니다)을 둡니다 |
| `docs/plans/Candidates-<영역>.md` | 아직 착수하지 않은 후보. 일마다 `### 이름` 절(바로 아래 `제목:` · `옛번호:` 줄)에 왜 필요한가와 착수 조건을 둡니다 |
| `docs/plans/Roadmap.md` | 순서만. 일은 이름 링크로만 가리킵니다 |
| `docs/plans/NEXT.md` | 지금 상태만. 통합 담당만 고칩니다. 일지가 아니라 한 장이라 고정 세 절(마지막 통합 한 줄 요약 / 남은 일 / 확인할 것)만 두고 통합이 끝날 때 덮어쓰며, 절을 새로 추가하지 않고 20KB 를 넘기지 않습니다(`CheckNextSize` 게이트, `<!-- next-format: one-page -->` 줄이 있을 때 검사) |

- 일의 식별자는 이름입니다(`ClusteredLighting`, 단위는 `ClusteredLighting.S2`). 옛 번호(`C-30` 등)는 `py -3 -m Scripts plans --find <옛번호>` 로 새 이름을 찾습니다. 새 일에 번호를 매기지 않습니다.
- 목록은 `py -3 -m Scripts plans`, 링크 점검은 `plans --check` 입니다. 루트 `StatusBoard.md` 는 `py -3 -m Scripts status-board --write` 가 만든 생성물이라 손으로 고치지 않고 병합 뒤 통합 담당만 다시 만듭니다(작업자가 만들면 병합이 충돌합니다).
- **일을 끝낸 사람이 직접 고칩니다.** 단위를 끝내면 그 단위 절을 지우고 머리 줄 `진행` 을 올립니다. 후보를 착수하면 절을 계획 문서로 옮기고 후보 문서에서 지웁니다. 마지막 단위를 끝내 `n/n` 이 되면 상태도 올립니다: 실행 확인(GPU · 측정)이 남았으면 `측정대기`, 다 끝났으면 `끝`(`plans --check` 가 `n/n` 인데 `구현중` 인 문서를 잡습니다).
- **상태가 `끝` 인 계획 문서는 지웁니다**(2026-10-24 사용자 지시). 남길 교훈은 먼저 코드 헤더 주석 · [결정 기록](09_Decisions.md) · [검증과 측정](08_Verification.md)으로 옮기고, 지운 문서를 가리키던 링크(Roadmap, 다른 문서, 코드 주석)를 모두 고칩니다.
- 갱신은 묻지 않고 작업 커밋에 함께 넣습니다(`plans --check` 와 `status-board --check` 가 점검).
- 병합 중이면(`.git/MERGE_HEAD`) 충돌 파일이 내가 만진 것인지부터 봅니다. 커밋 훅 없이 올라온 커밋은 포맷 위반을 달고 올 수 있으므로 `py -3 Scripts/lint/fixer/FormatModified.py` 로 정리합니다.

## 7. 커밋 메시지와 글

- **커밋 제목은 `<영역> - <요약>` 입니다.** 본문은 `문제점:`(증상과 원인), `해결방안:`(바꾼 타입, 함수, 파일), `결과:`(통과한 테스트, 동작, 측정값) 세 절이고, 맨 끝에 모델에 맞는 `Co-Authored-By` 줄을 둡니다. 병합 커밋도 한국어로 씁니다(`병합 - main(<해시>)을 wt/<이름> 으로(<무엇이 들어왔나>)`).
- **커밋 훅을 우회하지 않습니다(`--no-verify` 금지).** 훅이 린트를 돌리므로 임시 커밋으로 린트를 테스트하지 않습니다. 서식은 `py -3 Scripts/lint/fixer/FormatModified.py` 로 맞추고, 훅 출력은 요약이며 전부 보려면 `py -3 -m Scripts lint --verbose` 입니다.
- 코딩 규칙은 [AGENTS.md](../AGENTS.md), 해결 원칙은 [CLAUDE.md](../CLAUDE.md) 가 원본입니다.
- 커밋 메시지는 프로그래머 용어로 쓰고 경위와 배경을 담습니다. 주석과 문서에는 현재형의 계약과 주의만 씁니다.
- 사용자에게 보이는 모든 글은 한국어로 씁니다. 식별자, 경로, 명령, 로그 문자열은 원문 그대로 둡니다. 문체는 [문서 쓰기 지침](10_WritingDocs.md)을 따릅니다.

## 8. 리눅스 빌드

리눅스는 WSL 안의 별도 클론에서 빌드합니다. Windows 체크아웃에서 `WSL-*` 프리셋을 돌리면 공유 vcpkg 설치 트리를 망가뜨립니다. 클론 위치와 진단법은 [Scripts/setup README](../Scripts/setup/README.md) "함정 · 계약" 절에 있습니다.
**리눅스 확인은 CI 가 합니다**(사용자 지시). 로컬 WSL 빌드를 돌리지 않고 main 을 push 한 뒤 `py -3 -m Scripts ci-jobs --summary` 로 리눅스 잡을 봅니다. 실패하면 그때 원인을 찾고, "리눅스 미확인" 으로 보고하며 대기 항목으로 남기지 않습니다.

## 9. 에이전트와 토큰

에이전트를 띄우거나 토큰을 따질 때만 [에이전트와 토큰](12_Agents.md)을 읽습니다. 지시문 틀과 보고 틀은 [작업자 규칙](plans/WorkerRules.md)입니다.
