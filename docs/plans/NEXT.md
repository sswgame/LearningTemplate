# 다음 세션 인계

이 문서는 **지금 상태**만 둡니다. 새 세션은 이 문서를 먼저 읽고, 나머지는 아래에서 일에 맞는 것만 찾습니다. 한 사실은 한 곳에만 씁니다.

| 알고 싶은 것 | 어디 |
|---|---|
| 남은 일 한 건 한 건 | 후보 문서 `Candidates-<영역>.md` — `py -3 -m Scripts plans --area <영역>`(목록), `--file <이름>`(머리 줄과 절 목록) |
| 설계와 구현 단위 | 계획 문서 — `py -3 -m Scripts plans`(목록) |
| 앞으로 할 일의 순서 | [로드맵](Roadmap.md) |
| 결정과 근거 | [결정 기록](../09_Decisions.md) |
| 작업 방식 · 검증 | [작업 방식](../11_Workflow.md), [검증과 측정](../08_Verification.md) |
| 에이전트에게 맡길 때 · 토큰 | [에이전트와 토큰](../12_Agents.md), [작업자 규칙](WorkerRules.md) |
| 환경 · 도구 함정 | [함정](../14_Pitfalls.md) — 해당하는 일을 할 때만 |

한 일은 `git log` 가 정본입니다.

## 1. 지금 상태 (2026-10-19)

- main CI(실행 39143580252)는 열 잡 중 아홉이 통과했다. 남았던 Linux TSan 의 `EditorNetPlaySessionTest.DedicatedServerAndTwoClientsConnectPauseAndMergeTraces` 는 테스트가 프로세스마다 틱 하나가 끝나길 기다리게 고쳤다. 다음 main CI 의 TSan 잡으로 확인한다.
- `TypedComponents`: 인스펙터 콤보 선택기(3.8)까지 구현했다. 남은 것은 선택기의 화면 확인(`gpu-verifier`: 콤보 · `(none)` · 되돌리기 · Paste 비활성)과 앱 측정 넷(8.4). 새 후보 `StableAddressMap`(`Candidates-engine.md`).
- 문서 개편(커밋됨, **아직 push 안 함**): 백로그를 없애고 계획 문서 + `Candidates-<영역>.md` + `Roadmap.md` + `NEXT.md` 로 일원화했다. push 전에 [작업 방식](../11_Workflow.md) 3절대로 `full-build` 증거를 남긴다.
- 토큰 줄이기(2026-10-19, 전부 push 함): AGENTS.md 를 `docs/agents/` 로 나누고, 작업 방식 9절 · 보조 도구 설치 · 함정을 [에이전트와 토큰](../12_Agents.md) · [보조 도구 설치](../13_LocalTools.md) · [함정](../14_Pitfalls.md) 으로 뗐다. 도구 `edit-file` · `agent-log` · `doc`(문서 절 읽기) · `plans --section`, 30KB 넘는 .md 의 통째 읽기를 거절하는 훅(`ReadGuard`, 새 세션부터 적용), 커밋 훅 · `full-build` · `test` · `lint-suite` 출력 요약을 더했다. 전체 빌드 증거의 키는 마크다운 · `.claude/` · 빌드가 실행하지 않는 `Scripts/` 폴더 · `Test/PythonTest/*.py` 를 뺀 빌드 입력 해시라 문서만 바꾼 커밋은 다시 짓지 않는다. `worker` · `mech-worker` · `integrator` 의 `tools` 를 좁혔다(첫 기동에서 도구 이름 오류가 없는지 확인한다).
  - 압축은 효과가 작았다: `09_Decisions.md` 290KB → 274KB(-5.7%). 결정 · 이유 · 수치가 대부분이라 더 줄이려면 "수치 없는 기각 사유를 뺄지" 사람이 정해야 한다. 같은 이유로 `08_Verification`(79KB) · `TypedComponents`(87KB) · `ReflectionFormats`(68KB) 압축은 하지 않았다. 계획 문서는 끝난 단위 절을 지우는 쪽이 낫다(작업 방식 6절). `Scripts/README.md`(69KB)는 나누는 쪽이 낫다.
  - `09_Decisions.md` 5-4 의 "메모리 태그 통계" 항목 끝에 DynamicBitset 설명으로 보이는 문장("자기 기술형은 '0'·'1' 글…")이 잘못 붙어 있다. 원 위치를 확인해 옮긴다.
  - 끝난 단위 정리(push 함): `ServerBusFormat` 46→12KB, `TypedComponents` 87→26KB, `ReflectionFormats` 68→46KB, `GimmickCircuitState` 29→9KB, `DataDriven` 24→20KB. 남은 확인은 각 문서의 "남은 확인" 절과 `Candidates-network.md` 의 `ServerBusFormatLeftovers` 에 있다. `ReflectionFormats` 9절의 7-c · 7-d 행과 머리 줄 `9/11` 은 `TypedComponents` 구현이 끝난 사실에 맞춰 고쳐야 한다(작업자가 소유 때문에 못 고쳤다).
  - 측정(ccusage 20.0.36 고정, 읽기 전용): 세션 43개, 캐시 읽기가 토큰의 97.9%, 상위 2개 세션이 49%(첫째 127억 토큰). 비용은 (턴 수 × 컨텍스트 크기)에 비례해 세션 길이가 가장 큰 변수다. 큰 Bash 출력은 `sed` 19.0% · `grep` 8.8% · `cat` 3.9% 로 명령 출력이 아니라 읽기라서 RTK 는 보류, 세션 요약형 `claude-mem` 은 정본과 겹쳐 비추천, Serena 는 `clangd` 가 없어 보류. `GuardCommands` 가 30KB 넘는 `.md` 의 `cat` · 넓은 `sed -n` · `Get-Content` 를 막는다.
  - 2026-10-19 추가(커밋 전): 세션 가드 4·8·16MB, 읽기 가드가 48KB 넘는 코드까지, `Scripts/README.md` 를 `Scripts/docs/` 8개로 분할(목차 1.7KB; 분할 파일 안의 "아래 Layout 표" 같은 표현은 다른 파일을 가리키니 고칠 것), 기본 effort xhigh→high, `.mcp.json` 에서 figma 제거. 분할 파일 안의 상대 표현은 고쳤다. 비용 구성(ccusage, 43개 세션 합: 캐시 읽기 420억 · 캐시 쓰기 8.8억 · 출력 3100만 · 입력 64만 토큰)을 일반 요금 비율(읽기 0.1 · 쓰기 1.25 · 출력 5, 입력 1)로 환산하면 캐시 읽기 약 77% · 캐시 쓰기 약 20% · 출력 약 3% 라서, effort 를 내려 출력을 줄이는 이득은 작고 세션 길이(읽기)와 캐시 재생성(쓰기, 오래 쉰 뒤 재개)이 비용의 거의 전부다. 효과는 다음 ccusage 측정으로 확인한다.
  - 2026-10-19 추가(커밋 전): CLAUDE.md 7.0→5.9KB(가드 훅 절을 12 문서로 이관), 12 문서에 쉬기 전 끊기 · 본 세션 탐색 금지 · 상시 로드 억제 규칙 추가. 사용자 수준 MCP(context7 · Docs 커넥터)와 스킬은 저장소 밖 설정이라 사용자가 직접 끈다.
  - 남은 후보: 남은 래퍼 출력 요약(`Scripts/generate` 등 미확인), 12 문서 측정값 표에 이번 작업의 토큰을 `agent-log` 로 적기(분할 약 10만 · 도구 약 9만 · 훅 약 12만 · Read 훅 약 9만 · 래퍼 약 7만 · 결정 압축 셋 약 17만씩).
- 측정 · GPU 묶음(`meas`)은 한도(429)로 중단했다. 다시 하면 임시 쿠킹 폴더에서 한다(main 체크아웃에 탐침 파일을 만들지 않는다).
- 근본 원인 후보(미해결): `DiskPathVsResourceId`(디스크 경로와 리소스 id 가 같은 `string_view` 입구), `WorktreeVcpkgInstalledDir`(워크트리가 `vcpkg_installed` 를 나눠 씀).
- 세션 길이 하드 한도: 도입함(4MB · 8MB · 16MB 단계마다 한 번 차단, 압축 2회). `SessionGuard` 가 `NEXT.md` 인계와 새 세션을 안내하고, 같은 입력을 다시 보내면 통과한다.

## 2. 첫 일

1. push 뒤 CI 를 확인한다(`py -3 -m Scripts ci-jobs --runs`). TSan 잡이 통과하는지 본다.
2. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
