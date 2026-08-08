#!/usr/bin/env python3
"""
Scripts/vcpkg/FindVcpkg.py

vcpkg 루트 탐색. 자동 clone/bootstrap 은 opt-in (--install / search_paths / env).

  python3 Scripts/vcpkg/FindVcpkg.py
  python3 Scripts/vcpkg/FindVcpkg.py --install
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from ConfigHelper import (
    EnsureScriptsOnPath,
    GetProjectRoot,
    LoadEngineConfig,
    LoadSearchPaths,
    UpdateEngineConfig,
)

EnsureScriptsOnPath()


def _IsVcpkgRoot(path: Path) -> bool:
    return (path / "scripts" / "buildsystems" / "vcpkg.cmake").exists()


def FindVcpkg(allow_bootstrap: bool = False) -> Optional[Path]:
    """
    vcpkg 루트를 탐색합니다.
    allow_bootstrap=True 일 때만 미설치 시 git clone + bootstrap.
    """
    project_root = GetProjectRoot()
    search = LoadSearchPaths()
    cfg = LoadEngineConfig()

    vcpkg_cfg = cfg.get("vcpkg_root")
    if vcpkg_cfg and _IsVcpkgRoot(Path(vcpkg_cfg)):
        return Path(vcpkg_cfg).resolve()

    tools_subdir = search.get("vcpkg_tools_subdir", "Tools/vcpkg")
    tools_vcpkg = project_root / tools_subdir
    if _IsVcpkgRoot(tools_vcpkg):
        UpdateEngineConfig("vcpkg_root", str(tools_vcpkg))
        return tools_vcpkg.resolve()

    for env_var in ("VCPKG_ROOT", "VCPKG_INSTALLATION_ROOT"):
        val = os.environ.get(env_var)
        if val and _IsVcpkgRoot(Path(val)):
            resolved = Path(val).resolve()
            UpdateEngineConfig("vcpkg_root", str(resolved))
            return resolved

    vcpkg_bin = shutil.which("vcpkg")
    if vcpkg_bin:
        bin_path = Path(vcpkg_bin).resolve()
        for candidate in (bin_path.parent, bin_path.parent.parent):
            if _IsVcpkgRoot(candidate):
                UpdateEngineConfig("vcpkg_root", str(candidate))
                return candidate

    auto = allow_bootstrap
    if not auto:
        auto = bool(search.get("vcpkg_auto_bootstrap", False))
    if not auto:
        env_flag = os.environ.get("SW_VCPKG_AUTO_BOOTSTRAP", "").strip().lower()
        auto = env_flag in ("1", "true", "on", "yes")

    if not auto:
        sys.stderr.write(
            "[FindVcpkg] vcpkg not found. Set VCPKG_ROOT, install under "
            f"{tools_subdir}, or re-run with --install "
            "(or search_paths.vcpkg_auto_bootstrap / SW_VCPKG_AUTO_BOOTSTRAP).\n"
        )
        return None

    tools_vcpkg.parent.mkdir(parents=True, exist_ok=True)
    git_url = search.get("vcpkg_git_url", "https://github.com/microsoft/vcpkg.git")
    sys.stderr.write(f"[FindVcpkg] Cloning vcpkg into {tools_vcpkg}...\n")
    try:
        subprocess.run(["git", "clone", git_url, str(tools_vcpkg)], check=True)
        bootstrap = tools_vcpkg / (
            "bootstrap-vcpkg.bat" if sys.platform == "win32" else "bootstrap-vcpkg.sh"
        )
        subprocess.run([str(bootstrap)], cwd=str(tools_vcpkg), check=True)
        if _IsVcpkgRoot(tools_vcpkg):
            resolved = tools_vcpkg.resolve()
            UpdateEngineConfig("vcpkg_root", str(resolved))
            return resolved
    except Exception as exc:
        sys.stderr.write(f"[FindVcpkg Error] Failed to bootstrap vcpkg: {exc}\n")

    return None


# Backward-compatible alias
def FindOrInstallVcpkg() -> Optional[Path]:
    return FindVcpkg(allow_bootstrap=True)


def Main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description="Locate (or optionally bootstrap) vcpkg.")
    parser.add_argument(
        "--install",
        action="store_true",
        help="If not found, git clone + bootstrap under Tools/vcpkg",
    )
    args = parser.parse_args(argv)
    path = FindVcpkg(allow_bootstrap=args.install)
    if path:
        print(path.as_posix())
        return 0
    sys.stderr.write("[FindVcpkg Error] vcpkg not found\n")
    return 1


if __name__ == "__main__":
    sys.exit(Main())
