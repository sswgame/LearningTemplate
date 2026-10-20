#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListWorktrees.py
@brief 워크트리마다 브랜치와 주인(살아 있는지)을 보입니다 — 무엇을 지워도 되는지 고를 때 씁니다.

    py -3 -m Scripts worktree-list

주인은 `MakeWorktree.py` 가 만들 때 적은 기록(`common/Worktree.py` 의 `kOwnerFileName`)이다. 주인 프로세스가 살아 있으면 "쓰는 중", 끝났으면 "주인 프로세스 끝남"
(지워도 된다), 기록이 없으면 "주인 기록 없음"(이 기능 전에 만든 워크트리 — 누가 쓰는지 모르므로 사용자에게 묻는다)이다.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

from common.Worktree import describeWorktreeOwner, findMainRoot, getWorktreeGitDirectory, readWorktreeOwner  # noqa: E402


@dataclass(frozen=True)
class WorktreeEntry:
    """`git worktree list --porcelain` 의 항목 하나."""

    path: Path
    branch: str


def parseWorktreeList(text: str) -> list[WorktreeEntry]:
    """`git worktree list --porcelain` 출력 → 항목들. 브랜치가 없으면(분리된 HEAD) `(detached)` 다."""
    listEntry: list[WorktreeEntry] = []
    for block in text.strip().split("\n\n"):
        mapField: dict[str, str] = {}
        for line in block.splitlines():
            key, _, value = line.partition(" ")
            mapField[key] = value
        if "worktree" not in mapField:
            continue
        # `git worktree list --porcelain` 은 분리된 HEAD 에 branch 줄 대신 detached 줄을 쓴다.
        branch = mapField["branch"].removeprefix("refs/heads/") if "branch" in mapField else "(detached)"
        listEntry.append(WorktreeEntry(path=Path(mapField["worktree"]), branch=branch))
    return listEntry


def main(argv: list[str] | None = None) -> int:
    argparse.ArgumentParser(description="워크트리마다 브랜치와 주인(살아 있는지)을 보인다 — 인자 없음").parse_args(argv)
    mainRoot = findMainRoot(getProjectRoot())
    listed = runGit(["worktree", "list", "--porcelain"], cwd=mainRoot)
    if not listed.bSucceeded:
        print(f"[ListWorktrees] git worktree list 가 실패했습니다.\n{listed.output.strip()}", file=sys.stderr)
        return 2
    now = time.time()
    for entry in parseWorktreeList(listed.stdout):
        if entry.path.resolve() == mainRoot.resolve():
            print(f"{entry.path}  [{entry.branch}]  main 체크아웃")
            continue
        try:
            ownerText = describeWorktreeOwner(readWorktreeOwner(getWorktreeGitDirectory(entry.path)), now)
        except (RuntimeError, ValueError) as error:
            ownerText = f"주인을 확인하지 못함 — {error}"
        print(f"{entry.path}  [{entry.branch}]  {ownerText}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
