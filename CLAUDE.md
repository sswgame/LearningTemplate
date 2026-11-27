# CLAUDE.md

할 일 · 계획은 `docs/NEXT.md` 하나에 두고, 코딩 규칙은 `AGENTS.md` 목차에서 찾는다. 아래 원칙 넷을 지킨다.

문서 · 주석 · 지시문은 한국어로 쓰고, 지어낸 말 대신 업계 용어를 쓴다(예: 시험 → 테스트). `docs/` 에는 계획 · 할 일 · 규칙만 둔다. 설명 · 배경 · 끝난 일의 기록은 git 에 남긴다.

## 원칙

1. **구조로 푼다.** 같은 결함이 또 나올 수 있으면 그 모양을 쓸 수 없게 형식 · API · 데이터 구조를 바꾼다(예: 경로는 `DataPath`, 읽기 결과는 `[[nodiscard]]`). 린트 · 주석 · 문서 규칙은 형식으로 못 막는 틈에만 이유와 함께 둔다. 필수 값이 없으면 폴백 없이 오류로 멈춘다. 일을 마치면 근본 원인과, 같은 결함이 다른 곳에 있을 가능성을 한 줄로 보고한다.
2. **검증은 묶음 끝에 한 번.** 단계마다 코드 → 린트(`py -3 -m Scripts lint`) → 커밋만 하고, 빌드 · 검증 · 측정 · 병합 · push 는 묶음 끝에 한 번씩 한다. 병렬 작업자는 최신 main 에서 워크트리를 만들어 커밋하고 멈춘다. 통합 담당이 모아 검증한다.
3. **상용 엔진(언리얼 · 유니티 · Godot)과 비교해 그 자리에서 정한다.** 이유는 코드 주석이나 계획 문서에 한 줄로 적는다. 사용자에게는 법 · 라이선스, 외부 공개, 비용이 드는 연동, 되돌리기 어려운 삭제만 묻는다.
4. **대기를 쌓지 않는다.** 결정 대기 · 손 확인 대기를 인계 문서에 쌓지 않는다. 확인은 자동화 시나리오로 만들어 통합에서 한 번에 돌린다.

요청이 규칙과 충돌하거나 한도에 닿으면 실행 전에 한 줄로 알린다. 세션이 길어지면 `docs/NEXT.md` 에 인계를 적고 새 세션을 권한다. 30KB 넘는 `.md` 는 제목(`## …`)을 검색해 그 절만 연다.

## 보조 도구

훅이 강제한다. 규칙 데이터는 `.claude/hooks/` 의 `ToolPolicyRules.json` · `HookRules.json` 이고, 테스트는 `py -3 .claude/hooks/test_tool_policy.py` 다. 다음 호출은 거절되고 할 일이 안내된다.

- 아래 스킬 · graphify 를 먼저 하지 않은 호출
- 금지 명령(git stash · 워크트리 빌드 · 폴링 루프 · `git mv` 로 소스 · 에셋 옮기기)
- 생성물 · 기준선 손 편집
- 작업 종류 표에 맞지 않거나 필수 문구가 빠진 작업자 호출

기계로 판정되는 규칙은 문서 대신 이 규칙 데이터에 더한다.

| 언제 | 무엇 |
| --- | --- |
| 모든 작업 | `ponytail`. 구조 개편을 시킬 때는 요청에 "구조는 장기 이득이면 과감히 바꾼다"를 적는다(스킬의 "기존 구조 유지"와 부딪힌다) |
| 주석 · 커밋 메시지 · .md 를 쓸 때 | `humanizer`(저장소 스킬. Source · Scripts · Content · Config · Test · .md 편집과 git commit 앞에서 훅이 요구) |
| 하위 작업자에게 맡길 때 | `agent-dispatch`. 지시문 첫 줄에 `작업 종류: <키>` 를 적고, subagent_type · model · effort 를 표(`HookRules.json` 의 `agentPrompt.kinds`)에서 고른다 |
| 코드를 읽기 전 | 아래 "코드 인텔리전스 우선" 의 도구 차례. graphify 는 워크트리에서 `--graph "$(git rev-parse --path-format=absolute --git-common-dir)/../graphify-out/graph.json"` 을 붙이고, 그래프 갱신은 Stop 훅이 한다. 작업자 보고에 찾은 범위를 한 줄 적는다 |
| 에디터 UI | `taste-skill` 의 `references/app-screens.md`. 확인은 `editor/uicapture.scenario.xml` 스크린샷 |
| 묶음 끝 검증 · push / Linux / 시나리오 실패 | `preflight` / `linux-verify` / `scenario-debug`. 임시 스크립트를 새로 만들지 말고 여기에 더한다 |
| 외부 API(RHI 백엔드 · Android · 웹) | `source-driven-development`. 그 판의 공식 문서를 먼저 찾는다 |
| 결함 추적 · 성능 · 온라인 · 전수 이전 · 통합 전 읽기 | `debugging-and-error-recovery` · `performance-optimization`(측정 먼저) · `security-and-hardening`(온라인 서버 · HTTP · 로그인 · 영수증은 반드시) · `deprecation-and-migration` · `code-review-and-quality`(독립 검토는 에이전트 `code-reviewer`) |
| 이전 세션에서 다뤘나 | claude-mem `mem-search`. 찾은 것은 코드 · 문서와 대조한 뒤에만 쓴다 |

## 코드 인텔리전스 우선

소스를 셸 명령(grep · rg · sed · awk · cat · find · `git grep`)으로 읽거나 찾지 않는다. Bash 는 테스트 · 빌드 · Git · 패키지 관리 같은 실행에만 쓰고, 실행 결과를 `| grep` 으로 거르는 것은 된다. 훅 `no-shell-code-exploration` · `code-intel-before-grep` 이 막는다.

| 묻는 것 | 먼저 쓸 도구 |
| --- | --- |
| 구조 · 주요 모듈 · 문서와 설계 결정 | Graphify(`graphify query` · `path` · `explain`) |
| 호출하는 곳 · 의존하는 곳 · 바꿨을 때 영향 · 실행 경로 | CodeGraph(MCP `codegraph_callers` · `codegraph_callees` · `codegraph_impact` · `codegraph_explore`) |
| 심볼 정의 · 참조 · 함수 몸통 읽기 · 이름 바꾸기 · 심볼 단위 고치기 | Serena(MCP `find_symbol` · `find_referencing_symbols` · `get_symbols_overview` · `replace_symbol_body` · `rename_symbol`) |
| 파일 읽기 · 고치기 · 만들기 | Read · Edit · Write |

1. 맞는 코드 인텔리전스 도구를 먼저 쓰고, 그 전에 grep · sed 를 쓰지 않는다.
2. 대화에 이미 있는 결과는 다시 찾지 않는다.
3. 그래프 질의로 관련 파일을 고를 수 있으면 저장소 전체를 훑지 않는다.
4. 심볼 단위의 정확한 변경은 Serena 로 한다.
5. Graphify 는 구조 · 문서 맥락이 필요할 때만 쓴다. 코드 구조 질의는 CodeGraph 다.

전용 도구로 답이 안 나오면 그 이유를 한 줄로 말하고 Glob · Read 를 쓴다. Grep 도구는 마지막 수단이고, 일상적인 소스 편집에 sed 를 쓰지 않는다.

설치: CodeGraph 는 `npm i -g @colbymchenry/codegraph` 뒤 저장소 루트에서 `codegraph init -y --local-only`(인덱스 `.codegraph/` 는 git 밖, `codegraph telemetry off`). Serena 는 `uv tool install git+https://github.com/oraios/serena`(C++ 는 clangd 를 스스로 받고 `.clangd` 의 컴파일 DB 를 쓴다, 프로젝트 설정 `.serena/project.yml`). MCP 는 사람마다 `claude mcp add --scope local` 로 등록한다(경로가 사람마다 다르다).
