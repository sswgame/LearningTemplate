#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/api_diff.py

묶음이 새로 들인 함수 이름(헤더 선언)을 모아 사람이 검토할 목록으로 낸다 — 통합 때 `code-review-and-quality` 와 함께 본다.

**왜 게이트가 아니라 목록인가.** `naming-words` 는 함수의 앞 단어(동사)만 표와 대조한다. `allowsOneMore` · `finishGraceFind` 처럼 동사는 흔하고
이름 전체가 무엇을 하는지 말하지 않는 것은 낱말 표로 가릴 수 없다(`Peak` · `Extra` 는 업계에서도 쓰는 말이라 낱말을 막으면 맞는 이름까지 막힌다,
2026-11-08). 그래서 기계는 새 이름만 빠짐없이 모으고, 판정(언리얼 · 유니티 · Godot 의 같은 일 이름과 대조)은 통합 담당이 한다. 바꾼 옛 이름은
`Config/Rules/Lint/NamingWords.json` 의 `_bannedNames` 에 올려 다른 브랜치가 되살리지 못하게 한다.

`py -3 -m Scripts new-names --base <묶음 시작 커밋> [브랜치 …]` — 브랜치가 없으면 지금 작업 트리를 본다. 묶음 시작 전에도 있던 이름은 뺀다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git_or_raise  # noqa: E402
from common.paths import get_project_root  # noqa: E402

DECLARATION = re.compile(r"^\+\s*(?:\[\[nodiscard\]\]\s*)?(?:(?:static|virtual|inline|constexpr|explicit|SW_\w+)\s+)*[\w:<>,\s\*&]+?\b([a-z][A-Za-z0-9]*)\s*\([^;{]*\)\s*(?:const\s*)?(?:(?:override|final|noexcept)\s*)*(?:=\s*0\s*)?[;{]")
CALL_LIKE = re.compile(r"\b([a-z][A-Za-z0-9]*)\s*\(")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "decltype", "alignof", "catch"}


def run_git(project_root: Path, *arguments: str) -> str:
    return run_git_or_raise(arguments, cwd=project_root)


def collect_new_names(project_root: Path, base: str, ref: str | None) -> dict[str, str]:
    """@p base 에서 @p ref(없으면 작업 트리)까지 헤더에 더한 함수 이름 → 처음 보인 파일. 지운 줄에 있던 이름(옮긴 것)은 뺀다."""
    diff_range = [base] if ref is None else [f"{base}...{ref}"]
    diff = run_git(project_root, "diff", *diff_range, "--", "Source/*.h")
    list_removed: set[str] = set()
    added: dict[str, str] = {}
    path = ""
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:]
        elif line.startswith("---"):
            continue
        elif line.startswith("-"):
            list_removed.update(CALL_LIKE.findall(line))
        else:
            match = DECLARATION.match(line)
            if match and match.group(1) not in KEYWORDS:
                added.setdefault(match.group(1), path)
    return {name: where for name, where in added.items() if name not in list_removed}


def collect_existing_names(project_root: Path, base: str) -> set[str]:
    text = run_git(project_root, "grep", "-ohE", r"\b[a-z][A-Za-z0-9]*\s*\(", base, "--", "Source")
    return {re.sub(r"^[^:]*:", "", line).split("(")[0].strip() for line in text.splitlines()}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="묶음이 새로 들인 함수 이름을 검토 목록으로 낸다")
    parser.add_argument("--base", required=True, help="묶음 시작 커밋")
    parser.add_argument("refs", nargs="*", help="볼 브랜치(없으면 작업 트리)")
    arguments = parser.parse_args(argv)
    project_root = get_project_root()
    existing = collect_existing_names(project_root, arguments.base)
    list_row: list[tuple[str, str, str]] = []
    for ref in arguments.refs or [None]:
        for name, where in collect_new_names(project_root, arguments.base, ref).items():
            if name not in existing:
                list_row.append((ref or "(작업 트리)", where, name))
    for ref, where, name in sorted(set(list_row)):
        print(f"{ref}  {where}  {name}")
    print(f"[NewNames] 새 함수 이름 {len({row[2] for row in list_row})}개 — 업계 이름과 대조해 검토한다")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
