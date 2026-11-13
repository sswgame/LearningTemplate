"""
Stop 훅 — 턴을 끝내는 마지막 답이 한국어가 아니거나 금지한 자체 조어를 쓰면 막고 다시 쓰게 한다.

사용자가 "한글로좀" · "왜 자꾸 영어로 하니" · "본트리가 뭔데" 를 되풀이해 말했다(2026-11-12 ~ 13). 기억 파일은 세션마다 다시 읽히지 않아
잊으므로 훅이 강제한다. 표는 `HookRules.json` 의 `reply`.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

RULES_PATH = Path(__file__).with_name("HookRules.json")
STRIP = re.compile(r"```.*?```|`[^`\n]*`|\[[^\]]*\]\([^)]*\)|https?://\S+|[A-Za-z]:[\\/]\S+|\S+\.(?:h|cpp|py|md|json|cmake|xml|sh)\b", re.S)


def read_last_assistant_text(transcript_path: str) -> str:
    """기록(JSONL)에서 마지막 어시스턴트 메시지의 글 조각을 잇는다. 도구 호출만 있는 메시지는 건너뛴다."""
    lines = Path(transcript_path).read_text(encoding="utf-8", errors="replace").splitlines()
    for line in reversed(lines):
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        if entry.get("type") != "assistant":
            continue
        content = entry.get("message", {}).get("content", [])
        texts = [block.get("text", "") for block in content if isinstance(block, dict) and block.get("type") == "text"]
        if any(text.strip() for text in texts):
            return "\n".join(texts)
    return ""


def find_problems(text: str, rules: dict) -> list[str]:
    problems = [f"'{term}' 는 내가 지어낸 말이다 — '{instead}' 로 쓴다" for term, instead in rules["bannedTerms"].items() if term in text]
    body = STRIP.sub(" ", text)
    hangul = len(re.findall(r"[가-힣]", body))
    latin = len(re.findall(r"[A-Za-z]", body))
    if hangul + latin >= rules["minLetters"] and hangul / (hangul + latin) < rules["minHangulRatio"]:
        problems.append(f"답이 한국어가 아니다(한글 {hangul} · 영문 {latin}) — 사용자에게 하는 모든 답 · 진행 문장은 한국어로 쓴다")
    return problems


def main() -> int:
    payload = json.load(sys.stdin)
    if payload.get("stop_hook_active"):
        return 0  # 이미 한 번 막아 다시 쓰는 중이다 — 같은 턴에 두 번 막지 않는다(끝없는 되풀이 방지)
    rules = json.loads(RULES_PATH.read_text(encoding="utf-8"))["reply"]
    problems = find_problems(read_last_assistant_text(payload.get("transcript_path", "")), rules)
    if problems:
        print(json.dumps({"decision": "block", "reason": "마지막 답을 다시 쓴다: " + " / ".join(problems)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
