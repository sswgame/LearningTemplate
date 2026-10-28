"""
Scripts/common/Git.py

git 실행 파일 탐색과 git 이 알려 주는 파일 목록 — setup(vcpkg 고정)과 lint(staged 파일)가 함께 쓴다.
"""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path
from typing import Sequence

from .Paths import getProjectRoot
from .Process import ProcessResult, runProcess


def resolveGitExecutable() -> str | None:
    """
    시스템 PATH 및 Windows 기본 설치 폴더(Program Files, LocalAppData 등)에서 git 실행 파일(git.exe)을 찾습니다.
    (CMake Tools나 IDE 터미널 등 PATH 환경변수가 축소된 환경에서도 Git for Windows를 안정적으로 찾아냅니다.)
    """
    if foundPath := shutil.which("git"):
        return foundPath

    if sys.platform == "win32":
        # 설치 폴더는 환경 변수가 정본이다 — 경로 리터럴로 대신하면 다른 드라이브에 깐 PC 에서 엉뚱한 곳을 본다.
        # ProgramFiles 는 Windows 가 늘 정의하므로 없으면 환경이 깨진 것이다. 나머지 둘은 없는 PC 가 있다(32비트 · 사용자 설치 안 함).
        if "ProgramFiles" not in os.environ:
            raise RuntimeError("[Git] 환경 변수 ProgramFiles 가 없어 Git 설치 폴더를 찾을 수 없습니다")
        listInstallRoot = [Path(os.environ["ProgramFiles"])]
        if programFilesX86 := os.environ.get("ProgramFiles(x86)"):
            listInstallRoot.append(Path(programFilesX86))
        if localAppData := os.environ.get("LocalAppData"):
            listInstallRoot.append(Path(localAppData) / "Programs")
        candidateList = [root / "Git" / folder / "git.exe" for root in listInstallRoot for folder in ("cmd", "bin")]
        return next((str(cand.resolve()) for cand in candidateList if cand.is_file()), None)
    return None


def ensureGitOnPath() -> str | None:
    """
    git 실행 파일을 찾아 프로세스 환경변수(PATH)에 등록하고 실행 파일 경로를 반환합니다.
    (git을 찾지 못한 경우 None을 반환합니다.)
    """
    gitPath = resolveGitExecutable()
    if gitPath is None:
        return None

    gitDir = str(Path(gitPath).parent)
    pathParts = os.environ["PATH"].split(os.pathsep)
    pathLower = {part.lower() for part in pathParts if part}
    if gitDir.lower() not in pathLower:
        os.environ["PATH"] = gitDir + os.pathsep + os.environ["PATH"]
    return gitPath


def runGit(args: Sequence[str], *, cwd: Path | None = None) -> ProcessResult:
    """
    Git 명령어를 실행하고 결과를 반환합니다(출력은 UTF-8 — 한글 경로 · 커밋 메시지가 시스템 코드 페이지로 깨지지 않게).
    """
    gitExe = resolveGitExecutable() or "git"
    return runProcess([gitExe, *args], cwd=cwd)


kStagedFileArgument = ["diff", "--cached", "--name-only", "--diff-filter=ACM"]


def listGitFiles(projectRoot: Path, listGitArgument: list[str], extensions: set[str] | None) -> set[Path]:
    """
    git 명령이 한 줄에 하나씩 찍는 저장소 기준 경로를 절대 경로로 풀어 모읍니다. 지금 있는 파일만, `extensions` 가 있으면 그 확장자만.

    staged 전체 · staged C++ · 수정된 C++ · untracked C++ 네 곳이 이 여덟 줄을 각자 적고 있었습니다.
    """
    fileSet: set[Path] = set()
    gitResult = runGit(listGitArgument, cwd=projectRoot)
    if gitResult.returnCode != 0:
        # 목록을 못 얻으면 "바뀐 파일 없음" 이 아니다 — 빈 목록이면 커밋 훅이 검사 없이 통과한다.
        raise RuntimeError(f"git {' '.join(listGitArgument)} 실패: {gitResult.stderr.strip()}")
    for rawLine in gitResult.stdout.splitlines():
        if line := rawLine.strip():
            resolvedPath = (projectRoot / line).resolve()
            if resolvedPath.is_file() and (extensions is None or resolvedPath.suffix.lower() in extensions):
                fileSet.add(resolvedPath)
    return fileSet


def getAllStagedFiles(root: Path | None = None) -> list[Path]:
    """
    Git Staged(인덱스) 상태인 모든 파일 목록을 반환합니다.
    """
    return sorted(listGitFiles(root or getProjectRoot(), kStagedFileArgument, None))
