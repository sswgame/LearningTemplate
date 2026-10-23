# CLAUDE.md

Claude Code 안내. 이 저장소의 에이전트 규칙.

## 시작 전 필수 읽기
- **남은 일**: [docs/plans/NEXT.md](docs/plans/NEXT.md)(지금 상태) → [docs/plans/Roadmap.md](docs/plans/Roadmap.md)(순서) → 내 일의 계획 문서(`py -3 -m Scripts plans`로 목록, `--file <이름>`으로 한 문서, `--find <옛번호>`로 옛 번호 풀기). 백로그는 없다. 착수한 일은 계획 문서, 착수 전 후보는 `docs/plans/Candidates-<영역>.md`의 절이다. 일의 식별자는 번호가 아니라 이름이다. 끝나면 **직접** 그 계획 문서의 단위와 `진행`을 고치고 후보는 계획 문서로 옮기며, 교훈은 해당 코드의 헤더 주석 / Decisions / Verification으로 옮긴다([작업 방식](docs/11_Workflow.md) 6절). 최상단 README의 진행 표는 생성물이라 고치지 않는다.
- **큰 문서는 절만 읽는다**: `py -3 -m Scripts doc <파일>`(목차) → `--section <제목 일부 또는 5-4 같은 번호>`. 훅이 30KB 넘는 `.md` 의 통째 읽기를 거절한다.
- **작업 방식**: [docs/11_Workflow.md](docs/11_Workflow.md) — 워크트리, 동시 빌드 제한(최대 2), 검증 단계, push 규칙, 커밋 형식.
- **코딩 규칙**: [AGENTS.md](AGENTS.md)는 공통 규칙과 목차(5KB)다. 일에 맞는 파일만 `docs/agents/`에서 읽는다: 이름은 `NamingCpp`(C++) · `NamingScripts`(CMake · Python · HLSL), 새 파일 · include는 `Includes`, 상수는 `Constants`, 도우미는 `Helpers`, 스타일은 `CppStyle`, 스크립트 편집은 `EditingTraps`. (`docs/04_CodingGuidelines.md`는 예시만)
- **스크립트로 파일을 고칠 때**: heredoc 금지 — [EditingTraps](docs/agents/EditingTraps.md) 첫 항목.

문서·주석은 한국어. 문서 작성 규칙은 `docs/10_WritingDocs.md`.
새로 쓰는 모든 글(답변 · 에이전트 지시 · 커밋 메시지 · 주석 · 문서)은 그 문서의 용어 대조표를 따른다(예: 시험 → 테스트). 에이전트에게 일을 시킬 때 지시문에도 넣는다.

## 해결 원칙 — 구조로 푼다 (사용자 지시, 상시)
**원천적인 원인을 구조로 해결한다.** 같은 모양의 결함이 한 번 더 나올 수 있으면 증상을 고치지 않고, 그 모양을 쓸 수 없게 만드는 형식 · API · 데이터 구조를 바꾼다(예: 경로를 문자열이 아닌 `DataPath` 형식으로, 읽기 결과를 `[[nodiscard]]` 로).
린트 게이트 · 주석 · 문서 규칙 · 코드 리뷰는 **최후의 수단**이다. 게이트부터 만들지 않고, 형식으로 막을 수 있는지 먼저 따진다. 막지 못하는 틈에만 게이트를 두고 그 이유를 적는다.
폴백(없으면 조용히 기본값)을 두지 않는다 — 필수 값이 없으면 오류로 멈춘다. 값의 정본은 데이터이고 코드는 형식만 가진다([데이터 주도 구조 정리](docs/plans/DataDriven.md)).
에이전트에게 일을 시킬 때도 이 원칙을 지시문에 적는다. 일을 마치면 "같은 결함이 다른 곳에 있을 수 있는가 · 근본 원인은 무엇인가"를 한 줄로 보고한다.

## 에이전트 · 토큰 예산
`.claude/agents/`에 정의됨. **일에 맞는 가장 싼 모델을 고른다.** 일과 에이전트 · effort 표는 [docs/12_Agents.md](docs/12_Agents.md).

에이전트 지시문에는 **읽을 문서와 절을 이름으로 적고, 끝낼 때 직접 고칠 문서도 적는다**(틀은 [작업자 규칙](docs/plans/WorkerRules.md) 5절). 찾기만 하는 일은 `scout`, 보고는 열 줄 안쪽. `model`과 `effort`를 표대로 **지정하고**, 사용자에게 종류 · 모델 · 레벨을 한 줄로 알린다.

**병렬 작업은 작업자가 끝까지 일하고(린트 · 커밋만, 전체 빌드 · 병합 · push 금지), 한 곳에 모두 병합해 한 번에 검증한다**(사용자 지시, 되풀이: [작업 방식](docs/11_Workflow.md) 1절, [작업자 규칙](docs/plans/WorkerRules.md) 5절). 브랜치 하나가 끝났다고 따로 병합 · 전체 빌드를 하지 않는다.

요청이 규칙과 충돌하거나 한도에 닿으면 **실행 전에 한 줄로 알린다**. 긴 세션은 `docs/plans/NEXT.md`에 인계 후 새 세션.

## 빌드 (자주 쓰는 것만)
```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug                    # App·Server·모듈
cmake --build --preset Ninja-Debug --target all AllTests  # +테스트
```

## 보조 도구 (로컬 설치, 선택)
설치 방법은 [보조 도구 설치](docs/13_LocalTools.md). 쓰는 규칙만 여기 둔다.

- **graphify**: 항상 켠다. 코드 질문은 저장소 루트에서 `graphify query "<질문>"`, `graphify path "<A>" "<B>"`, `graphify explain "<개념>"`으로 범위를 먼저 좁힌다(작업자 워크트리는 MCP `query_graph`). 코드를 고친 뒤에는 `graphify update Source`로 갱신한다(약 4분). PreToolUse · git 훅은 공유 설정에 넣지 않는다.
- **ponytail**: `.claude/skills/ponytail/SKILL.md`가 있으면 코딩 · 문서 · 설정 등 모든 작업마다 켠다. 자동이 아니라서 세션 시작 훅이 `Skill` 로 호출하라고 알린다(2026-10-15 사용자 결정). "기존 구조를 유지한다"는 지침이 "구조는 장기 이득이면 과감히 바꾼다"는 방침과 부딪힐 수 있으니, 구조 개편을 시킬 때는 요청에 그 뜻을 적는다.

## 가드 훅
훅(`GuardCommands` · `SessionGuard` · `ReadGuard`)이 `git commit --no-verify`, 링크가 든 폴더의 재귀 삭제, 너무 긴 세션, 큰 파일의 통째 읽기를 막는다. 내용과 보조 에이전트(gpu-verifier · code-reviewer · design-reviewer · ux-reviewer)는 [에이전트와 토큰](docs/12_Agents.md). 워크트리는 `py -3 -m Scripts worktree-remove <이름>`으로 지운다.
- 워크트리는 손으로 정션을 걸지 말고 `py -3 -m Scripts worktree-make <이름> --base main`으로 만든다. 기준 브랜치를 `main`으로 주지 않으면 `origin/main`에서 갈라진다.
