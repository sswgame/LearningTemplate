# 보조 도구 설치

새 PC 에서 보조 도구를 설치할 때만 읽습니다. 쓰는 규칙(graphify 로 범위를 먼저 좁힌다 등)은 [CLAUDE.md](../CLAUDE.md) 에 있습니다.

`.claude/skills/` 는 추적하지 않습니다(`.gitignore`). 새 PC 에서는 아래대로 설치합니다.

- **graphify**: 코드 지식 그래프다. `py -3 -m pip install --user graphifyy` 로 설치하고 `graphify install --project` 로 스킬을 등록한다. 사용자 Scripts 폴더가 PATH 에 있어야 한다.
  - 그래프는 `graphify update Source` 로 만든다(약 4분). 산출물 `Source/graphify-out/` 은 저장소 루트의 `graphify-out/` 으로 옮긴다. 추적하지 않는다. `Source` 안에 두면 린트가 그 아래 `.md` 를 읽을 수 있어서 옮긴다.
  - 항상 켠다. 코드 질문은 저장소 루트에서 `graphify query "<질문>"`, `graphify path "<A>" "<B>"`, `graphify explain "<개념>"` 으로 범위를 먼저 좁힌다. 코드를 고친 뒤에는 같은 `update` 명령으로 그래프를 갱신한다.
  - PreToolUse 훅은 공유 `settings.json` 에 넣지 않는다. 이 PC 밖에서는 `graphify` 가 PATH 에 없어 모든 도구 호출이 막힌다. 필요하면 개인 `settings.local.json` 에 절대 경로로 둔다.
  - git 훅(`graphify hook install`)도 넣지 않는다. `update` 는 증분 갱신이 아니라 `Source` 전체를 다시 읽어 3~4분이 걸린다. 설치하면 워크트리 여럿에서 커밋할 때마다 이 시간이 든다. `pre-commit` 훅과는 충돌하지 않는다.
  - 작업자 워크트리에서는 스킬과 안내 훅이 보이지 않으므로 MCP 서버로도 연결한다. 저장소 루트 `.mcp.json` 이 `py -3 -m graphify.serve graphify-out/graph.json` 을 띄우고, `query_graph` 도구로 질의한다. `py -3 -m pip install --user "graphifyy[mcp]"` 가 필요하고, `graphify-out/` 이 있는 폴더(main 체크아웃)에서 쓴다.
- **ponytail**: "가장 작은 변경"을 요구하는 지침 스킬이다. `.claude/skills/ponytail/SKILL.md` 를 두면 코딩 작업마다 자동으로 켜진다(2026-10-15 사용자 결정). `/ponytail lite|full|ultra` 로 강도를 바꾼다.
  - "기존 구조를 유지한다"는 지침이 이 저장소의 "구조는 규모가 커도 장기 이득이면 과감히 바꾼다"는 방침과 부딪힐 수 있다. 구조 개편을 시킬 때는 요청에 그 뜻을 분명히 적는다.
