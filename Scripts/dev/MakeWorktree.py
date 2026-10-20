#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MakeWorktree.py
@brief 작업 단위 하나를 위한 git 워크트리를 만들고, 내려받은 도구 · vcpkg 설치 폴더를 main 체크아웃과 나눠 쓰게 연결합니다.

    py -3 -m Scripts worktree-make <이름>                       # <드라이브>:/w/<이름>(Windows) · <main 의 부모>/LT-wt/<이름>, 브랜치 wt/<이름>, 기준 origin/main
    py -3 -m Scripts worktree-make <이름> --base wt/apply5      # 다른 브랜치에서 갈라 만든다
    py -3 -m Scripts worktree-make <이름> --dry-run             # 무엇을 할지만 보인다
    py -3 -m Scripts worktree-make <이름> --pid $PID            # 사람 셸 — 주인 프로세스를 직접 준다(에이전트는 CLAUDE_PID 를 쓴다)

만들 때 주인(이름 · 프로세스 번호 · 세션 · 시작 시각)을 워크트리의 git 디렉터리에 적습니다(`common/Worktree.py` 의 `kOwnerFileName`). `worktree-list` 와
`worktree-remove` 가 그 프로세스가 살아 있는지 보고 "다른 세션이 쓰는 중" 을 알린다 — 주인을 모르면 남의 워크트리를 지울 수 있어서,
주인 프로세스를 정할 수 없으면 만들지 않는다.

워크트리마다 LLVM · Ninja · vcpkg 를 새로 받고 vcpkg 포트를 다시 빌드하면 수십 분이 걸립니다. 그래서 `common/Worktree.py` 의 `kSharedFolders` 폴더는
main 체크아웃의 것을 가리키는 링크(Windows 는 정션, 그 밖은 심볼릭 링크)로 둡니다. 이 링크는 지우거나 고치지 않습니다 — 지우는 것은
`RemoveWorktree.py` 가 링크를 먼저 끊은 뒤에 합니다. 작업 방식은 `docs/11_Workflow.md` 에 있습니다.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Worktree import (WorktreeOwner, createDirectoryLink, findMainRoot, getWorktreeGitDirectory, kOwnerPidEnvironment,  # noqa: E402
                             kOwnerSessionEnvironment, kWorktreeFolderName, listDifferentTriplets, listSharedLinks, makePlan,
                             resolveOwnerProcessId, writeWorktreeOwner)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="작업 단위용 git 워크트리를 만들고 도구 · vcpkg 폴더를 main 체크아웃과 나눠 쓰게 연결한다.")
    parser.add_argument("name", help="워크트리 이름 — 폴더 <워크트리 루트>/<이름>, 브랜치 wt/<이름>")
    parser.add_argument("--base", default="origin/main", help="갈라 나올 기준(기본 origin/main — origin/ 이면 먼저 fetch 한다)")
    parser.add_argument("--worktree-root", type=Path, default=None, help=f"워크트리를 둘 폴더(기본: Windows 는 드라이브 루트의 w, 그 밖은 main 의 부모/{kWorktreeFolderName})")
    parser.add_argument("--dry-run", action="store_true", help="만들지 않고 할 일만 보인다")
    parser.add_argument("--owner", default=None, help="주인 이름(기본: 워크트리 이름)")
    parser.add_argument("--pid", type=int, default=None,
                        help=f"주인 프로세스 번호 — 세션처럼 오래 사는 프로세스(기본: 환경 변수 {kOwnerPidEnvironment}). 사람 셸이면 PowerShell 의 $PID")
    args = parser.parse_args(argv)

    try:
        mainRoot = findMainRoot(getProjectRoot())
        plan = makePlan(mainRoot, args.name, args.base, args.worktree_root)
        ownerPid = resolveOwnerProcessId(args.pid)
        if ownerPid is None:
            raise ValueError(f"주인 프로세스를 정할 수 없습니다 — --pid 를 줍니다(환경 변수 {kOwnerPidEnvironment} 가 없다). "
                             "주인 기록이 없으면 다른 세션이 이 워크트리를 쓰는 중인지 알 수 없다")
    except (RuntimeError, ValueError) as error:
        print(f"[MakeWorktree] {error}", file=sys.stderr)
        return 2
    owner = WorktreeOwner(owner=args.owner or args.name, pid=ownerPid, session=os.environ.get(kOwnerSessionEnvironment, ""),
                          startTime=time.time())

    listLink = listSharedLinks(plan)
    print(f"[MakeWorktree] {plan.worktreePath} (브랜치 {plan.branch}, 기준 {plan.base})")
    for linkPath, targetPath in listLink:
        print(f"  링크 {linkPath.relative_to(plan.worktreePath).as_posix()} -> {targetPath}")
    if args.dry_run:
        return 0
    if plan.worktreePath.exists():
        print(f"[MakeWorktree] 이미 있습니다: {plan.worktreePath}", file=sys.stderr)
        return 1

    if plan.base.startswith("origin/"):
        fetched = runGit(["fetch", "origin"], cwd=mainRoot)
        if not fetched.bSucceeded:
            print(f"[MakeWorktree] git fetch 가 실패했습니다 — 마지막으로 받은 {plan.base} 로 만듭니다.\n{fetched.stderr.strip()}", file=sys.stderr)
    added = runGit(["worktree", "add", str(plan.worktreePath), "-b", plan.branch, plan.base], cwd=mainRoot)
    if not added.bSucceeded:
        print(f"[MakeWorktree] git worktree add 가 실패했습니다.\n{added.output.strip()}", file=sys.stderr)
        return 1
    writeWorktreeOwner(getWorktreeGitDirectory(plan.worktreePath), owner)
    print(f"  주인 {owner.owner} (pid {owner.pid}, 세션 {owner.session or '없음'})")

    bAllLinked = all([createDirectoryLink(linkPath, targetPath) for linkPath, targetPath in listLink if not linkPath.exists()])
    listDifferent = listDifferentTriplets(plan)
    if listDifferent:
        print("[MakeWorktree] 경고: vcpkg 트리플릿 파일의 바이트가 main 과 다릅니다(줄끝) — 두 트리가 서로의 vcpkg 스탬프를 어긋남으로 봅니다.\n"
              "  그 파일을 지우고 `git checkout -- <파일>` 로 다시 받으세요: " + ", ".join(listDifferent), file=sys.stderr)
    print(f"[MakeWorktree] 완료: {plan.worktreePath}")
    return 0 if bAllLinked else 1


if __name__ == "__main__":
    sys.exit(main())
