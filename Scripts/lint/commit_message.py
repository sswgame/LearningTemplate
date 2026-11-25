#!/usr/bin/env python3
"""
Scripts/lint/commit_message.py

커밋 메시지 양식 검사 — git `commit-msg` 훅이 부른다(설치: Scripts/setup/git_hooks.py).

양식(사용자 결정 2026-11-20 "원인-조치-결과 내용으로 기승전결이 있게"): 첫 줄은 한 줄 요약, 본문에 `원인:` → `조치:` → `결과:`
세 단락이 이 차례로 있고 단락마다 내용이 있다. 기억 · 지시문에 두면 되풀이해 빠지므로 훅이 막는다.
병합(`Merge `) · 되돌리기(`Revert `) · `fixup!` · `squash!` 커밋은 git 이 짓는 꼴이라 뺀다. 꼬리말(`Co-Authored-By:` 등)과 `#` 주석 줄은 보지 않는다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.command import Command, ScriptContext  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402

LABELS = ("원인", "조치", "결과")
EXEMPT_SUBJECT_PREFIXES = ("Merge ", "Revert ", "fixup!", "squash!")
TRAILER = re.compile(r"^[A-Za-z][A-Za-z-]*: ")
LABEL_LINE = re.compile(r"^\s*(?:[-*]\s*)?(" + "|".join(LABELS) + r")\s*:\s*(.*)$")

SAMPLE_MESSAGE = """예:
  렌더 그래프 HZB 전이를 그래프로 옮긴다

  원인: HZB 짓기가 손으로 전이해 그래프가 상태를 몰랐고, 읽는 쪽이 지난 피라미드를 읽을 수 있었다.
  조치: 파이프라인 XML 에 HZB 를 출력으로 적고 FrameBufferTable 에 등록, 손 전이 6 곳을 지웠다.
  결과: 전이는 레벨 콜백이 한 번에 낸다(RT.Graph.transitions). 남은 몫: 뷰 그래프 안 두 짓기는 ③에서."""


def find_problems(message: str) -> list[str]:
    """메시지의 양식 위반 목록을 돌려줍니다. 비면 통과입니다."""
    lines = [line.rstrip("\r") for line in message.split("\n") if not line.startswith("#")]
    while lines and lines[-1].strip() == "":
        lines.pop()
    if not lines or lines[0].strip() == "":
        return ["첫 줄(요약)이 비었다"]
    subject = lines[0].strip()
    if subject.startswith(EXEMPT_SUBJECT_PREFIXES):
        return []
    body = [line for line in lines[1:] if TRAILER.match(line.strip()) is None]

    order: list[str] = []
    content: dict[str, list[str]] = {}
    current: str | None = None
    for line in body:
        matched = LABEL_LINE.match(line)
        if matched:
            current = matched.group(1)
            order.append(current)
            content.setdefault(current, [])
            if matched.group(2).strip():
                content[current].append(matched.group(2).strip())
        elif current is not None and line.strip():
            content[current].append(line.strip())

    problems: list[str] = []
    missing = [label for label in LABELS if label not in content]
    if missing:
        problems.append("본문에 없는 단락: " + " · ".join(f"`{label}:`" for label in missing))
    duplicated = sorted({label for label in order if order.count(label) > 1})
    if duplicated:
        problems.append("두 번 나온 단락: " + " · ".join(f"`{label}:`" for label in duplicated))
    present = [label for label in order if order.count(label) == 1]
    if not missing and not duplicated and present != list(LABELS):
        problems.append("단락 차례가 원인 → 조치 → 결과 가 아니다: " + " → ".join(present))
    empty = [label for label in LABELS if label in content and not content[label]]
    if empty:
        problems.append("내용이 빈 단락: " + " · ".join(f"`{label}:`" for label in empty))
    return problems


class CommitMessageCommand(Command):
    name = "commit-message"
    help = "커밋 메시지 양식(첫 줄 요약 + 원인: → 조치: → 결과:)을 본다 — git commit-msg 훅이 부른다"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("message_file", type=Path, help="커밋 메시지 파일")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        problems = find_problems(args.message_file.read_text(encoding="utf-8", errors="replace"))
        if not problems:
            return ExitCode.PASSED
        print("[commit-msg] 커밋 메시지 양식 위반 — 첫 줄 요약 + 본문 `원인:` → `조치:` → `결과:`(사용자 결정 2026-11-20):", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        print(SAMPLE_MESSAGE, file=sys.stderr)
        return ExitCode.VIOLATION


if __name__ == "__main__":
    sys.exit(CommitMessageCommand.main())
