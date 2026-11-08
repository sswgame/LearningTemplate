# CLAUDE.md

> 질문: 에이전트가 매 세션 시작에 읽고 지킬 것은 무엇인가?
> 독자: 이 저장소에서 일하는 에이전트.
> 결론: 남은 일은 `NEXT.md` → 거기서 가리키는 계획 문서 순으로 읽고, 코딩 규칙은 `AGENTS.md` 로 간다. 원칙 넷을 어기지 않는다.

문서 · 주석 · 지시문은 한국어로 쓴다(예: 시험 → 테스트).

## 읽을 것

- 남은 일: [NEXT](docs/NEXT.md)(지금 상태와 구조 개편 계획).
- 큰 문서: 30KB 넘는 `.md` 는 통째로 읽지 않는다 — 제목(`## …`)을 검색해 그 절만 부분 읽기로 연다.
- 코딩 규칙: [AGENTS.md](AGENTS.md) 목차에서 일에 맞는 `docs/agents/` 파일만 읽는다.

## 원칙

1. **구조로 푼다.** 같은 결함이 또 나올 수 있으면 증상이 아니라 그 모양을 쓸 수 없게 만드는 형식 · API · 데이터 구조를 바꾼다(예: 경로는 `DataPath`, 읽기 결과는 `[[nodiscard]]`). 게이트 · 주석 · 문서 규칙은 형식으로 막지 못하는 틈에만 두고 이유를 적는다. 폴백은 두지 않고 필수 값이 없으면 오류로 멈춘다. 일을 마치면 근본 원인과 같은 결함이 다른 곳에 있을 가능성을 한 줄로 보고한다.
2. **한 에이전트가 단계별로 진행하고 검증은 묶음 끝에 한 번이다.** 단계마다 코드 → 린트(`py -3 -m Scripts lint`) → 커밋만 하고, 빌드 · 검증 · 측정 · 병합 · push 는 묶음이 끝난 뒤 같은 에이전트가 한 번씩만 한다. 병렬 작업자는 커밋하고 멈추며, 통합 담당이 모아 검증한다.
3. **상용 엔진(언리얼 · 유니티 · Godot)과 비교해 그 자리에서 정한다.** 이유는 코드 주석이나 계획 문서에 한 줄 남긴다. 사용자에게는 법 · 라이선스, 외부 공개, 비용이 드는 연동, 되돌리기 어려운 삭제만 묻는다.
4. **대기를 쌓지 않는다.** 결정 대기와 손 확인 대기를 인계 문서에 쌓지 않고, 확인은 자동화 시나리오로 만들어 통합에서 한 번에 돌린다.

요청이 규칙과 충돌하거나 한도에 닿으면 실행 전에 한 줄로 알린다. 긴 세션은 `docs/NEXT.md` 에 인계를 적고 새 세션을 권한다.

## 보조 도구

- **도구 게이트:** 아래 도구 · 스킬은 글 규칙이 아니라 훅이 강제한다(`.claude/hooks/ToolGate.py`, 규칙은 데이터 `.claude/hooks/ToolGateRules.json`, 테스트 `py -3 .claude/hooks/TestToolGate.py`). 그 일 앞에서 요구한 스킬 · graphify 를 하지 않으면 호출이 거절되고 먼저 할 일이 안내된다. 새 스킬을 들이면 쓸 자리를 표에 한 줄 더한다 — 설치만 하고 쓰지 않는 스킬을 두지 않는다.
- graphify: 코드를 읽기 전에 그래프로 범위를 좁힌다 — 본 트리 루트에서는 `graphify query "<질문>"`(`path "<A>" "<B>"` · `explain "<개념>"` 도), 워크트리에서는 같은 명령에 `--graph "$(git rev-parse --path-format=absolute --git-common-dir)/../graphify-out/graph.json"` 을 붙인다(그래프는 본 저장소 루트 `graphify-out/` 하나다 — 코드 폴더 안에 두지 않는다). 갱신은 Stop 훅이 `GRAPHIFY_OUT=<루트>/graphify-out graphify update Source` 로 한다. 손으로 돌릴 때도 `GRAPHIFY_OUT` 을 빼면 `Source/graphify-out/` 이 생겨 소속 게이트가 멈춘다. 작업자 보고에는 그래프로 찾은 범위를 한 줄 적는다.
- taste-skill: 에디터 UI(ImGui 패널 · 테마 토큰 `Content/editor/theme/*.tokens.json`)를 고칠 때 `Skill` 로 부르고 `references/app-screens.md`(앱 화면 규칙)를 따른다 — 강조색은 데이터 · 선택에만, 켜진 토글은 중립 단계, 본문은 그로테스크 한 벌(로그 줄만 고정폭). 검사 스크립트는 DOM 용이라 확인은 `editor/uicapture.scenario.xml` 스크린샷으로 한다.
- bundle-verify · linux-verify · scenario-debug: 묶음 끝 검증과 push, Linux 빌드 · 체인 · 골든(`Scripts/dev/LinuxScenarios.py`), 시나리오 실패 추적은 `Skill` 로 부르고 그 절차를 따른다. 임시 스크립트를 새로 만들지 말고 여기에 더한다.
- ponytail: 스킬이 있으면 모든 작업에서 `Skill` 로 부른다. 구조 개편을 시킬 때는 "구조는 장기 이득이면 과감히 바꾼다"는 뜻을 요청에 적는다(스킬의 "기존 구조 유지" 지침과 부딪힌다).
- context7(저장소 `.mcp.json` 의 MCP 서버): 외부 라이브러리 · SDK API(Vulkan · D3D12 · Jolt · Box2D · ImGui · Emscripten · WebGPU/Dawn · Android NDK · vcpkg 포트 등)를 쓰거나 판을 올릴 때는 기억에 기대지 말고 context7 로 그 판의 문서를 먼저 찾는다. 요구 판 · 필수 기능 대조(플랫폼 일)도 여기서 근거를 단다.
- claude-mem(사용자 범위 플러그인 — 기록은 플러그인 훅 · 서비스가 남기므로 MCP 만으로는 쓸 수 없다): 이전 세션에서 같은 결함 · 결정을 다뤘는지 볼 때 `mem-search` 로 찾는다. 정본은 여전히 저장소 문서(`docs/`)와 자동 기억 파일이다 — claude-mem 에서 찾은 것은 코드 · 문서와 대조한 뒤에만 쓴다.
- agent-skills: 일에 맞는 스킬을 `Skill` 로 부른다 — 결함 추적 `debugging-and-error-recovery`(scenario-debug 와 함께), 성능 `performance-optimization`(측정 먼저 — 원칙과 같다), 서버 · 온라인 · 입력 검증 `security-and-hardening`(온라인 서버 · HTTP · 계정 로그인 · 영수증 끝점을 고치면 반드시), 이름 · 위치 전수 이전 `deprecation-and-migration`, 설계 판단 `documentation-and-adrs`(결과는 `docs/DesignBacklog.md` 판단 절), 통합 전 읽기 `code-review-and-quality`. 계획 · 질문 · 배포 스킬(`spec` · `plan` · `interview-me` · `ship` · `git-workflow-and-versioning`)이 이 문서와 부딪히면 이 문서를 따른다(질문 대기를 쌓지 않고, push 는 bundle-verify 만).
