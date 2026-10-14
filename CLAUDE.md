# CLAUDE.md

Claude Code 안내. 이 저장소의 에이전트 규칙.

## 시작 전 필수 읽기
- **남은 일**: [docs/06_Backlog.md](docs/06_Backlog.md) — 작업 시작 전에 읽는다. 끝나면 항목을 지우고 교훈은 해당 코드의 헤더 주석 / Decisions / Verification으로 옮긴다.
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

## 보조 도구 (로컬 설치, 선택)
`.claude/skills/` 는 추적하지 않는다(`.gitignore`). 새 PC 에서는 아래대로 설치한다.
- **graphify** — 코드 지식 그래프. `py -3 -m pip install --user graphifyy` 후 `graphify install --project`
  (사용자 Scripts 폴더를 PATH 에 둔다). 그래프는 `graphify update Source` 로 만들고(약 4분) 산출물 `Source/graphify-out/` 을 저장소 루트 `graphify-out/` 으로 옮긴다(추적 안 함 — 린트가 `Source` 아래 `.md` 를 훑는다).
  질문은 저장소 루트에서 `graphify query "<질문>"` · `graphify path "<A>" "<B>"` · `graphify explain "<개념>"` 로 먼저 좁히고, 코드를 고친 뒤에도 같은 명령으로 갱신한다.
  `graphify install` 이 쓰는 PreToolUse 훅과 `graphify hook install`(git 훅)은 넣지 않는다 — PATH 에 없으면 모든 도구 호출이 걸리고, 커밋 훅(`PreCommitLint`)과 부딪힌다.
- **ponytail** — "가장 작은 변경" 지침 스킬. `.claude/skills/ponytail/SKILL.md` 를 두고 `/ponytail` 로 부를 때만 쓴다(`disable-model-invocation`).
  구조를 과감히 바꾸는 이 저장소의 방침과 충돌할 수 있어 자동으로 켜지 않는다.
- **Headroom** — 입력 압축 프록시. `py -3 -m pip install --user headroom-ai` 로 설치만 하고 `headroom wrap` 은 쓰지 않는다(사용자 전역 `~/.claude.json` 을 바꾼다).
  쓸 때는 `HEADROOM_BEACON=off` 로 익명 통계를 끄고 직접 프록시(`headroom proxy`)를 띄운다.
