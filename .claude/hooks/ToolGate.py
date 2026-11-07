"""도구 사용 게이트(PreToolUse · PostToolUse 훅) — 설치한 스킬 · graphify 를 그 일 앞에서 반드시 쓰게 한다.

CLAUDE.md 의 "보조 도구" 는 글로만 있어 세션마다 지켜지지 않았다(2026-11-08 사용자 지적). 에이전트의 행동은 형식으로 막을 수 없어
게이트로 둔다: 규칙 표(`ToolGateRules.json` — 규칙은 데이터, 이 파일은 판정만)의 조건을 채우지 않은 호출은 거절하고 먼저 할 일을 알린다.

PostToolUse 는 표지를 남긴다: `skill:<이름>`(Skill 도구), `graph`(graphify query · path · explain, mcp__graphify__ 도구),
`context7`(이름에 context7 가 든 도구). 상태는 에이전트마다(하위 에이전트는 agent_id, 아니면 session_id) 임시 폴더의 JSON 에 둔다.
"""
import json
import os
import re
import sys
import tempfile
import time

kStateDir = os.path.join(tempfile.gettempdir(), "claude-toolgate")
kRulesPath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ToolGateRules.json")
kGraphCommand = re.compile(r"\bgraphify\s+(query|path|explain)\b")


class ToolGate:
    """한 훅 호출의 입력을 읽어 표지를 남기거나, 규칙에 걸리면 거절을 낸다."""

    def __init__(self, payload: dict) -> None:
        self._payload = payload
        self._toolName = str(payload.get("tool_name", ""))
        self._toolInput = payload.get("tool_input") or {}
        key = str(payload.get("agent_id") or payload.get("session_id") or "unknown")
        self._statePath = os.path.join(kStateDir, re.sub(r"[^A-Za-z0-9_.-]", "_", key) + ".json")

    def loadState(self) -> dict:
        try:
            with open(self._statePath, encoding="utf-8") as stream:
                return json.load(stream)
        except (OSError, ValueError):
            return {}

    def saveState(self, state: dict) -> None:
        os.makedirs(kStateDir, exist_ok=True)
        with open(self._statePath, "w", encoding="utf-8") as stream:
            json.dump(state, stream)

    def command(self) -> str:
        return str(self._toolInput.get("command", ""))

    def targetPath(self) -> str:
        """파일 · 폴더 인자(Glob 은 패턴도 붙인다)를 `/` 로 맞춘다."""
        parts = [str(self._toolInput.get(name, "")) for name in ("file_path", "notebook_path", "path", "pattern") if self._toolInput.get(name)]
        return "/" + "/".join(parts).replace("\\", "/").lstrip("/")

    def markerFor(self) -> str | None:
        if self._toolName.startswith("mcp__graphify__") or (self._toolName in ("Bash", "PowerShell") and kGraphCommand.search(self.command())):
            return "graph"
        if "context7" in self._toolName.lower():
            return "context7"
        if self._toolName == "Skill":
            return "skill:" + str(self._toolInput.get("skill", "")).split(":")[-1]
        return None

    def ruleMatches(self, rule: dict) -> bool:
        if self._toolName not in rule.get("tools", []):
            return False
        if self._toolName in ("Bash", "PowerShell"):
            command = self.command()
            return bool(rule.get("shell")) and re.search(rule["shell"], command) is not None and not kGraphCommand.search(command)
        return bool(rule.get("path")) and re.search(rule["path"], self.targetPath()) is not None

    def onPostToolUse(self) -> None:
        marker = self.markerFor()
        if marker is None:
            return
        state = self.loadState()
        state[marker] = time.time()
        self.saveState(state)

    def onPreToolUse(self) -> dict | None:
        with open(kRulesPath, encoding="utf-8") as stream:
            rules = json.load(stream)["_rules"]
        state = self.loadState()
        now = time.time()
        for rule in rules:
            if not self.ruleMatches(rule):
                continue
            window = rule.get("windowMinutes")
            satisfied = any(marker in state and (window is None or now - state[marker] <= window * 60) for marker in rule["requiresAny"])
            if not satisfied:
                reason = f"[도구 게이트 {rule['name']}] {rule['hint']} 그 뒤에 이 호출을 다시 한다(CLAUDE.md 보조 도구, 규칙 .claude/hooks/ToolGateRules.json)."
                return {"hookSpecificOutput": {"hookEventName": "PreToolUse", "permissionDecision": "deny", "permissionDecisionReason": reason}}
        return None


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except ValueError:
        return 0
    gate = ToolGate(payload)
    event = payload.get("hook_event_name")
    if event == "PostToolUse":
        gate.onPostToolUse()
    elif event == "PreToolUse":
        decision = gate.onPreToolUse()
        if decision is not None:
            sys.stdout.buffer.write(json.dumps(decision, ensure_ascii=False).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
