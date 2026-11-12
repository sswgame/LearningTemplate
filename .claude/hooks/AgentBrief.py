"""
PreToolUse(Agent) 훅 — 코드를 고치는 작업자에게 일을 맡길 때 지시문에 꼭 있어야 할 것이 빠지면 맡기기를 거절한다.

작업자가 C++20(concept · consteval)을 써 통합 빌드가 수백 곳 깨졌고(2026-11-13), 빌드 금지 · 새 린트 금지를 빠뜨린 지시도 되풀이됐다.
기억에 기대지 않고 지시문을 검사한다. 표는 `HookRules.json` 의 `agentBrief`.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

kRulesPath = Path(__file__).with_name("HookRules.json")


def findMissing(prompt: str, rules: dict) -> list[str]:
    return [item["name"] for item in rules["required"] if not any(re.search(pattern, prompt) for pattern in item["anyOf"])]


def main() -> int:
    payload = json.load(sys.stdin)
    toolInput = payload.get("tool_input", {})
    rules = json.loads(kRulesPath.read_text(encoding="utf-8"))["agentBrief"]
    if toolInput.get("subagent_type", "") not in rules["subagentTypes"]:
        return 0  # 읽기 · 찾기 전용 작업자(Explore · web-fetch …)는 코드를 고치지 않는다
    missing = findMissing(toolInput.get("prompt", ""), rules)
    if missing:
        reason = "작업자 지시문에 빠진 것: " + " · ".join(missing) + " — 지시문에 적고 다시 맡긴다(.claude/hooks/HookRules.json agentBrief)."
        print(json.dumps({"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "deny", "permissionDecisionReason": reason}}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
