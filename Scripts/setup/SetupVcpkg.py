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
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ConfigHelper import (
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
)

def runGitInternal(gitExe: str, argList: list[str], *, cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
    """git 실행 파일을 절대 경로로 호출합니다."""
    return subprocess.run(
        [gitExe, *argList],
        cwd=str(cwd) if cwd is not None else None,
        capture_output=True,
        text=True,
        check=False,
    )

def pinVcpkgCommitInternal(root: Path, commit: str, gitExe: str) -> None:
    """
    Tools/vcpkg가 이미 있으면 search_paths.vcpkg_git_commit으로 detach checkout.
    VCPKG_ROOT/PATH로 잡은 외부 트리는 건드리지 않는다.
    """
    commit = (commit or "").strip()
    if not commit:
        return
    gitProbe = runGitInternal(gitExe, ["-C", str(root), "rev-parse", "--is-inside-work-tree"])
    if gitProbe.returncode != 0 or gitProbe.stdout.strip() != "true":
        print(f"[SetupVcpkg] Skip pin (not a git work tree): {root}", file=sys.stderr)
        return

    head = runGitInternal(gitExe, ["-C", str(root), "rev-parse", "HEAD"])
    resolved = runGitInternal(gitExe, ["-C", str(root), "rev-parse", commit])
    if (
        head.returncode == 0
        and resolved.returncode == 0
        and head.stdout.strip() == resolved.stdout.strip()
    ):
        return

    print(f"[SetupVcpkg] Pinning {root} to {commit}", file=sys.stderr)
    checkout = runGitInternal(gitExe, ["-C", str(root), "checkout", "--detach", commit])
    if checkout.returncode == 0:
        return
    fetch = runGitInternal(gitExe, ["-C", str(root), "fetch", "--depth", "1", "origin", commit])
    if fetch.returncode != 0:
        sys.stderr.write(
            f"[SetupVcpkg] Could not fetch {commit} ({fetch.stderr.strip() or checkout.stderr.strip()})\n"
        )
        return
    checkout = runGitInternal(gitExe, ["-C", str(root), "checkout", "--detach", commit])
    if checkout.returncode != 0:
        sys.stderr.write(f"[SetupVcpkg] checkout {commit} failed: {checkout.stderr.strip()}\n")

def removeIncompleteVcpkgTreeInternal(tools: Path) -> None:
    """실패한/불완전한 Tools/vcpkg 잔여물을 지웁니다."""
    if not tools.exists():
        return
    if isVcpkgRoot(tools):
        return
    print(f"[SetupVcpkg] Removing incomplete vcpkg tree: {tools}", file=sys.stderr)
    shutil.rmtree(tools, ignore_errors=True)

def bootstrapVcpkgInternal(tools: Path, gitUrl: str, gitCommit: str, gitExe: str) -> bool:
    """
    Tools/vcpkg로 clone + bootstrap.
    @return 성공 시 True
    """
    tools.parent.mkdir(parents=True, exist_ok=True)
    removeIncompleteVcpkgTreeInternal(tools)

    print(f"[SetupVcpkg] Cloning into {tools}...", file=sys.stderr)
    print(f"[SetupVcpkg] Using git: {gitExe}", file=sys.stderr)

    clone = runGitInternal(gitExe, ["clone", gitUrl, str(tools)])
    if clone.returncode != 0:
        sys.stderr.write(f"[SetupVcpkg Error] git clone failed:\n{clone.stderr.strip() or clone.stdout.strip()}\n")
        removeIncompleteVcpkgTreeInternal(tools)
        return False

    if gitCommit:
        checkout = runGitInternal(gitExe, ["-C", str(tools), "checkout", "--detach", gitCommit])
        if checkout.returncode != 0:
            sys.stderr.write(
                f"[SetupVcpkg Error] checkout {gitCommit} failed:\n{checkout.stderr.strip()}\n"
            )
            removeIncompleteVcpkgTreeInternal(tools)
            return False

    if sys.platform == "win32":
        bootstrap = tools / "bootstrap-vcpkg.bat"
        bootstrapCmd = ["cmd", "/c", str(bootstrap)]
    else:
        bootstrap = tools / "bootstrap-vcpkg.sh"
        bootstrapCmd = ["bash", str(bootstrap)]

    if not bootstrap.is_file():
        sys.stderr.write(f"[SetupVcpkg Error] bootstrap script missing: {bootstrap}\n")
        removeIncompleteVcpkgTreeInternal(tools)
        return False

    print(f"[SetupVcpkg] Running {bootstrap.name}...", file=sys.stderr)
    boot = subprocess.run(bootstrapCmd, cwd=str(tools), capture_output=True, text=True, check=False)
    if boot.returncode != 0:
        sys.stderr.write(
            f"[SetupVcpkg Error] bootstrap failed (exit {boot.returncode}):\n"
            f"{boot.stderr.strip() or boot.stdout.strip()}\n"
        )
        removeIncompleteVcpkgTreeInternal(tools)
        return False

    if not isVcpkgRoot(tools):
        sys.stderr.write("[SetupVcpkg Error] bootstrap finished but vcpkg.cmake is missing.\n")
        removeIncompleteVcpkgTreeInternal(tools)
        return False
    return True

def setupVcpkg(allowBootstrap: bool = False) -> Optional[Path]:
    """
    시스템 또는 프로젝트 내에서 vcpkg 경로를 탐색하고, 필요한 경우 새로 부트스트랩합니다.

    Args:
        allowBootstrap: True일 경우 vcpkg가 없으면 Tools/vcpkg에 자동으로 clone 및 빌드합니다.
            False여도 search_paths.vcpkg_auto_bootstrap / SW_VCPKG_AUTO_BOOTSTRAP 이면 허용됩니다.

    Returns:
        탐색된 vcpkg 경로(Path 객체). 찾지 못한 경우 None을 반환합니다.
    """
    search = loadSearchPaths()
    tools = resolveToolsSubdir(kKeyVcpkgToolsSubdir, kDirToolsVcpkg, search)
    extras = {kKeyVcpkgToolsSubdir: search.get(kKeyVcpkgToolsSubdir, kDirToolsVcpkg)}

    for envVar in ("VCPKG_ROOT", "VCPKG_INSTALLATION_ROOT"):
        val = os.environ.get(envVar)
        if val and isVcpkgRoot(val):
            print(f"[SetupVcpkg] Using {envVar}: {val}", file=sys.stderr)
            return Path(recordEnginePath(kKeyVcpkgRoot, val))

    vcpkgBin = shutil.which("vcpkg")
    if vcpkgBin:
        binPath = Path(vcpkgBin).resolve()
        for candidate in (binPath.parent, binPath.parent.parent):
            if isVcpkgRoot(candidate):
                print(f"[SetupVcpkg] Using PATH: {candidate}", file=sys.stderr)
                return Path(recordEnginePath(kKeyVcpkgRoot, candidate))

    found = findFirstValidRoot(
        platformSearchRoots(search, kKeyVcpkgSearchRoots),
        isVcpkgRoot,
        extras=extras,
        skip=tools,
    )
    if found:
        print(f"[SetupVcpkg] Using search root: {found}", file=sys.stderr)
        return Path(recordEnginePath(kKeyVcpkgRoot, found))

    if isVcpkgRoot(tools):
        gitExe = ensureGitOnPath()
        gitCommit = str(search.get(kKeyVcpkgGitCommit, "")).strip()
        if gitExe is not None and gitCommit:
            pinVcpkgCommitInternal(tools, gitCommit, gitExe)
        print(f"[SetupVcpkg] Using project kit: {tools}", file=sys.stderr)
        return Path(recordEnginePath(kKeyVcpkgRoot, tools))

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
    if not bootstrapVcpkgInternal(tools, gitUrl, gitCommit, gitExe):
        return None
    return Path(recordEnginePath(kKeyVcpkgRoot, tools))

def main(argv: Optional[list] = None) -> int:
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
    args = parser.parse_args(argv)
    path = setupVcpkg(allowBootstrap=args.install)
    if path:
        print(path.as_posix())
        return 0
    sys.stderr.write("[SetupVcpkg Error] vcpkg not found\n")
    return 1

if __name__ == "__main__":
    sys.exit(main())
