# CLAUDE.md

Claude Code 안내. 이 저장소의 에이전트 규칙.

## 시작 전 필수 읽기
- **남은 일**: [docs/06_Backlog.md](docs/06_Backlog.md) — 작업 시작 전에 읽는다. 끝나면 항목을 지우고 교훈은 해당 README / Decisions / Verification으로 옮긴다.
- **작업 방식**: [docs/11_Workflow.md](docs/11_Workflow.md) — 워크트리, 동시 빌드 제한(최대 2), 검증 단계, push 규칙, 커밋 형식.
- **코딩 규칙**: [AGENTS.md](AGENTS.md) — 이름·함수 어휘·헤더 순서 등. C++/CMake/Python을 쓰기 전에 반드시 읽는다. (`docs/04_CodingGuidelines.md`는 예시만)
- **스크립트로 파일을 고칠 때**: heredoc 금지 — [AGENTS.md](AGENTS.md) "Editing traps" 첫 항목.

문서·주석은 한국어, 로그/assert 문자열은 영어. 문서 작성 규칙은 `docs/10_WritingDocs.md`.
새로 쓰는 모든 글(답변 · 에이전트 지시 · 커밋 메시지 · 주석 · 문서)은 그 문서의 용어 대조표를 따른다(예: 시험 → 테스트). 에이전트에게 일을 시킬 때 지시문에도 넣는다.

## 에이전트 · 토큰 예산
`.claude/agents/`에 정의됨. **일에 맞는 가장 싼 모델을 고른다.** 일과 에이전트 · effort 표는 [docs/11_Workflow.md](docs/11_Workflow.md) 9절.

요청이 규칙과 충돌하거나 한도에 닿으면 **실행 전에 한 줄로 알린다**.
긴 세션은 `docs/plans/NEXT.md`에 인계 후 새 세션 권장.

## 빌드 (자주 쓰는 것만)
```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug                    # App·Server·모듈
cmake --build --preset Ninja-Debug --target all AllTests  # +테스트
