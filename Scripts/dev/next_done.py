#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/next_done.py

커밋이 끝냈다고 적은 할 일 줄이 `docs/NEXT.md` 에 그대로 남아 있는지 본다.

작업자는 NEXT 를 고치지 않고 통합이 지운다. 병합이 많으면 지우는 일만 빠져 끝난 줄이 며칠치 쌓였고, 다음 작업자가 이미 끝난 줄을 다시 맡았다.
끝낸 커밋이 꼬리말을 적는다(줄마다 하나, commit-msg 훅은 꼬리말을 보지 않는다):

  NEXT: <줄 앞말> 끝

앞말은 목록 머리 · `[코드]` · 굵은 글씨를 뺀 줄의 처음 낱말들이다(`NEXT: 렌더러 규격 끝`). 기준(`origin/main`)의 줄과 HEAD 의 줄이 **글자까지 같을 때만** 남은 것이다 —
통합이 지웠거나 남은 몫으로 고쳐 쓴 줄은 지나간다. 앞말이 기준 문서의 두 줄 이상에 맞거나 어디에도 맞지 않으면 알리기만 한다(이미 적힌 커밋은 고칠 수 없다).
묶음 검증이 시작할 때 본다 — 끝에서 보면 줄을 지운 커밋이 검증한 sha 가 아니게 된다.

  py -3 -m Scripts next-done [--base origin/main]     # 남은 줄이 있으면 1
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git, run_git_or_raise  # noqa: E402
from common.paths import get_project_root  # noqa: E402

NEXT_PATH = "docs/NEXT.md"
DONE_LINE = re.compile(r"^NEXT:\s*(\S.*?)\s*끝\.?\s*$", re.M)
LIST_HEAD = re.compile(r"^\s*(?:[-*]|\d+\.)\s+")


def normalize(text: str) -> str:
    """견줄 글 — 목록 머리 · `[코드]` 표 · 굵은 글씨 · 코드 표시를 뺀다."""
    return LIST_HEAD.sub("", text).replace("`[코드]`", "").replace("[코드]", "").replace("**", "").replace("`", "").strip()


def list_items(next_text: str) -> list[str]:
    """할 일 줄(목록 줄)만."""
    return [line.rstrip() for line in next_text.splitlines() if LIST_HEAD.match(line)]


def find_remaining(messages: Iterable[str], base_text: str, head_text: str) -> tuple[list[str], list[str]]:
    """(남은 줄, 알림). `base_text` · `head_text` 는 기준과 HEAD 의 NEXT 글이다."""
    list_base = list_items(base_text)
    set_head = set(list_items(head_text))
    list_remaining: list[str] = []
    list_warning: list[str] = []
    for words in dict.fromkeys(match for message in messages for match in DONE_LINE.findall(message)):
        list_match = [line for line in list_base if normalize(line).startswith(normalize(words))]
        if len(list_match) != 1:
            list_warning.append(f"'NEXT: {words} 끝' 이 기준 문서의 줄 {len(list_match)} 개에 맞는다 — 손으로 본다")
        elif list_match[0] in set_head:
            list_remaining.append(list_match[0])
    return list_remaining, list_warning


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts next-done", description="커밋이 끝냈다고 적은 NEXT 줄이 남아 있는지 본다")
    parser.add_argument("--base", default="origin/main", help="기준 커밋(검증해 push 한 것)")
    arguments = parser.parse_args(argv)
    root = get_project_root()
    messages = run_git_or_raise(["log", "--format=%B%x00", f"{arguments.base}..HEAD"], cwd=root).split("\x00")
    base = run_git(["show", f"{arguments.base}:{NEXT_PATH}"], cwd=root)
    head = run_git(["show", f"HEAD:{NEXT_PATH}"], cwd=root)
    list_remaining, list_warning = find_remaining(messages, base.stdout, head.stdout)
    for warning in list_warning:
        print(f"[next-done] 알림: {warning}")
    for line in list_remaining:
        print(f"[next-done] 끝났다고 적힌 줄이 {NEXT_PATH} 에 그대로다 — 지우거나 남은 몫으로 고친다: {line}")
    return ExitCode.VIOLATION if list_remaining else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
