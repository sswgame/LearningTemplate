"""DXC / DXIL 공유 라이브러리 경로 탐색."""

from __future__ import annotations

import os
import platform
import shutil
from pathlib import Path
from typing import List, Tuple

from ConfigHelper import (
    FindFirstExistingFileRecursive,
    LoadSearchPaths,
    NormalizePath,
)


def _FindVcpkgInstalledDirs(project_root: Path) -> List[Path]:
    result: List[Path] = []
    search = LoadSearchPaths()
    rel = search.get("vcpkg_installed_rel", "build/vcpkg_installed")
    preferred = project_root / rel
    if preferred.is_dir():
        result.append(preferred)

    if not project_root.exists():
        return result

    try:
        for candidate in project_root.rglob("vcpkg_installed"):
            if candidate.is_dir() and candidate not in result:
                result.append(candidate)
    except (OSError, PermissionError):
        pass
    return result


def FindDxcDlls(sdk_dir: str, sdk_ver: str, project_root: Path) -> Tuple[str, str]:
    """
    dxcompiler / dxil 경로를 찾습니다.
    우선순위: vcpkg_installed → VULKAN_SDK → Windows SDK → PATH
    """
    sys_name = platform.system()
    if sys_name == "Windows":
        dxc_names = ["dxcompiler.dll"]
        dxil_names = ["dxil.dll"]
    elif sys_name == "Darwin":
        dxc_names = ["libdxcompiler.dylib"]
        dxil_names = ["libdxil.dylib"]
    else:
        dxc_names = ["libdxcompiler.so", "libdxcompiler.so.1"]
        dxil_names = ["libdxil.so", "libdxil.so.1"]

    dxc_dll = ""
    dxil_dll = ""

    vcpkg_dirs = _FindVcpkgInstalledDirs(project_root)
    if vcpkg_dirs:
        dxc_dll = FindFirstExistingFileRecursive(vcpkg_dirs, dxc_names)
        dxil_dll = FindFirstExistingFileRecursive(vcpkg_dirs, dxil_names)

    if not dxc_dll or not dxil_dll:
        vulkan_sdk = os.environ.get("VULKAN_SDK")
        if vulkan_sdk:
            vulkan_root = Path(vulkan_sdk)
            if not dxc_dll:
                dxc_dll = FindFirstExistingFileRecursive([vulkan_root], dxc_names)
            if not dxil_dll:
                dxil_dll = FindFirstExistingFileRecursive([vulkan_root], dxil_names)

    if sys_name == "Windows" and sdk_dir and sdk_ver:
        sdk_root = Path(sdk_dir)
        if not dxc_dll:
            dxc_dll = FindFirstExistingFileRecursive([sdk_root], dxc_names)
        if not dxil_dll:
            dxil_dll = FindFirstExistingFileRecursive([sdk_root], dxil_names)

    if not dxc_dll:
        for name in dxc_names:
            found = shutil.which(name)
            if found:
                dxc_dll = NormalizePath(found)
                break

    if not dxil_dll:
        for name in dxil_names:
            found = shutil.which(name)
            if found:
                dxil_dll = NormalizePath(found)
                break

    return NormalizePath(dxc_dll), NormalizePath(dxil_dll)
