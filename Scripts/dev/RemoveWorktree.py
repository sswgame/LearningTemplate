#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RemoveWorktree.py
@brief `MakeWorktree.py` 로 만든 워크트리를 지웁니다 — main 과 나눠 쓰는 도구 · vcpkg 링크를 **먼저 끊고** 지웁니다.

    py -3 -m Scripts worktree-remove <이름> [<이름> …]            # 워크트리와 브랜치 wt/<이름> 을 지운다
    py -3 -m Scripts worktree-remove <이름> --keep-branch         # 브랜치는 남긴다
    py -3 -m Scripts worktree-remove <이름> --force               # 커밋하지 않은 변경이 있어도 지운다
    py -3 -m Scripts worktree-remove <이름> --even-if-owned       # 다른 세션이 쓰는 중이어도 지운다(사용자가 시킨 때만)

주인 기록(`common/Worktree.py` 의 `kOwnerFileName`)의 프로세스가 살아 있고 내 프로세스(`--pid` · `CLAUDE_PID`)가 아니면 "다른 세션이 쓰는 중" 으로 건너뛴다.
`--force` 는 커밋하지 않은 변경만 넘는다 — 둘을 한 플래그로 두면 변경을 버리려고 준 `--force` 가 남의 워크트리까지 지운다.

링크를 끊지 않고 폴더를 지우면 도구에 따라 링크 너머(main 의 LLVM · vcpkg 설치 트리)까지 지울 수 있습니다. 그래서 지우기 전에
`common/Worktree.py` 의 `kSharedFolders` 링크를 링크로서만 지우고, 그 뒤에 `git worktree remove` 를 부릅니다.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import shutil
import stat
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

from generate.CookAssets import kCookStampPathPattern  # noqa: E402 — 쿠킹 스탬프 경로의 정본
from common.Worktree import (  # noqa: E402
    describeWorktreeOwner,
    findMainRoot,
    getWorktreeGitDirectory,
    isOwnedByOtherLiveProcess,
    kOwnerPidEnvironment,
    kSharedFolders,
    makePlan,
    readWorktreeOwner,
    resolveOwnerProcessId,
)


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


def listBlockingChanges(statusPorcelain: str) -> list[str]:
    """`git status --porcelain` 에서 지우기를 막는 변경의 경로만 돌려줍니다 — 쿠킹 스탬프(생성물)는 뺀다.

    스탬프는 추적되는 파일이라 빌드만 해도 바뀌고, 그 변경은 버려도 되는 것이다. 이름 바꾸기(`a -> b`)는 도착 경로로 본다.
    """
    paths: list[str] = []
    for line in statusPorcelain.splitlines():
        path = line[3:].split(" -> ")[-1].strip().strip('"').replace("\\", "/")
        if path and not fnmatch.fnmatch(path, kCookStampPathPattern):
            paths.append(path)
    return paths


def removeLeftoverDirectory(path: Path, retryCount: int = 5, retrySeconds: float = 0.5) -> bool:
    """폴더 껍데기가 남았으면 짧게 다시 지워 봅니다. 끝내 남으면 False — 호출한 쪽이 경로를 경고로 보인다."""
    for attempt in range(retryCount):
        if not path.exists():
            return True
        shutil.rmtree(path, ignore_errors=True)
        if not path.exists():
            return True
        if attempt + 1 < retryCount:
            time.sleep(retrySeconds)
    return not path.exists()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="MakeWorktree 로 만든 워크트리를 나눠 쓰는 링크를 먼저 끊고 지운다.")
    parser.add_argument("names", nargs="+", help="지울 워크트리 이름(브랜치 wt/<이름>)")
    parser.add_argument("--worktree-root", type=Path, default=None, help="워크트리를 둔 폴더(기본: MakeWorktree 와 같은 위치 — Windows 는 드라이브 루트의 w)")
    parser.add_argument("--keep-branch", action="store_true", help="브랜치 wt/<이름> 을 지우지 않는다")
    parser.add_argument("--force", action="store_true", help="커밋하지 않은 변경이 있어도 지운다")
    parser.add_argument("--even-if-owned", dest="evenIfOwned", action="store_true",
                        help="다른 세션이 쓰는 중(주인 프로세스가 살아 있음)이어도 지운다 — 사용자가 시킨 때만")
    parser.add_argument("--pid", type=int, default=None, help=f"내 프로세스 번호(기본: 환경 변수 {kOwnerPidEnvironment}) — 내가 주인인 워크트리는 그냥 지운다")
    args = parser.parse_args(argv)

    mainRoot = findMainRoot(getProjectRoot())
    currentPid = resolveOwnerProcessId(args.pid)
    now = time.time()
    failureCount = 0
    for name in args.names:
        plan = makePlan(mainRoot, name, "", args.worktree_root)
        if plan.worktreePath.resolve() == mainRoot.resolve():
            print(f"[RemoveWorktree] main 체크아웃은 지우지 않습니다: {mainRoot}", file=sys.stderr)
            failureCount += 1
            continue
        if plan.worktreePath.exists():
            try:
                owner = readWorktreeOwner(getWorktreeGitDirectory(plan.worktreePath))
            except (RuntimeError, ValueError) as error:
                print(f"[RemoveWorktree] 주인을 확인하지 못해 건너뜁니다(--even-if-owned 로 지운다): {error}", file=sys.stderr)
                if not args.evenIfOwned:
                    failureCount += 1
                    continue
                owner = None
            if isOwnedByOtherLiveProcess(owner, currentPid) and not args.evenIfOwned:
                print(f"[RemoveWorktree] 다른 세션이 쓰는 중이라 건너뜁니다(--even-if-owned 로 지운다): {plan.worktreePath} — "
                      f"{describeWorktreeOwner(owner, now)}", file=sys.stderr)
                failureCount += 1
                continue
            status = runGit(["status", "--porcelain"], cwd=plan.worktreePath)
            blocking = listBlockingChanges(status.stdout) if status.bSucceeded else []
            if blocking and not args.force:
                shown = ", ".join(blocking[:5]) + (f" 외 {len(blocking) - 5}개" if len(blocking) > 5 else "")
                print(f"[RemoveWorktree] 커밋하지 않은 변경이 있어 건너뜁니다(--force 로 지운다): {plan.worktreePath} — {shown}", file=sys.stderr)
                failureCount += 1
                continue
            for rel in kSharedFolders:
                linkPath = plan.worktreePath / rel
                if isDirectoryLink(linkPath):
                    removeDirectoryLink(linkPath)
            runGit(["worktree", "remove", "--force", str(plan.worktreePath)], cwd=mainRoot)
            if plan.worktreePath.exists() and not removeLeftoverDirectory(plan.worktreePath):
                print(f"[RemoveWorktree] 경고: 폴더가 남았습니다(다른 프로세스가 쥐고 있을 수 있습니다): {plan.worktreePath}", file=sys.stderr)
                failureCount += 1
        if not args.keep_branch:
            # 청소다 — 가지가 이미 없으면 지는 것이 정상이다. 남았으면 이유를 보인다.
            deleted = runGit(["branch", "-D", plan.branch], cwd=mainRoot)
            if not deleted.bSucceeded:
                print(f"[RemoveWorktree] 가지 {plan.branch} 를 지우지 않았습니다: {deleted.stderr.strip()}", file=sys.stderr)
        print(f"[RemoveWorktree] 지움: {name}")
    runGit(["worktree", "prune"], cwd=mainRoot)   # 청소 — 남은 기록만 정리한다, 실패해도 다음 prune 이 한다
    return 1 if failureCount else 0


if __name__ == "__main__":
    sys.exit(main())
