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
  - 남은 후보: 끝난 단위 절 정리(`TypedComponents` · `ReflectionFormats`), `Scripts/README.md` 분할, 남은 래퍼 출력 요약(`Scripts/generate` 등 미확인), 12 문서 측정값 표에 이번 작업의 토큰을 `agent-log` 로 적기(분할 약 10만 · 도구 약 9만 · 훅 약 12만 · Read 훅 약 9만 · 래퍼 약 7만 · 결정 압축 셋 약 17만씩).
- 측정 · GPU 묶음(`meas`)은 한도(429)로 중단했다. 다시 하면 임시 쿠킹 폴더에서 한다(main 체크아웃에 탐침 파일을 만들지 않는다).
- 근본 원인 후보(미해결): `DiskPathVsResourceId`(디스크 경로와 리소스 id 가 같은 `string_view` 입구), `WorktreeVcpkgInstalledDir`(워크트리가 `vcpkg_installed` 를 나눠 씀).
- 사용자 결정 대기: 없음.

## 2. 첫 일

1. push 뒤 CI 를 확인한다(`py -3 -m Scripts ci-jobs --runs`). TSan 잡이 통과하는지 본다.
2. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
