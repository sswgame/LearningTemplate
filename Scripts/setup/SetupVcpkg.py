#!/usr/bin/env python3
"""
Scripts/setup/SetupVcpkg.py

vcpkg: VCPKG_ROOT → PATH → vcpkg_search_roots → Tools/vcpkg → opt-in clone.

  python3 Scripts/setup/SetupVcpkg.py
  python3 Scripts/setup/SetupVcpkg.py --install
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    autoBootstrapEnabled,
    ensureGitOnPath,
    findFirstValidRoot,
    isVcpkgRoot,
    kDirToolsVcpkg,
    kEnvSwVcpkgAutoBootstrap,
    kKeyVcpkgAutoBootstrap,
    kKeyVcpkgGitCommit,
    kKeyVcpkgGitUrl,
    kKeyVcpkgRoot,
    kKeyVcpkgSearchRoots,
    kKeyVcpkgToolsSubdir,
    loadSearchPaths,
    platformSearchRoots,
    recordEnginePath,
    resolveToolsSubdir,
    runGit,
)


def pinVcpkgCommitInternal(vcpkgRoot: Path, commit: str, gitExe: str) -> None:
    """
    Tools/vcpkg 디렉터리가 Git 워크트리인 경우, search_paths.json에 지정된 특정 커밋(vcpkg_git_commit)으로 체크아웃합니다.
    (단, 시스템 환경변수 VCPKG_ROOT나 PATH로 탐색된 외부 트리는 수정하지 않습니다.)
    """
    commit = (commit or "").strip()
    if not commit:
        return
    gitProbe = runGit(["-C", str(vcpkgRoot), "rev-parse", "--is-inside-work-tree"])
    if gitProbe.returncode != 0 or gitProbe.stdout.strip() != "true":
        print(f"[SetupVcpkg] Skip pin (not a git work tree): {vcpkgRoot}", file=sys.stderr)
        return

    head = runGit(["-C", str(vcpkgRoot), "rev-parse", "HEAD"])
    resolved = runGit(["-C", str(vcpkgRoot), "rev-parse", commit])
    if (
        head.returncode == 0
        and resolved.returncode == 0
        and head.stdout.strip() == resolved.stdout.strip()
    ):
        return

    print(f"[SetupVcpkg] Pinning {vcpkgRoot} to {commit}", file=sys.stderr)
    checkout = runGit(["-C", str(vcpkgRoot), "checkout", "--detach", commit])
    if checkout.returncode == 0:
        return
    fetch = runGit(["-C", str(vcpkgRoot), "fetch", "--depth", "1", "origin", commit])
    if fetch.returncode != 0:
        sys.stderr.write(
            f"[SetupVcpkg] Could not fetch {commit} ({fetch.stderr.strip() or checkout.stderr.strip()})\n"
        )
        return
    checkout = runGit(["-C", str(vcpkgRoot), "checkout", "--detach", commit])
    if checkout.returncode != 0:
        sys.stderr.write(f"[SetupVcpkg] checkout {commit} failed: {checkout.stderr.strip()}\n")


def removeIncompleteVcpkgTreeInternal(toolsDir: Path) -> None:
    """다운로드나 부트스트랩 도중 중단되어 불완전하게 남은 Tools/vcpkg 디렉터리를 삭제하여 정리합니다."""
    if not toolsDir.exists():
        return
    if isVcpkgRoot(toolsDir):
        return
    print(f"[SetupVcpkg] Removing incomplete vcpkg tree: {toolsDir}", file=sys.stderr)
    shutil.rmtree(toolsDir, ignore_errors=True)


def bootstrapVcpkgInternal(toolsDir: Path, gitUrl: str, gitCommit: str, gitExe: str) -> bool:
    """
    vcpkg Git 저장소를 Tools/vcpkg로 클론하고 bootstrap 스크립트를 실행하여 빌드 환경을 구축합니다.

    Returns:
        부트스트랩 성공 시 True, 실패 시 False
    """
    toolsDir.parent.mkdir(parents=True, exist_ok=True)
    removeIncompleteVcpkgTreeInternal(toolsDir)

    print(f"[SetupVcpkg] Cloning into {toolsDir}...", file=sys.stderr)
    print(f"[SetupVcpkg] Using git: {gitExe}", file=sys.stderr)

    clone = runGit(["clone", gitUrl, str(toolsDir)])
    if clone.returncode != 0:
        sys.stderr.write(
            f"[SetupVcpkg Error] git clone failed:\n{clone.stderr.strip() or clone.stdout.strip()}\n"
        )
        removeIncompleteVcpkgTreeInternal(toolsDir)
        return False

    if gitCommit:
        checkout = runGit(["-C", str(toolsDir), "checkout", "--detach", gitCommit])
        if checkout.returncode != 0:
            sys.stderr.write(
                f"[SetupVcpkg Error] checkout {gitCommit} failed:\n{checkout.stderr.strip()}\n"
            )
            removeIncompleteVcpkgTreeInternal(toolsDir)
            return False

    bootstrap = (
        toolsDir / "bootstrap-vcpkg.bat" if sys.platform == "win32" else toolsDir / "bootstrap-vcpkg.sh"
    )
    bootstrapCmd = (
        ["cmd", "/c", str(bootstrap)] if sys.platform == "win32" else ["bash", str(bootstrap)]
    )

    if not bootstrap.is_file():
        sys.stderr.write(f"[SetupVcpkg Error] bootstrap script missing: {bootstrap}\n")
        removeIncompleteVcpkgTreeInternal(toolsDir)
        return False

    print(f"[SetupVcpkg] Running {bootstrap.name}...", file=sys.stderr)
    bootstrapProcess = subprocess.run(
        bootstrapCmd, cwd=str(toolsDir), capture_output=True, text=True, check=False
    )
    if bootstrapProcess.returncode != 0:
        sys.stderr.write(
            f"[SetupVcpkg Error] bootstrap failed (exit {bootstrapProcess.returncode}):\n"
            f"{bootstrapProcess.stderr.strip() or bootstrapProcess.stdout.strip()}\n"
        )
        removeIncompleteVcpkgTreeInternal(toolsDir)
        return False

    if not isVcpkgRoot(toolsDir):
        sys.stderr.write("[SetupVcpkg Error] bootstrap finished but vcpkg.cmake is missing.\n")
        removeIncompleteVcpkgTreeInternal(toolsDir)
        return False
    return True


def setupVcpkg(allowBootstrap: bool = False) -> Path | None:
    """
    시스템 또는 프로젝트 내에서 vcpkg 경로를 탐색하고, 필요한 경우 새로 부트스트랩합니다.

    Args:
        allowBootstrap: True일 경우 vcpkg가 없으면 Tools/vcpkg에 자동으로 clone 및 빌드합니다.
            False여도 search_paths.vcpkg_auto_bootstrap / SW_VCPKG_AUTO_BOOTSTRAP 이면 허용됩니다.

    Returns:
        탐색된 vcpkg 경로(Path 객체). 찾지 못한 경우 None을 반환합니다.
    """
    search = loadSearchPaths()
    toolsDir = resolveToolsSubdir(kKeyVcpkgToolsSubdir, kDirToolsVcpkg, search)
    extras = {kKeyVcpkgToolsSubdir: search.get(kKeyVcpkgToolsSubdir, kDirToolsVcpkg)}

    for envVar in ("VCPKG_ROOT", "VCPKG_INSTALLATION_ROOT"):
        if (envValue := os.environ.get(envVar)) and isVcpkgRoot(envValue):
            print(f"[SetupVcpkg] Using {envVar}: {envValue}", file=sys.stderr)
            return Path(recordEnginePath(kKeyVcpkgRoot, envValue))

    if vcpkgBin := shutil.which("vcpkg"):
        binPath = Path(vcpkgBin).resolve()
        for candidate in (binPath.parent, binPath.parent.parent):
            if isVcpkgRoot(candidate):
                print(f"[SetupVcpkg] Using PATH: {candidate}", file=sys.stderr)
                return Path(recordEnginePath(kKeyVcpkgRoot, candidate))

    found = findFirstValidRoot(
        platformSearchRoots(search, kKeyVcpkgSearchRoots),
        isVcpkgRoot,
        extras=extras,
        skip=toolsDir,
    )
    if found:
        print(f"[SetupVcpkg] Using search root: {found}", file=sys.stderr)
        return Path(recordEnginePath(kKeyVcpkgRoot, found))

    if isVcpkgRoot(toolsDir):
        gitExe = ensureGitOnPath()
        gitCommit = str(search.get(kKeyVcpkgGitCommit, "")).strip()
        if gitExe is not None and gitCommit:
            pinVcpkgCommitInternal(toolsDir, gitCommit, gitExe)
        print(f"[SetupVcpkg] Using project kit: {toolsDir}", file=sys.stderr)
        return Path(recordEnginePath(kKeyVcpkgRoot, toolsDir))

    if not autoBootstrapEnabled(
        allowBootstrap,
        kKeyVcpkgAutoBootstrap,
        kEnvSwVcpkgAutoBootstrap,
        default=False,
        search=search,
    ):
        sys.stderr.write(
            "[SetupVcpkg] vcpkg not found. Set VCPKG_ROOT, use Tools/vcpkg, "
            "or --install / vcpkg_auto_bootstrap / SW_VCPKG_AUTO_BOOTSTRAP.\n"
        )
        return None

    gitExe = ensureGitOnPath()
    if gitExe is None:
        sys.stderr.write(
            "[SetupVcpkg Error] git not found on PATH (or typical Git for Windows locations).\n"
            "  Install Git for Windows, or add git to PATH, then re-run CMake configure / "
            "Scripts/setup/SetupVcpkg.py --install.\n"
        )
        return None

    gitUrl = str(search.get(kKeyVcpkgGitUrl, "https://github.com/microsoft/vcpkg.git"))
    gitCommit = str(search.get(kKeyVcpkgGitCommit, "")).strip()
    if not bootstrapVcpkgInternal(toolsDir, gitUrl, gitCommit, gitExe):
        return None
    return Path(recordEnginePath(kKeyVcpkgRoot, toolsDir))


def main(argv: list[str] | None = None) -> int:
    """
    vcpkg 설정 스크립트의 CLI 진입점입니다.

    Args:
        argv: 명령줄 인수 리스트.

    Returns:
        종료 코드 (성공 시 0).
    """
    parser = argparse.ArgumentParser(description="Locate or bootstrap vcpkg.")
    parser.add_argument(
        "--install",
        action="store_true",
        help="If not found, git clone + bootstrap under Tools/vcpkg",
    )
    parsedArgs = parser.parse_args(argv)
    resolvedPath = setupVcpkg(allowBootstrap=parsedArgs.install)
    if resolvedPath:
        print(resolvedPath.as_posix())
        return 0
    sys.stderr.write("[SetupVcpkg Error] vcpkg not found\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
