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
- 토큰 줄이기(브랜치 `wt/tokenslim`, 병합 전): AGENTS.md 를 `docs/agents/` 로 나누고, 작업 방식 9절을 [에이전트와 토큰](../12_Agents.md) 으로, 보조 도구 설치를 [보조 도구 설치](../13_LocalTools.md) 로, 함정을 [함정](../14_Pitfalls.md) 으로 뗐다. 전후 토큰은 12 문서 측정값 표에 적는다.
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
