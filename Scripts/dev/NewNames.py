#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/NewNames.py

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

from common.ExitCode import ExitCode  # noqa: E402
from common.Git import runGitOrRaise  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kDeclaration = re.compile(r"^\+\s*(?:\[\[nodiscard\]\]\s*)?(?:(?:static|virtual|inline|constexpr|explicit|SW_\w+)\s+)*[\w:<>,\s\*&]+?\b([a-z][A-Za-z0-9]*)\s*\([^;{]*\)\s*(?:const\s*)?(?:(?:override|final|noexcept)\s*)*(?:=\s*0\s*)?[;{]")
kCallLike = re.compile(r"\b([a-z][A-Za-z0-9]*)\s*\(")
kKeywords = {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "decltype", "alignof", "catch"}


def runGit(projectRoot: Path, *arguments: str) -> str:
    return runGitOrRaise(arguments, cwd=projectRoot)


def collectNewNames(projectRoot: Path, base: str, ref: str | None) -> dict[str, str]:
    """@p base 에서 @p ref(없으면 작업 트리)까지 헤더에 더한 함수 이름 → 처음 보인 파일. 지운 줄에 있던 이름(옮긴 것)은 뺀다."""
    diffRange = [base] if ref is None else [f"{base}...{ref}"]
    diff = runGit(projectRoot, "diff", *diffRange, "--", "Source/*.h")
    listRemoved: set[str] = set()
    added: dict[str, str] = {}
    path = ""
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:]
        elif line.startswith("---"):
            continue
        elif line.startswith("-"):
            listRemoved.update(kCallLike.findall(line))
        else:
            match = kDeclaration.match(line)
            if match and match.group(1) not in kKeywords:
                added.setdefault(match.group(1), path)
    return {name: where for name, where in added.items() if name not in listRemoved}


def collectExistingNames(projectRoot: Path, base: str) -> set[str]:
    text = runGit(projectRoot, "grep", "-ohE", r"\b[a-z][A-Za-z0-9]*\s*\(", base, "--", "Source")
    return {re.sub(r"^[^:]*:", "", line).split("(")[0].strip() for line in text.splitlines()}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="묶음이 새로 들인 함수 이름을 검토 목록으로 낸다")
    parser.add_argument("--base", required=True, help="묶음 시작 커밋")
    parser.add_argument("refs", nargs="*", help="볼 브랜치(없으면 작업 트리)")
    arguments = parser.parse_args(argv)
    projectRoot = getProjectRoot()
    existing = collectExistingNames(projectRoot, arguments.base)
    listRow: list[tuple[str, str, str]] = []
    for ref in arguments.refs or [None]:
        for name, where in collectNewNames(projectRoot, arguments.base, ref).items():
            if name not in existing:
                listRow.append((ref or "(작업 트리)", where, name))
    for ref, where, name in sorted(set(listRow)):
        print(f"{ref}  {where}  {name}")
    print(f"[NewNames] 새 함수 이름 {len({row[2] for row in listRow})}개 — 업계 이름과 대조해 검토한다")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
