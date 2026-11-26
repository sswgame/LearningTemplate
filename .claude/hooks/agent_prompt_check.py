"""
PreToolUse(Agent) 훅. 작업자에게 일을 맡기는 호출을 표(`HookRules.json` 의 `agentPrompt`)로 검사하고, 어긋나면 거절한다.

- 지시문의 `작업 종류: <키>` 가 `kinds` 의 한 줄을 고른다. 호출의 subagent_type · model · effort 가 그 줄 밖이면 거절한다.
  일의 무게에 맞는 모델 · effort 를 고르게 해 토큰을 아낀다(사용자 요청 2026-11-26).
- 코드를 고치는 종류(edits)는 필수 문구도 본다. C++20 을 써 통합 빌드가 수백 곳 깨졌고(2026-11-13), 빌드 금지 · 새 린트 금지를 빠뜨린 지시도 되풀이됐다.
- 이 기계에서 실패하는 isolation 값(`refusedIsolation`, worktree)은 종류와 상관없이 거절하고 대신할 길을 알린다.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

RULES_PATH = Path(__file__).with_name("HookRules.json")
KIND_PATTERN = re.compile(r"작업 종류:\s*([a-z-]+)")


def find_missing(prompt: str, rules: dict) -> list[str]:
    return [item["name"] for item in rules["required"] if not any(re.search(pattern, prompt) for pattern in item["anyOf"])]


def check(tool_input: dict, rules: dict) -> str | None:
    """거절 사유를 돌려준다. 통과면 None."""
    refused = rules["refusedIsolation"].get(tool_input.get("isolation", ""))
    if refused:
        return refused
    subagent_type = tool_input.get("subagent_type") or "general-purpose"
    if subagent_type == "fork":
        return None  # 포크는 부모의 모델 · effort · 지시를 그대로 잇는다
    kinds = rules["kinds"]
    prompt = tool_input.get("prompt", "")
    match = KIND_PATTERN.search(prompt)
    if match is None or match.group(1) not in kinds:
        return "지시문에 `작업 종류: <키>` 가 없거나 표에 없는 키다. 키: " + " · ".join(kinds) + " (스킬 agent-dispatch 로 고른다)."
    name = match.group(1)
    kind = kinds[name]
    wrong = []
    if subagent_type not in kind["subagentTypes"]:
        wrong.append(f"subagent_type {subagent_type} → {' | '.join(kind['subagentTypes'])}")
    if tool_input.get("model") not in kind["models"]:
        wrong.append(f"model {tool_input.get('model')} → {' | '.join(kind['models'])}")
    if tool_input.get("effort") not in kind["efforts"]:
        wrong.append(f"effort {tool_input.get('effort')} → {' | '.join(kind['efforts'])}")
    if wrong:
        return f"작업 종류 {name} 에 맞지 않는 호출: " + ", ".join(wrong) + " (.claude/hooks/HookRules.json agentPrompt.kinds)."
    if kind["edits"]:
        missing = find_missing(prompt, rules)
        if missing:
            return "작업자 지시문에 빠진 것: " + " · ".join(missing) + " — 지시문에 적고 다시 맡긴다(.claude/hooks/HookRules.json agentPrompt)."
    return None


def main() -> int:
    payload = json.load(sys.stdin)
    rules = json.loads(RULES_PATH.read_text(encoding="utf-8"))["agentPrompt"]
    reason = check(payload.get("tool_input", {}), rules)
    if reason:
        print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "deny", "permissionDecisionReason": reason}}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
