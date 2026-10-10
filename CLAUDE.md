# CLAUDE.md

Claude Code 안내. 이 저장소의 에이전트 규칙.

## 시작 전 필수 읽기
- **남은 일**: [docs/06_Backlog.md](docs/06_Backlog.md) — 작업 시작 전에 읽는다. 끝나면 항목을 지우고 교훈은 해당 README / Decisions / Verification으로 옮긴다.
- **작업 방식**: [docs/11_Workflow.md](docs/11_Workflow.md) — 워크트리, 동시 빌드 제한(최대 2), 검증 단계, push 규칙, 커밋 형식.
- **코딩 규칙**: [AGENTS.md](AGENTS.md) — 이름·함수 어휘·헤더 순서 등. C++/CMake/Python을 쓰기 전에 반드시 읽는다. (`docs/04_CodingGuidelines.md`는 예시만)

문서·주석은 한국어, 로그/assert 문자열은 영어. 문서 작성 규칙은 `docs/10_WritingDocs.md`.

## 에이전트 · 토큰 예산
`.claude/agents/`에 정의됨. **가장 싼 모델을 고른다.**

| 일 | 에이전트 | effort |
|----|----------|--------|
| 파일·호출부·값 찾기 | `scout` (haiku) | low |
| 이름변경·이동·문서·반복편집 | `mech-worker` (sonnet) | low |
| 범위 정해진 C++/CMake/Python | `worker` (opus, 국소적이면 sonnet) | medium |
| 설계·크로스모듈·GPU/스레드·통합 | `integrator` (opus) | high |

요청이 규칙과 충돌하거나 한도에 닿으면 **실행 전에 한 줄로 알린다**.
긴 세션은 `docs/plans/NEXT.md`에 인계 후 새 세션 권장.

## 빌드 (자주 쓰는 것만)
```powershell
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug                    # App·Server·모듈
cmake --build --preset Ninja-Debug --target all AllTests  # +테스트
