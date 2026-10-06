#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RemoveWorktree.py
@brief `MakeWorktree.py` 로 만든 워크트리를 지웁니다 — main 과 나눠 쓰는 도구 · vcpkg 링크를 **먼저 끊고** 지웁니다.

    py -3 -m Scripts worktree-remove <이름> [<이름> …]            # 워크트리와 브랜치 wt/<이름> 을 지운다
    py -3 -m Scripts worktree-remove <이름> --keep-branch         # 브랜치는 남긴다
    py -3 -m Scripts worktree-remove <이름> --force               # 커밋하지 않은 변경이 있어도 지운다

링크를 끊지 않고 폴더를 지우면 도구에 따라 링크 너머(main 의 LLVM · vcpkg 설치 트리)까지 지울 수 있습니다. 그래서 지우기 전에
`MakeWorktree.kSharedFolders` 의 링크를 링크로서만 지우고, 그 뒤에 `git worktree remove` 를 부릅니다.
"""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, runGit  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from MakeWorktree import findMainRoot, kSharedFolders, makePlan  # noqa: E402


def isDirectoryLink(path: Path) -> bool:
    """`path` 가 심볼릭 링크이거나 Windows 정션인가(링크 너머는 보지 않는다)."""
    if path.is_symlink():
        return True
    try:
        attributes = os.lstat(path)
    except OSError:
        return False
    reparseFlag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(reparseFlag) and bool(getattr(attributes, "st_file_attributes", 0) & reparseFlag)


def removeDirectoryLink(path: Path) -> None:
    """링크만 지웁니다. 정션 · 디렉터리 심볼릭 링크는 Windows 에서 rmdir, 그 밖은 unlink 로 지운다(대상은 그대로)."""
    if os.name == "nt":
        os.rmdir(path)
    else:
        os.unlink(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="MakeWorktree 로 만든 워크트리를 나눠 쓰는 링크를 먼저 끊고 지운다.")
    parser.add_argument("names", nargs="+", help="지울 워크트리 이름(브랜치 wt/<이름>)")
    parser.add_argument("--worktree-root", type=Path, default=None, help="워크트리를 둔 폴더(기본: main 체크아웃의 부모/LT-wt)")
    parser.add_argument("--keep-branch", action="store_true", help="브랜치 wt/<이름> 을 지우지 않는다")
    parser.add_argument("--force", action="store_true", help="커밋하지 않은 변경이 있어도 지운다")
    args = parser.parse_args(argv)

    mainRoot = findMainRoot(getProjectRoot())
    failureCount = 0
    for name in args.names:
        plan = makePlan(mainRoot, name, "", args.worktree_root)
        if plan.worktreePath.resolve() == mainRoot.resolve():
            print(f"[RemoveWorktree] main 체크아웃은 지우지 않습니다: {mainRoot}", file=sys.stderr)
            failureCount += 1
            continue
        if plan.worktreePath.exists():
            status = runGit(["status", "--porcelain"], cwd=plan.worktreePath)
            if status.bSucceeded and status.stdout.strip() and not args.force:
                print(f"[RemoveWorktree] 커밋하지 않은 변경이 있어 건너뜁니다(--force 로 지운다): {plan.worktreePath}", file=sys.stderr)
                failureCount += 1
                continue
            for rel in kSharedFolders:
                linkPath = plan.worktreePath / rel
                if isDirectoryLink(linkPath):
                    removeDirectoryLink(linkPath)
            removed = runGit(["worktree", "remove", "--force", str(plan.worktreePath)], cwd=mainRoot)
            if not removed.bSucceeded and plan.worktreePath.exists():
                shutil.rmtree(plan.worktreePath, ignore_errors=True)
        if not args.keep_branch:
            runGit(["branch", "-D", plan.branch], cwd=mainRoot)
        print(f"[RemoveWorktree] 지움: {name}")
    runGit(["worktree", "prune"], cwd=mainRoot)
    return 1 if failureCount else 0


if __name__ == "__main__":
    sys.exit(main())
