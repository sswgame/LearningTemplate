"""
Scripts/common/Git.py

git 실행 파일 탐색과 git 이 알려 주는 파일 목록 — setup(vcpkg 고정)과 lint(staged 파일)가 함께 쓴다.
"""

from __future__ import annotations

import functools
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence

from .HostPlatform import isWindowsHost
from .RunProcess import ProcessResult, runProcess


def resolveGitExecutable() -> str | None:
    """
    시스템 PATH 및 Windows 기본 설치 폴더(Program Files, LocalAppData 등)에서 git 실행 파일(git.exe)을 찾습니다.
    (CMake Tools나 IDE 터미널 등 PATH 환경변수가 축소된 환경에서도 Git for Windows를 안정적으로 찾아냅니다.)
    """
    if foundPath := shutil.which("git"):
        return foundPath

    if isWindowsHost():
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


@functools.cache
def listRepositoryEnvironmentNames() -> tuple[str, ...]:
    """git 이 어느 저장소 · 인덱스를 볼지 고르는 환경 변수(`git rev-parse --local-env-vars` — git 이 서브모듈에 들어갈 때 스스로 지우는 목록)."""
    completed = subprocess.run([resolveGitExecutable() or "git", "rev-parse", "--local-env-vars"], capture_output=True, encoding="utf-8", errors="replace", check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"git rev-parse --local-env-vars 실패: {completed.stderr.strip()}")
    return tuple(completed.stdout.split())


def makeIsolatedEnvironment() -> dict[str, str]:
    """이 프로세스의 환경에서 저장소를 고르는 git 환경 변수를 뺀 것."""
    setName = set(listRepositoryEnvironmentNames())
    return {name: value for name, value in os.environ.items() if name not in setName}


def runGit(args: Sequence[str], *, cwd: Path | None = None) -> ProcessResult:
    """
    Git 명령어를 실행하고 결과를 반환합니다(출력은 UTF-8 — 한글 경로 · 커밋 메시지가 시스템 코드 페이지로 깨지지 않게).

    저장소는 `cwd`(없으면 이 프로세스의 폴더)가 고른다 — 물려받은 `GIT_DIR` · `GIT_INDEX_FILE` · `GIT_WORK_TREE` 등은 늘 뺀다. 커밋 훅 안에서 부른
    스크립트가 그 값을 물려받으면 다른 저장소(임시 · 클론)를 가리킨 명령이 훅의 저장소를 건드린다(임시 저장소 `git init` 이 공용 config 에 core.bare 를
    썼다). Scripts 는 git 을 이 입구(또는 `runGitInHookRepository` · `runGitBytes`)로만 부른다 — platform-chains 가 subprocess 로 직접 부르는 것을 막는다.
    """
    gitExe = resolveGitExecutable() or "git"
    return runProcess([gitExe, *args], cwd=cwd, environment=makeIsolatedEnvironment())


def runGitOrRaise(args: Sequence[str], *, cwd: Path | None = None) -> str:
    """`runGit` 의 표준 출력 — 지면 멈춘다(`subprocess.check_output` 을 대신한다)."""
    result = runGit(args, cwd=cwd)
    if result.returnCode != 0:
        raise RuntimeError(f"git {' '.join(args)} 실패: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def runGitInHookRepository(args: Sequence[str], *, cwd: Path) -> ProcessResult:
    """
    물려받은 git 환경 변수를 **그대로 둔** git — 커밋 훅 린트가 지금 커밋하는 인덱스를 볼 때만 쓴다. `git commit -a` · `git commit <경로>` 는 임시 인덱스를
    `GIT_INDEX_FILE` 로 넘기므로, 빼면 스테이지 목록 · 블롭이 커밋 내용과 어긋난다. 훅 밖에서는 `runGit` 과 같다.
    """
    gitExe = resolveGitExecutable() or "git"
    return runProcess([gitExe, *args], cwd=cwd)


def runGitBytes(args: Sequence[str], *, cwd: Path, inputBytes: bytes, bHookRepository: bool) -> tuple[int, bytes, str]:
    """표준 입력을 넘기고 바이트 출력을 받는 git(`cat-file --batch`) — (종료 코드, 출력, 오류 글). 환경은 `bHookRepository` 가 고른다(위 두 입구와 같은 뜻)."""
    completed = subprocess.run([resolveGitExecutable() or "git", *args], input=inputBytes, cwd=cwd, capture_output=True, check=False,
                               env=None if bHookRepository else makeIsolatedEnvironment())
    return completed.returncode, completed.stdout, completed.stderr.decode("utf-8", "replace")


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
