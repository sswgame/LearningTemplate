"""
@file Worktree.py
@brief git 워크트리 라이브러리 — 경로 · 브랜치 계획, main 과 나눠 쓰는 폴더 링크, 주인 기록(`kOwnerFileName`).

명령은 `dev/MakeWorktree.py`(만들기) · `dev/RemoveWorktree.py`(지우기) · `dev/ListWorktrees.py`(목록)이고, 빌드 도구(`RunFullBuild` ·
`RunBuildBaseline`)와 잠금(`common/SharedLock.py`)이 main 체크아웃을 찾을 때도 이 모듈을 쓴다. 작업 방식은 `docs/11_Workflow.md` 1절.
"""

from __future__ import annotations

import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

from .Host import runGit
from .Process import isProcessAlive, runProcess

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
            print(f"[Worktree] 정션을 만들지 못했습니다: {linkPath} -> {targetPath}\n{result.output.strip()}", file=sys.stderr)
        return result.bSucceeded
    try:
        os.symlink(targetPath, linkPath, target_is_directory=True)
    except OSError as error:
        print(f"[Worktree] 링크를 만들지 못했습니다: {linkPath} -> {targetPath} ({error})", file=sys.stderr)
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
