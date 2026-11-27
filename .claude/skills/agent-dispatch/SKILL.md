---
name: agent-dispatch
description: 하위 작업자(Agent)에게 일을 맡기기 전에 일의 무게에 맞는 작업 종류 · 모델 · effort 를 고른다. 토큰을 아끼려고 쓴다. 훅(agent_prompt_check)이 지시문의 `작업 종류: <키>` 와 호출의 subagent_type · model · effort 가 표에 맞는지 본다.
---

# agent-dispatch

표의 정본은 `.claude/hooks/HookRules.json` 의 `agentPrompt.kinds` 다. 여기에는 고르는 법만 적는다.

## 고르는 차례

1. 직접 하는 편이 싼지 먼저 본다. 대상을 알고 도구 호출 몇 번이면 끝나는 일은 맡기지 않는다.
2. 결과가 무엇인지로 종류를 고른다.
   - 위치나 사실 한 줄이 필요하면 `find`, 웹 문서를 읽어야 하면 `web` 이다.
   - 후보 목록 · 보고서(코드를 고치지 않음)는 `survey` 다.
   - 표나 스크립트로 기계적으로 바꾸는 일(이전 · 개명 · 주석 다듬기 · .md 정리)은 `mechanical` 이다.
   - 이미 정한 명령 차례를 돌리고 결과만 옮기는 일(병합 뒤 쌍 표 `--reapply` · 치환 스크립트 다시 돌리기 · `syntax` · `--ratchet` · 보고 집계)은 `script-run`(haiku)이다. 판단이 필요한 일이나 결과 diff 를 검토하는 일은 여기에 넣지 않는다.
   - 결함 수정 · 기능 · 테스트 고치기는 `implement` 다.
   - 구조 개편, 여러 갈래 통합 · 충돌 풀기, 상용 엔진과 견주는 설계는 `restructure` 다.
   - 통합 전 독립 검토는 `review`, 구현 계획은 `plan` 이다.
3. 표의 범위 안에서 낮은 쪽부터 고른다. 판단이 여러 파일에 걸치거나 틀렸을 때 되돌리는 값이 크면 높은 쪽을 쓴다(예: `implement` 는 한 파일 결함이면 medium, 여러 층이면 high).
4. 지시문 첫 줄에 `작업 종류: <키>` 를 적고, 호출에 `subagent_type` · `model` · `effort` 를 그 줄 값으로 넣는다. 포크(`fork`)는 부모를 이어 받으므로 검사를 받지 않는다.

## 토큰을 아끼는 지시문

- 읽기 전용 종류(`find` · `web` · `survey` · `review` · `plan`)는 코드 고치기 문구(C++17 · 빌드 금지 · 커밋 양식 …)를 넣지 않아도 된다. 훅은 `edits` 가 true 인 종류에만 그 문구를 요구한다.
- 이미 아는 것(파일 · 줄 · 배제한 가설)은 지시문에 적어 다시 찾지 않게 한다.
- 보고는 10 줄 이내로 받고, 긴 결과는 scratchpad `reports/<갈래>.md` 에 쓰게 한다.
- 같은 영역의 후속 일은 새로 띄우지 않고 끝난 작업자에게 SendMessage 로 맡긴다. 이전 맥락을 다시 읽는 비용이 없다.

## 표를 고칠 때

종류를 더하거나 범위를 바꾸면 `HookRules.json` 만 고치고 `py -3 .claude/hooks/test_tool_policy.py` 를 돌린다. 이 스킬에는 키 이름이 바뀔 때만 손댄다.
