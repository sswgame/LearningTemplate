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
- 토큰 줄이기(2026-10-19, 푸시함): 규칙과 측정(ccusage 43개 세션: 캐시 읽기 약 77% · 쓰기 약 20% · 출력 약 3%)은 [에이전트와 토큰](../12_Agents.md) 에 있다. 비용의 거의 전부가 세션 길이와 캐시 재생성이라 effort 를 더 내릴 이득은 작다. 압축은 효과가 작았다(`09_Decisions.md` -5.7%). 계획 문서는 끝난 단위 절을 지우는 쪽이 낫다(작업 방식 6절).
  - 남은 후보: 래퍼 출력 요약(`Scripts/generate` 등, 크기 미측정), 다음 ccusage 측정으로 오늘 변경(세션 가드 · 읽기 가드 · 문서 분할 · MCP 정리)의 효과 확인. 사용자 수준 MCP · 계정 스킬은 저장소 밖 설정이다.
- 측정 · GPU 묶음(`meas`)은 한도(429)로 중단했다. 다시 하면 임시 쿠킹 폴더에서 한다(main 체크아웃에 탐침 파일을 만들지 않는다).
- 근본 원인 후보(미해결): `DiskPathVsResourceId`(디스크 경로와 리소스 id 가 같은 `string_view` 입구), `WorktreeVcpkgInstalledDir`(워크트리가 `vcpkg_installed` 를 나눠 씀).
- 세션 길이 하드 한도: 도입함(4MB · 8MB · 16MB 단계마다 한 번 차단, 압축 2회). `SessionGuard` 가 `NEXT.md` 인계와 새 세션을 안내하고, 같은 입력을 다시 보내면 통과한다.

## 2. 첫 일

1. push 뒤 CI 를 확인한다(`py -3 -m Scripts ci-jobs --runs`). TSan 잡이 통과하는지 본다.
2. 그다음은 [로드맵](Roadmap.md) 의 `## 지금 순서` 맨 위부터. **`[병렬 가능]` 표시된 일은 파일 소유가 겹치지 않으면 병렬로 띄운다**(세션 시작 훅이 그 목록을 알린다). `[단독: …]` 은 혼자, `[확인 필요]` 는 먼저 확인한다. 인계를 쓸 때 로드맵의 표시를 다시 확인하고, 일을 끝내면 그 줄의 표시도 맞춘다.

## 3. 새 세션 시작 절차

1. 이 문서만 먼저 읽는다. 일을 정하면 그 일의 계획 문서를 `py -3 -m Scripts plans --file <이름>` 으로 읽고, [작업 방식](../11_Workflow.md) 은 필요한 절(빌드 2절 · 검증 3절 · push 4절)만 읽는다. 에이전트를 띄울 때만 [에이전트와 토큰](../12_Agents.md) 과 [작업자 규칙](WorkerRules.md). C++ 을 쓰기 전에 [AGENTS.md](../../AGENTS.md) 목차에서 일에 맞는 파일만.
2. `git worktree list` 로 남은 워크트리를 보고 병합이 끝난 것을 지운다.
3. 큰 일은 사용자와 순서를 확인한 뒤 시작한다. 시작 전에 graphify 그래프를 갱신한다(`graphify update Source`, 약 4 분).
