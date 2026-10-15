# CLAUDE.md

Claude Code 안내. 이 저장소의 에이전트 규칙.

## 시작 전 필수 읽기
- **남은 일**: [docs/backlog/README.md](docs/backlog/README.md) — 백로그는 영역별 파일로 나뉘어 있고 통째로 읽지 않는다. 안내 문서에서 상태 태그와 일을 고르는 순서를 보고, 내 일의 항목은 `py -3 -m Scripts backlog --id <번호>`(예: `C-2`)로 본문만 읽는다. 끝나면 항목을 지우고 교훈은 해당 코드의 헤더 주석 / Decisions / Verification으로 옮긴다.
- **작업 방식**: [docs/11_Workflow.md](docs/11_Workflow.md) — 워크트리, 동시 빌드 제한(최대 2), 검증 단계, push 규칙, 커밋 형식.
- **코딩 규칙**: [AGENTS.md](AGENTS.md) — 이름·함수 어휘·헤더 순서 등. 51KB이므로 `grep -n '^#'`로 절을 찾아 일에 맞는 절만 읽는다: 이름은 `Naming`(CMake · C++ · Python · HLSL), 새 파일 · include는 `C++ structure and includes`, 상수는 `Constants`, 도우미는 `Helpers`, 스타일은 `C++ style`, 스크립트 편집은 `Editing traps`. (`docs/04_CodingGuidelines.md`는 예시만)
- **스크립트로 파일을 고칠 때**: heredoc 금지 — [AGENTS.md](AGENTS.md) "Editing traps" 첫 항목.

문서·주석은 한국어. 문서 작성 규칙은 `docs/10_WritingDocs.md`.
새로 쓰는 모든 글(답변 · 에이전트 지시 · 커밋 메시지 · 주석 · 문서)은 그 문서의 용어 대조표를 따른다(예: 시험 → 테스트). 에이전트에게 일을 시킬 때 지시문에도 넣는다.

## 에이전트 · 토큰 예산
`.claude/agents/`에 정의됨. **일에 맞는 가장 싼 모델을 고른다.** 일과 에이전트 · effort 표는 [docs/11_Workflow.md](docs/11_Workflow.md) 9절.

에이전트 지시문에는 **읽을 문서와 절을 이름으로 적는다**(예: "AGENTS.md `Naming`과 `Editing traps`만", "백로그 `C-2` 항목만"). 에이전트는 문서를 통째로 읽기 쉽고, 작업자마다 같은 수만 토큰이 반복된다. 찾기만 하는 일은 `scout`, 결과 보고는 열 줄 안쪽으로 받는다.

요청이 규칙과 충돌하거나 한도에 닿으면 **실행 전에 한 줄로 알린다**.
긴 세션은 `docs/plans/NEXT.md`에 인계 후 새 세션 권장.

## 빌드 (자주 쓰는 것만)
```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug                    # App·Server·모듈
cmake --build --preset Ninja-Debug --target all AllTests  # +테스트

## 보조 도구 (로컬 설치, 선택)
`.claude/skills/`는 추적하지 않는다(`.gitignore`). 새 PC에서는 아래대로 설치한다.

- **graphify**: 코드 지식 그래프다. `py -3 -m pip install --user graphifyy`로 설치하고 `graphify install --project`로 스킬을 등록한다. 사용자 Scripts 폴더가 PATH에 있어야 한다.
  - 그래프는 `graphify update Source`로 만든다(약 4분). 산출물 `Source/graphify-out/`은 저장소 루트의 `graphify-out/`으로 옮긴다. 추적하지 않는다. `Source` 안에 두면 린트가 그 아래 `.md`를 읽을 수 있어서 옮긴다.
  - 항상 켠다. 코드 질문은 저장소 루트에서 `graphify query "<질문>"`, `graphify path "<A>" "<B>"`, `graphify explain "<개념>"`으로 범위를 먼저 좁힌다. 코드를 고친 뒤에는 같은 `update` 명령으로 그래프를 갱신한다.
  - PreToolUse 훅은 공유 `settings.json`에 넣지 않는다. 이 PC 밖에서는 `graphify`가 PATH에 없어 모든 도구 호출이 막힌다. 필요하면 개인 `settings.local.json`에 절대 경로로 둔다.
  - git 훅(`graphify hook install`)도 넣지 않는다. `update`는 증분 갱신이 아니라 `Source` 전체를 다시 읽어 3~4분이 걸린다. 설치하면 워크트리 여럿에서 커밋할 때마다 이 시간이 든다. `pre-commit` 훅과는 충돌하지 않는다.
  - 작업자 워크트리에서는 스킬과 안내 훅이 보이지 않으므로 MCP 서버로도 연결한다. 저장소 루트 `.mcp.json`이 `py -3 -m graphify.serve graphify-out/graph.json`을 띄우고, `query_graph` 도구로 질의한다. `py -3 -m pip install --user "graphifyy[mcp]"`가 필요하고, `graphify-out/`이 있는 폴더(main 체크아웃)에서 쓴다.
- **ponytail**: "가장 작은 변경"을 요구하는 지침 스킬이다. `.claude/skills/ponytail/SKILL.md`를 두면 코딩 작업마다 자동으로 켜진다(2026-10-15 사용자 결정). `/ponytail lite|full|ultra`로 강도를 바꾼다.
  - "기존 구조를 유지한다"는 지침이 이 저장소의 "구조는 규모가 커도 장기 이득이면 과감히 바꾼다"는 방침과 부딪힐 수 있다. 구조 개편을 시킬 때는 요청에 그 뜻을 분명히 적는다.

## 가드 훅과 보조 에이전트
- **`.claude/hooks/GuardCommands.py`**: Bash · PowerShell 호출 직전에 두 가지를 막는다. `git commit --no-verify`(커밋 훅은 우회하지 않는다)와, 링크(정션)가 든 폴더의 재귀 삭제다. 후자는 `Tools/LLVM` 같은 공유 도구가 함께 지워지는 것을 막는다. 워크트리는 `py -3 -m Scripts worktree-remove <이름>`으로 지운다. 훅이 실패하면 호출을 막지 않는다.
- **`gpu-verifier`** 에이전트: GPU · 창 실행을 `py -3 -m Scripts shared-lock acquire gpu`로 직렬화해 맡고 캡처를 눈으로 확인해 보고한다. **`code-reviewer`** 에이전트: 병합 전에 브랜치 diff를 읽기 전용으로 검토한다. 작업자끼리 부딪히는 결정과 이 저장소의 규칙 위반을 찾는다. **`ux-reviewer`** 에이전트: 에디터 UI 캡처(PNG)를 읽기 전용으로 보고 UX 결함 표를 낸다. 기준은 [docs/plans/EditorUX.md](docs/plans/EditorUX.md).
- 워크트리는 손으로 정션을 걸지 말고 `py -3 -m Scripts worktree-make <이름> --base main`으로 만든다. 기준 브랜치를 `main`으로 주지 않으면 `origin/main`에서 갈라진다.
