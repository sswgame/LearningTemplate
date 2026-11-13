"""도구 사용 정책(PreToolUse · PostToolUse 훅) — 설치한 스킬 · graphify 를 그 일 앞에서 반드시 쓰게 한다.

CLAUDE.md 의 "보조 도구" 는 글로만 있어 세션마다 지켜지지 않았다(2026-11-08 사용자 지적). 에이전트의 행동은 형식으로 막을 수 없어
정책으로 둔다: 규칙 표(`ToolPolicyRules.json` — 규칙은 데이터, 이 파일은 판정만)의 조건을 채우지 않은 호출은 거절하고 먼저 할 일을 알린다.

PostToolUse 는 표지를 남긴다: `skill:<이름>`(Skill 도구), `graph`(graphify query · path · explain, mcp__graphify__ 도구),
`context7`(이름에 context7 가 든 도구). 상태는 에이전트마다(하위 에이전트는 agent_id, 아니면 session_id) 임시 폴더의 JSON 에 둔다.
"""
import json
import os
import re
import sys
import tempfile
import time

STATE_DIR = os.path.join(tempfile.gettempdir(), "claude-toolgate")
RULES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ToolPolicyRules.json")
GRAPH_COMMAND = re.compile(r"\bgraphify\s+(query|path|explain)\b")


class ToolPolicy:
    """한 훅 호출의 입력을 읽어 표지를 남기거나, 규칙에 걸리면 거절을 낸다."""

    def __init__(self, payload: dict) -> None:
        self._payload = payload
        self._tool_name = str(payload.get("tool_name", ""))
        self._tool_input = payload.get("tool_input") or {}
        key = str(payload.get("agent_id") or payload.get("session_id") or "unknown")
        self._state_path = os.path.join(STATE_DIR, re.sub(r"[^A-Za-z0-9_.-]", "_", key) + ".json")

    def load_state(self) -> dict:
        try:
            with open(self._state_path, encoding="utf-8") as stream:
                return json.load(stream)
        except (OSError, ValueError):
            return {}

    def save_state(self, state: dict) -> None:
        os.makedirs(STATE_DIR, exist_ok=True)
        with open(self._state_path, "w", encoding="utf-8") as stream:
            json.dump(state, stream)

    def command(self) -> str:
        return str(self._tool_input.get("command", ""))

    def target_path(self) -> str:
        """파일 · 폴더 인자(Glob 은 패턴도 붙인다)를 `/` 로 맞춘다."""
        parts = [str(self._tool_input.get(name, "")) for name in ("file_path", "notebook_path", "path", "pattern") if self._tool_input.get(name)]
        return "/" + "/".join(parts).replace("\\", "/").lstrip("/")

    def marker_for(self) -> str | None:
        if self._tool_name.startswith("mcp__graphify__") or (self._tool_name in ("Bash", "PowerShell") and GRAPH_COMMAND.search(self.command())):
            return "graph"
        if "context7" in self._tool_name.lower():
            return "context7"
        if self._tool_name == "Skill":
            return "skill:" + str(self._tool_input.get("skill", "")).split(":")[-1]
        return None

    def rule_matches(self, rule: dict) -> bool:
        if self._tool_name not in rule.get("tools", []):
            return False
        if self._tool_name in ("Bash", "PowerShell"):
            command = self.command()
            return bool(rule.get("shell")) and re.search(rule["shell"], command) is not None and not GRAPH_COMMAND.search(command)
        return bool(rule.get("path")) and re.search(rule["path"], self.target_path()) is not None

    def on_post_tool_use(self) -> None:
        marker = self.marker_for()
        if marker is None:
            return
        state = self.load_state()
        state[marker] = time.time()
        self.save_state(state)

    def on_pre_tool_use(self) -> dict | None:
        with open(RULES_PATH, encoding="utf-8") as stream:
            rules = json.load(stream)["_rules"]
        state = self.load_state()
        now = time.time()
        for rule in rules:
            if not self.rule_matches(rule):
                continue
            window = rule.get("windowMinutes")
            satisfied = any(marker in state and (window is None or now - state[marker] <= window * 60) for marker in rule["requiresAny"])
            if not satisfied:
                reason = f"[도구 정책 {rule['name']}] {rule['hint']} 그 뒤에 이 호출을 다시 한다(CLAUDE.md 보조 도구, 규칙 .claude/hooks/ToolPolicyRules.json)."
                return {"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "deny", "permissionDecisionReason": reason}}
        return None


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except ValueError:
        return 0
    gate = ToolPolicy(payload)
    event = payload.get("hook_event_name")
    if event == "PostToolUse":
        gate.on_post_tool_use()
    elif event == "PreToolUse":
        decision = gate.on_pre_tool_use()
        if decision is not None:
            sys.stdout.buffer.write(json.dumps(decision, ensure_ascii=False).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
