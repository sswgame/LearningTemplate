"""도구 사용 정책(PreToolUse · PostToolUse 훅) — 설치한 스킬 · graphify 를 그 일 앞에서 반드시 쓰게 한다.

CLAUDE.md 의 "보조 도구" 는 글로만 있어 세션마다 지켜지지 않았다(2026-11-08 사용자 지적). 에이전트의 행동은 형식으로 막을 수 없어
정책으로 둔다: 규칙 표(`ToolPolicyRules.json` — 규칙은 데이터, 이 파일은 판정만)의 조건을 채우지 않은 호출은 거절하고 먼저 할 일을 알린다.

PostToolUse 는 표지를 남긴다: `skill:<이름>`(Skill 도구), 그리고 규칙 표 `_markers` 의 표지(graphify · CodeGraph · Serena 호출). 표지 `never` 는 남지 않아
그것을 요구하는 규칙은 언제나 거절한다(금지 명령 · 손으로 고치지 않는 파일). 상태는 에이전트마다(하위 에이전트는 agent_id, 아니면 session_id) 임시 폴더의 JSON 에 둔다.

경로 규칙은 `stemTables` 로 린트 표를 읽어 파일 줄기도 볼 수 있다(`newFileOnly` 면 아직 없는 파일만): 금지 낱말 목록을 훅에 다시 적지 않고 린트와 한 표를 쓴다.
표는 이 훅과 같은 저장소의 것이다(규칙 표와 같은 판).
"""
import json
import os
import re
import sys
import tempfile
import time

STATE_DIR = os.path.join(tempfile.gettempdir(), "claude-toolgate")
RULES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ToolPolicyRules.json")
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")   # Scripts/lint/name_collection.py 의 ANY_WORD 와 같은 쪼개기


def split_words(stem: str) -> list[str]:
    return WORD.findall(stem)


def find_stem_hit(path: str, stem_tables: list) -> str | None:
    """파일 줄기가 린트 표의 금지 낱말에 걸리면 그 낱말 — `lastWord` 는 끝 낱말, `phrase` 는 이어 나오는 낱말들. 표 · 칸이 없으면 멈춘다(폴백 없음)."""
    words = split_words(os.path.basename(path).split(".")[0])
    for entry in stem_tables:
        with open(os.path.join(PROJECT_ROOT, entry["table"]), encoding="utf-8") as stream:
            keys = json.load(stream)[entry["key"]]
        for key in keys:
            key_words = split_words(key)
            if entry["match"] == "lastWord" and words[-1:] == key_words:
                return key
            if entry["match"] == "phrase" and any(words[index:index + len(key_words)] == key_words for index in range(len(words))):
                return key
    return None


class ToolPolicy:
    """한 훅 호출의 입력을 읽어 표지를 남기거나, 규칙에 걸리면 거절을 낸다."""

    def __init__(self, payload: dict) -> None:
        self._payload = payload
        self._tool_name = str(payload.get("tool_name", ""))
        self._tool_input = payload.get("tool_input") or {}
        key = str(payload.get("agent_id") or payload.get("session_id") or "unknown")
        self._state_path = os.path.join(STATE_DIR, re.sub(r"[^A-Za-z0-9_.-]", "_", key) + ".json")
        with open(RULES_PATH, encoding="utf-8") as stream:
            table = json.load(stream)
        self._rules = table["_rules"]
        self._markers = table["_markers"]

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

    def shell_marker(self) -> str | None:
        """셸 명령이 표지를 남기는 명령(graphify · codegraph CLI)이면 그 표지입니다."""
        if self._tool_name not in ("Bash", "PowerShell"):
            return None
        return next((entry["marker"] for entry in self._markers if entry.get("shell") and re.search(entry["shell"], self.command())), None)

    def marker_for(self) -> str | None:
        if self._tool_name == "Skill":
            return "skill:" + str(self._tool_input.get("skill", "")).split(":")[-1]
        tool_marker = next((entry["marker"] for entry in self._markers if entry.get("tool") and re.search(entry["tool"], self._tool_name)), None)
        return tool_marker or self.shell_marker()

    def rule_matches(self, rule: dict) -> bool:
        if self._tool_name not in rule.get("tools", []):
            return False
        if self._tool_name in ("Bash", "PowerShell", "Monitor"):
            return bool(rule.get("shell")) and re.search(rule["shell"], self.command()) is not None and self.shell_marker() is None
        if not rule.get("path") or re.search(rule["path"], self.target_path()) is None:
            return False
        if rule.get("exceptPath") and re.search(rule["exceptPath"], self.target_path()) is not None:
            return False
        if rule.get("newFileOnly") and os.path.exists(str(self._tool_input.get("file_path", ""))):
            return False
        return "stemTables" not in rule or find_stem_hit(self.target_path(), rule["stemTables"]) is not None

    def on_post_tool_use(self) -> None:
        marker = self.marker_for()
        if marker is None:
            return
        state = self.load_state()
        state[marker] = time.time()
        self.save_state(state)

    def on_pre_tool_use(self) -> dict | None:
        state = self.load_state()
        now = time.time()
        for rule in self._rules:
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
