"""
Scripts/common/Host.py

호스트 개발 환경 도구(Git, clang-format 등) 탐색, Git diff 쿼리 및 실행 유틸리티.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence

from .ClangFormat import resolveClangFormat
from .Constants import kCppAllExtensions
from .Parallel import runUntilNonZero
from .Paths import getProjectRoot, normalizePath


def resolveGitExecutable() -> str | None:
    """
    시스템 PATH 및 Windows 기본 설치 폴더(Program Files, LocalAppData 등)에서 git 실행 파일(git.exe)을 찾습니다.
    (CMake Tools나 IDE 터미널 등 PATH 환경변수가 축소된 환경에서도 Git for Windows를 안정적으로 찾아냅니다.)
    """
    if foundPath := shutil.which("git"):
        return foundPath

    if sys.platform == "win32":
        programFiles = os.environ.get("ProgramFiles", r"C:\Program Files")
        programFilesX86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        localAppData = os.environ.get("LocalAppData", "")
        candidateList = [
            Path(programFiles) / "Git" / "cmd" / "git.exe",
            Path(programFiles) / "Git" / "bin" / "git.exe",
            Path(programFilesX86) / "Git" / "cmd" / "git.exe",
            Path(programFilesX86) / "Git" / "bin" / "git.exe",
        ]
        if localAppData:
            candidateList.extend([
                Path(localAppData) / "Programs" / "Git" / "cmd" / "git.exe",
                Path(localAppData) / "Programs" / "Git" / "bin" / "git.exe",
            ])
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
    pathParts = os.environ.get("PATH", "").split(os.pathsep)
    pathLower = {part.lower() for part in pathParts if part}
    if gitDir.lower() not in pathLower:
        os.environ["PATH"] = gitDir + os.pathsep + os.environ.get("PATH", "")
    return gitPath


def runGit(args: Sequence[str],
           *,
           cwd: Path | None = None,
           check: bool = False) -> subprocess.CompletedProcess:
    """
    Git 명령어를 실행하고 결과를 반환합니다.
    """
    gitExe = resolveGitExecutable() or "git"
    return subprocess.run(
        [gitExe, *args],
        cwd=str(cwd) if cwd is not None else None,
        capture_output=True,
        text=True,
        check=check,
    )


_kStagedFileArgument = ["diff", "--cached", "--name-only", "--diff-filter=ACM"]


def listGitFilesInternal(projectRoot: Path, listGitArgument: list[str], extensions: set[str] | None) -> set[Path]:
    """
    git 명령이 한 줄에 하나씩 찍는 저장소 기준 경로를 절대 경로로 풀어 모읍니다. 지금 있는 파일만, `extensions` 가 있으면 그 확장자만.

    staged 전체 · staged C++ · 수정된 C++ · untracked C++ 네 곳이 이 여덟 줄을 각자 적고 있었습니다.
    """
    fileSet: set[Path] = set()
    gitResult = runGit(listGitArgument, cwd=projectRoot)
    if gitResult.returncode != 0:
        return fileSet
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
    return sorted(listGitFilesInternal(root or getProjectRoot(), _kStagedFileArgument, None))


def getStagedCppFiles(root: Path | None = None,
                      extensions: set[str] | None = None) -> list[Path]:
    """
    Git Staged(인덱스) 상태인 C++ 소스 및 헤더 파일 목록을 반환합니다.
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    return sorted(listGitFilesInternal(root or getProjectRoot(), _kStagedFileArgument, targetExtensions))


def getModifiedCppFiles(root: Path | None = None,
                        extensions: set[str] | None = None,
                        includeUntracked: bool = True) -> list[Path]:
    """
    Git 작업 트리에서 수정(Tracked)되었거나 새로 생성된(Untracked) C++ 소스/헤더 파일 목록을 반환합니다.
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    projectRoot = root or getProjectRoot()

    # 1. 수정된 파일 (Staged + Unstaged)
    fileSet = listGitFilesInternal(projectRoot, ["diff", "--name-only", "HEAD"], targetExtensions)
    # 2. 새로 추가된 파일 (Untracked)
    if includeUntracked:
        fileSet |= listGitFilesInternal(projectRoot, ["ls-files", "--others", "--exclude-standard"], targetExtensions)
    return sorted(fileSet)



def runClangFormatBatch(files: Sequence[Path | str],
                        *,
                        checkOnly: bool = False,
                        cwd: Path | None = None,
                        clangFormatPath: str | None = None,
                        batchSize: int = 64) -> int:
    """
    지정된 파일 목록에 대해 clang-format을 배치 단위로 병렬 실행합니다.

    Args:
        files: 포맷팅할 파일 경로 목록
        checkOnly: True이면 --dry-run --Werror 모드로 검사만 수행, False이면 -i 모드로 파일 직접 수정
        cwd: 실행 기준 디렉터리 (기본값: 프로젝트 루트)
        clangFormatPath: 사용할 clang-format 실행 파일 경로 (None이면 자동 탐색)
        batchSize: 1회 배치당 처리할 파일 개수 (기본 64개)

    Returns:
        모든 배치가 성공하면 0, 실패 시 해당 프로세스 종료 코드 반환
    """
    if not files:
        return 0
    projectRoot = cwd or getProjectRoot()
    clangFormatExe = clangFormatPath or resolveClangFormat()
    if not clangFormatExe:
        sys.stderr.write(
            "[RunClangFormat] clang-format을 찾을 수 없습니다. "
            "'py -3 Scripts/setup/SetupEnvironment.py'를 먼저 실행해주세요.\n"
        )
        return 1

    filePathStrings = [str(f) for f in files]
    batches = [
        filePathStrings[startIndex : startIndex + batchSize]
        for startIndex in range(0, len(filePathStrings), batchSize)
    ]

    def runSingleBatchInternal(batch: list[str]) -> int:
        command = [clangFormatExe]
        if checkOnly:
            command.extend(["--dry-run", "--Werror"])
        else:
            command.append("-i")
        command.extend(batch)
        result = subprocess.run(command, cwd=str(projectRoot), check=False)
        return result.returncode

    # clang-format 은 자식 프로세스라 코어 수를 넘겨 띄우면 서로 경합한다 (Parallel 의 프로세스 정책).
    return runUntilNonZero(runSingleBatchInternal, batches)
