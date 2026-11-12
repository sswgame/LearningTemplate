# CLAUDE.md

> 질문: 에이전트가 매 세션 시작에 읽고 지킬 것은 무엇인가?
> 독자: 이 저장소에서 일하는 에이전트.
> 결론: 남은 일은 `docs/NEXT.md`, 코딩 규칙은 `AGENTS.md` 목차에서 찾는다. 원칙 넷을 어기지 않는다.

문서 · 주석 · 지시문은 한국어로 쓰고, 지어낸 말 대신 업계 용어를 쓴다(예: 시험 → 테스트).

## 읽을 것

- 남은 일: [NEXT](docs/NEXT.md).
- 코딩 규칙: [AGENTS.md](AGENTS.md) 목차에서 일에 맞는 `docs/agents/` 파일만.
- 30KB 넘는 `.md` 는 통째로 읽지 않는다 — 제목(`## …`)을 검색해 그 절만 연다.

## 원칙

1. **구조로 푼다.** 같은 결함이 또 나올 수 있으면 그 모양을 쓸 수 없게 형식 · API · 데이터 구조를 바꾼다(예: 경로는 `DataPath`, 읽기 결과는 `[[nodiscard]]`). 린트 · 주석 · 문서 규칙은 형식으로 못 막는 틈에만, 이유와 함께 둔다. 폴백 없이 필수 값이 없으면 오류로 멈춘다. 일을 마치면 근본 원인과 같은 결함이 다른 곳에 있을 가능성을 한 줄로 보고한다.
2. **검증은 묶음 끝에 한 번.** 단계마다 코드 → 린트(`py -3 -m Scripts lint`) → 커밋만 하고, 빌드 · 검증 · 측정 · 병합 · push 는 묶음 끝에 한 번씩. 병렬 작업자는 최신 main 에서 워크트리를 만들어 커밋하고 멈추며, 통합 담당이 모아 검증한다.
3. **상용 엔진(언리얼 · 유니티 · Godot)과 비교해 그 자리에서 정한다.** 이유는 코드 주석이나 계획 문서에 한 줄. 사용자에게는 법 · 라이선스, 외부 공개, 비용이 드는 연동, 되돌리기 어려운 삭제만 묻는다.
4. **대기를 쌓지 않는다.** 결정 대기 · 손 확인 대기를 인계 문서에 쌓지 않고, 확인은 자동화 시나리오로 만들어 통합에서 한 번에 돌린다.

요청이 규칙과 충돌하거나 한도에 닿으면 실행 전에 한 줄로 알린다. 긴 세션은 `docs/NEXT.md` 에 인계를 적고 새 세션을 권한다.

## 보조 도구

훅이 강제한다(`.claude/hooks/ToolGate.py`, 규칙 데이터 `ToolGateRules.json`, 테스트 `py -3 .claude/hooks/TestToolGate.py`): 요구한 스킬 · graphify 를 먼저 하지 않으면 호출이 거절되고 할 일이 안내된다. 새 스킬을 들이면 아래에 쓸 자리를 한 줄 더한다.

| 언제 | 무엇 |
| --- | --- |
| 모든 작업 | `ponytail`. 구조 개편을 시킬 때는 "구조는 장기 이득이면 과감히 바꾼다"를 요청에 적는다(스킬의 "기존 구조 유지"와 부딪힌다) |
| 코드를 읽기 전 | graphify: 메인 작업 폴더에서 `graphify query "<질문>"`(`path` · `explain` 도), 워크트리에서는 `--graph "$(git rev-parse --path-format=absolute --git-common-dir)/../graphify-out/graph.json"` 을 붙인다. 그래프는 루트 `graphify-out/` 하나 — 손으로 갱신할 때 `GRAPHIFY_OUT=<루트>/graphify-out graphify update Source`(빼면 `Source/graphify-out/` 이 생겨 게이트가 멈춘다). 작업자 보고에 찾은 범위를 한 줄 |
| 에디터 UI(ImGui 패널 · `Content/editor/theme/*.tokens.json`) | `taste-skill` 의 `references/app-screens.md` — 강조색은 데이터 · 선택에만, 켜진 토글은 중립 단계, 본문은 그로테스크 한 벌(로그만 고정폭). 확인은 `editor/uicapture.scenario.xml` 스크린샷 |
| 묶음 끝 검증 · push / Linux / 시나리오 실패 | `bundle-verify` / `linux-verify` / `scenario-debug`. 임시 스크립트를 새로 만들지 말고 여기에 더한다 |
| 외부 라이브러리 · SDK API, 판 올림 | context7(`.mcp.json`) — 그 판의 문서를 먼저 찾는다 |
| 이전 세션에서 다뤘나 | claude-mem `mem-search` — 찾은 것은 코드 · 문서와 대조한 뒤에만 쓴다(정본은 `docs/` 와 자동 기억) |
| 결함 추적 · 성능 · 서버 · 온라인 · 입력 검증 · 전수 이전 · 설계 판단 · 통합 전 읽기 | `debugging-and-error-recovery` · `performance-optimization`(측정 먼저) · `security-and-hardening`(온라인 서버 · HTTP · 로그인 · 영수증은 반드시) · `deprecation-and-migration` · `documentation-and-adrs`(결과는 `docs/DesignBacklog.md` 판단 절) · `code-review-and-quality` |

계획 · 질문 · 배포 스킬(`spec` · `plan` · `interview-me` · `ship` · `git-workflow-and-versioning`)이 이 문서와 부딪히면 이 문서를 따른다(질문 대기를 쌓지 않고, push 는 `bundle-verify` 만).
