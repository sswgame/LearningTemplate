#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MakeWorktree.py
@brief 작업 단위 하나를 위한 git 워크트리를 만들고, 내려받은 도구 · vcpkg 설치 폴더를 main 체크아웃과 나눠 쓰게 연결합니다.

    py -3 -m Scripts worktree-make <이름>                       # <main 의 부모>/LT-wt/<이름>, 브랜치 wt/<이름>, 기준 origin/main
    py -3 -m Scripts worktree-make <이름> --base wt/apply5      # 다른 브랜치에서 갈라 만든다
    py -3 -m Scripts worktree-make <이름> --dry-run             # 무엇을 할지만 보인다
    py -3 -m Scripts worktree-make <이름> --pid $PID            # 사람 셸 — 주인 프로세스를 직접 준다(에이전트는 CLAUDE_PID 를 쓴다)

만들 때 주인(이름 · 프로세스 번호 · 세션 · 시작 시각)을 워크트리의 git 디렉터리에 적습니다(`kOwnerFileName`). `worktree-list` 와
`worktree-remove` 가 그 프로세스가 살아 있는지 보고 "다른 세션이 쓰는 중" 을 알린다 — 주인을 모르면 남의 워크트리를 지울 수 있어서,
주인 프로세스를 정할 수 없으면 만들지 않는다.

워크트리마다 LLVM · Ninja · vcpkg 를 새로 받고 vcpkg 포트를 다시 빌드하면 수십 분이 걸립니다. 그래서 `kSharedFolders` 의 폴더는
main 체크아웃의 것을 가리키는 링크(Windows 는 정션, 그 밖은 심볼릭 링크)로 둡니다. 이 링크는 지우거나 고치지 않습니다 — 지우는 것은
`RemoveWorktree.py` 가 링크를 먼저 끊은 뒤에 합니다. 작업 방식은 `docs/11_Workflow.md` 에 있습니다.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, isProcessAlive, runGit, runProcess  # noqa: E402

#: main 체크아웃과 나눠 쓰는 폴더(저장소 루트 기준). 내려받은 도구와 vcpkg 설치 트리 · 바이너리 캐시다.
kSharedFolders: tuple[str, ...] = ("Tools/LLVM", "Tools/Ninja", "Tools/vcpkg", "Tools/_cache",
                                   "build/vcpkg_installed", "build/vcpkg_cache")
#: 워크트리를 모아 두는 폴더 이름(main 체크아웃의 부모 아래). Windows 는 드라이브 루트 아래의 짧은 이름을 쓴다 — 경로가 길면 오브젝트 경로(260)와 명령 줄(32767)이 한도를 넘는다.
kWorktreeFolderName = "LT-wt"
kShortWorktreeFolderName = "w"
#: 워크트리 브랜치 이름의 접두.
kBranchPrefix = "wt/"
#: 바이트가 main 과 다르면 vcpkg 스탬프가 어긋나 configure 마다 설치가 도는 파일(`docs/08_Verification.md` 3절).
kTripletGlob = "cmake/Modules/Toolchain/Vcpkg/*.cmake"
#: 워크트리 주인 기록. 워크트리의 git 디렉터리(`<공통 .git>/worktrees/<이름>/`)에 둔다 — 작업 폴더에 두면 `git status` 에 잡혀
#: 지우기가 "커밋하지 않은 변경" 으로 막히고, 실수로 커밋될 수 있다.
kOwnerFileName = "sw-owner.json"
#: 주인 프로세스 번호 · 세션을 주는 환경 변수(Claude Code 세션 프로세스). 하위 에이전트는 부모와 같은 값을 받는다 — 한 세션은 한 주인이다.
kOwnerPidEnvironment = "CLAUDE_PID"
kOwnerSessionEnvironment = "CLAUDE_CODE_SESSION_ID"


@dataclass(frozen=True)
class WorktreeOwner:
    """
    워크트리를 쓰는 주인. `pid` 가 살아 있는 동안 "쓰는 중" 이다 — 주인 프로세스는 세션처럼 오래 사는 프로세스여야 한다
    (`worktree-make` 를 부른 셸 · py 런처는 바로 끝나서 쓸 수 없다). 프로세스 번호는 재사용될 수 있어 "쓰는 중" 은 틀려도 안전한 쪽(지우지 않음)으로만 틀린다.
    """

    owner: str
    pid: int
    session: str
    startTime: float


def resolveOwnerProcessId(pidArgument: int | None) -> int | None:
    """주인 프로세스 번호 — `--pid` 가 먼저, 없으면 `CLAUDE_PID`. 둘 다 없으면 None(부르는 쪽이 오류로 멈출지 정한다)."""
    if pidArgument is not None:
        return pidArgument
    value = os.environ.get(kOwnerPidEnvironment, "").strip()
    return int(value) if value.isdigit() else None


def getWorktreeGitDirectory(worktreePath: Path) -> Path:
    """워크트리의 git 디렉터리(main 이면 `.git`, 워크트리면 `<공통 .git>/worktrees/<이름>`)입니다."""
    result = runGit(["rev-parse", "--absolute-git-dir"], cwd=worktreePath)
    if not result.bSucceeded:
        raise RuntimeError(f"git 워크트리가 아닙니다: {worktreePath}\n{result.stderr.strip()}")
    return Path(result.stdout.strip())


def writeWorktreeOwner(gitDirectory: Path, owner: WorktreeOwner) -> None:
    """주인 기록을 씁니다."""
    record = {"owner": owner.owner, "pid": owner.pid, "session": owner.session, "startTime": owner.startTime}
    (gitDirectory / kOwnerFileName).write_text(json.dumps(record, ensure_ascii=False), encoding="utf-8")


def readWorktreeOwner(gitDirectory: Path) -> WorktreeOwner | None:
    """주인 기록입니다. 기록이 없으면 None(이 기능 전에 만든 워크트리 · main). 기록이 깨졌으면 ValueError."""
    path = gitDirectory / kOwnerFileName
    if not path.is_file():
        return None
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
        return WorktreeOwner(owner=str(record["owner"]), pid=int(record["pid"]), session=str(record["session"]),
                             startTime=float(record["startTime"]))
    except (ValueError, KeyError, TypeError) as error:
        raise ValueError(f"워크트리 주인 기록이 깨졌습니다: {path} ({error})") from error


def isOwnedByOtherLiveProcess(owner: WorktreeOwner | None, currentPid: int | None) -> bool:
    """다른 주인이 살아서 쓰는 중인가. 내 프로세스(`currentPid`)가 주인이면 남이 아니다. 내 번호를 모르면 살아 있는 주인은 모두 남이다."""
    return owner is not None and owner.pid != currentPid and isProcessAlive(owner.pid)


def describeWorktreeOwner(owner: WorktreeOwner | None, now: float) -> str:
    """사람이 읽는 주인 한 줄 — 살아 있는지까지."""
    if owner is None:
        return "주인 기록 없음"
    state = "쓰는 중" if isProcessAlive(owner.pid) else "주인 프로세스 끝남"
    ageMinutes = int((now - owner.startTime) / 60)
    return f"{state} — {owner.owner} (pid {owner.pid}, 세션 {owner.session or '없음'}, {ageMinutes}분 전 시작)"


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


def getDefaultWorktreeRoot(mainRoot: Path) -> Path:
    """워크트리 기본 위치 — Windows 는 `<드라이브>/w`(짧게), 그 밖은 main 의 부모/LT-wt 입니다."""
    if os.name == "nt" and mainRoot.drive:
        return Path(mainRoot.drive + "\\") / kShortWorktreeFolderName
    return mainRoot.parent / kWorktreeFolderName


def makePlan(mainRoot: Path, name: str, base: str, worktreeRoot: Path | None) -> WorktreePlan:
    """이름에서 워크트리 경로와 브랜치를 정합니다. 이름은 폴더 · 브랜치에 그대로 쓰이므로 영숫자 · `-` · `_` 만 받습니다."""
    if not name or any(not (ch.isalnum() or ch in "-_") for ch in name):
        raise ValueError(f"워크트리 이름은 영숫자 · '-' · '_' 만 씁니다: '{name}'")
    root = worktreeRoot if worktreeRoot is not None else getDefaultWorktreeRoot(mainRoot)
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
