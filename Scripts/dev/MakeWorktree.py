#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MakeWorktree.py
@brief 작업 단위 하나를 위한 git 워크트리를 만들고, 내려받은 도구 · vcpkg 설치 폴더를 main 체크아웃과 나눠 쓰게 연결합니다.

    py -3 -m Scripts worktree-make <이름>                       # <main 의 부모>/LT-wt/<이름>, 브랜치 wt/<이름>, 기준 origin/main
    py -3 -m Scripts worktree-make <이름> --base wt/apply5      # 다른 브랜치에서 갈라 만든다
    py -3 -m Scripts worktree-make <이름> --dry-run             # 무엇을 할지만 보인다

워크트리마다 LLVM · Ninja · sccache · vcpkg 를 새로 받고 vcpkg 포트를 다시 빌드하면 수십 분이 걸립니다. 그래서 `kSharedFolders` 의 폴더는
main 체크아웃의 것을 가리키는 링크(Windows 는 정션, 그 밖은 심볼릭 링크)로 둡니다. 이 링크는 지우거나 고치지 않습니다 — 지우는 것은
`RemoveWorktree.py` 가 링크를 먼저 끊은 뒤에 합니다. 작업 방식은 `docs/11_Workflow.md` 에 있습니다.
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, runGit, runProcess  # noqa: E402

#: main 체크아웃과 나눠 쓰는 폴더(저장소 루트 기준). 내려받은 도구와 vcpkg 설치 트리 · 바이너리 캐시다.
kSharedFolders: tuple[str, ...] = ("Tools/LLVM", "Tools/Ninja", "Tools/Sccache", "Tools/vcpkg", "Tools/_cache",
                                   "build/vcpkg_installed", "build/vcpkg_cache")
#: 워크트리를 모아 두는 폴더 이름(main 체크아웃의 부모 아래).
kWorktreeFolderName = "LT-wt"
#: 워크트리 브랜치 이름의 접두.
kBranchPrefix = "wt/"
#: 바이트가 main 과 다르면 vcpkg 스탬프가 어긋나 configure 마다 설치가 도는 파일(`docs/08_Verification.md` 3절).
kTripletGlob = "cmake/Modules/Toolchain/Vcpkg/*.cmake"


@dataclass(frozen=True)
class WorktreePlan:
    """만들 워크트리 하나 — 경로와 브랜치, 기준."""

    mainRoot: Path
    worktreePath: Path
    branch: str
    base: str


def findMainRoot(start: Path) -> Path:
    """`start` 가 속한 저장소의 main 체크아웃(공통 .git 의 부모)입니다. 워크트리 안에서 불러도 main 을 돌려줍니다."""
    result = runGit(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=start)
    if not result.bSucceeded:
        raise RuntimeError(f"git 저장소가 아닙니다: {start}\n{result.stderr.strip()}")
    return Path(result.stdout.strip()).resolve().parent


def makePlan(mainRoot: Path, name: str, base: str, worktreeRoot: Path | None) -> WorktreePlan:
    """이름에서 워크트리 경로와 브랜치를 정합니다. 이름은 폴더 · 브랜치에 그대로 쓰이므로 영숫자 · `-` · `_` 만 받습니다."""
    if not name or any(not (ch.isalnum() or ch in "-_") for ch in name):
        raise ValueError(f"워크트리 이름은 영숫자 · '-' · '_' 만 씁니다: '{name}'")
    root = worktreeRoot if worktreeRoot is not None else mainRoot.parent / kWorktreeFolderName
    return WorktreePlan(mainRoot=mainRoot, worktreePath=root / name, branch=kBranchPrefix + name, base=base)


def listSharedLinks(plan: WorktreePlan) -> list[tuple[Path, Path]]:
    """만들 링크 (워크트리 쪽 경로, main 쪽 대상) 목록 — main 에 실제로 있는 폴더만."""
    return [(plan.worktreePath / rel, plan.mainRoot / rel) for rel in kSharedFolders if (plan.mainRoot / rel).is_dir()]


def createDirectoryLink(linkPath: Path, targetPath: Path) -> bool:
    """`linkPath` 에 `targetPath` 를 가리키는 디렉터리 링크를 만듭니다(Windows 정션 — 관리자 권한이 필요 없다)."""
    linkPath.parent.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        result = runProcess(["cmd", "/c", "mklink", "/J", str(linkPath), str(targetPath)])
        if not result.bSucceeded:
            print(f"[MakeWorktree] 정션을 만들지 못했습니다: {linkPath} -> {targetPath}\n{result.output.strip()}", file=sys.stderr)
        return result.bSucceeded
    try:
        os.symlink(targetPath, linkPath, target_is_directory=True)
    except OSError as error:
        print(f"[MakeWorktree] 링크를 만들지 못했습니다: {linkPath} -> {targetPath} ({error})", file=sys.stderr)
        return False
    return True


def listDifferentTriplets(plan: WorktreePlan) -> list[str]:
    """main 과 바이트가 다른 vcpkg 트리플릿 파일(줄끝이 다르게 체크아웃된 경우)입니다."""
    listDifferent = []
    for mainFile in sorted(plan.mainRoot.glob(kTripletGlob)):
        worktreeFile = plan.worktreePath / mainFile.relative_to(plan.mainRoot)
        if worktreeFile.is_file() and worktreeFile.read_bytes() != mainFile.read_bytes():
            listDifferent.append(mainFile.relative_to(plan.mainRoot).as_posix())
    return listDifferent


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="작업 단위용 git 워크트리를 만들고 도구 · vcpkg 폴더를 main 체크아웃과 나눠 쓰게 연결한다.")
    parser.add_argument("name", help="워크트리 이름 — 폴더 <워크트리 루트>/<이름>, 브랜치 wt/<이름>")
    parser.add_argument("--base", default="origin/main", help="갈라 나올 기준(기본 origin/main — origin/ 이면 먼저 fetch 한다)")
    parser.add_argument("--worktree-root", type=Path, default=None, help=f"워크트리를 둘 폴더(기본: main 체크아웃의 부모/{kWorktreeFolderName})")
    parser.add_argument("--dry-run", action="store_true", help="만들지 않고 할 일만 보인다")
    args = parser.parse_args(argv)

    try:
        mainRoot = findMainRoot(getProjectRoot())
        plan = makePlan(mainRoot, args.name, args.base, args.worktree_root)
    except (RuntimeError, ValueError) as error:
        print(f"[MakeWorktree] {error}", file=sys.stderr)
        return 2

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

    bAllLinked = all([createDirectoryLink(linkPath, targetPath) for linkPath, targetPath in listLink if not linkPath.exists()])
    listDifferent = listDifferentTriplets(plan)
    if listDifferent:
        print("[MakeWorktree] 경고: vcpkg 트리플릿 파일의 바이트가 main 과 다릅니다(줄끝) — 두 트리가 서로의 vcpkg 스탬프를 어긋남으로 봅니다.\n"
              "  그 파일을 지우고 `git checkout -- <파일>` 로 다시 받으세요: " + ", ".join(listDifferent), file=sys.stderr)
    print(f"[MakeWorktree] 완료: {plan.worktreePath}")
    return 0 if bAllLinked else 1


if __name__ == "__main__":
    sys.exit(main())
