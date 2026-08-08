"""LLVM / libclang 경로 탐색."""

from __future__ import annotations

import os
import platform
import shutil
from functools import lru_cache
from pathlib import Path
from typing import List

from ConfigHelper import (
    ExpandPathTemplate,
    FindFirstExistingFile,
    FindFirstExistingFileRecursive,
    LoadSearchPaths,
    NormalizePath,
)


@lru_cache(maxsize=1)
def FindLlvmPath() -> str:
    """PATH / env / search_paths 후보에서 LLVM 루트를 찾습니다."""
    for env_key in ("LLVM_DIR", "LLVM_HOME", "LLVM_ROOT", "LLVM_PATH"):
        env_llvm = os.environ.get(env_key)
        if env_llvm and os.path.exists(env_llvm):
            return NormalizePath(env_llvm)

    llvm_bin = shutil.which("clang-cl") or shutil.which("clang")
    if llvm_bin:
        parent = Path(llvm_bin).resolve().parent.parent
        if (
            (parent / "include").exists()
            or (parent / "bin" / "clang-cl.exe").exists()
            or (parent / "bin" / "clang").exists()
        ):
            return NormalizePath(str(parent))

    search = LoadSearchPaths()
    roots_map = search.get("llvm_search_roots", {})
    sys_key = platform.system().lower()
    if sys_key == "darwin":
        sys_key = "darwin"
    elif sys_key.startswith("win"):
        sys_key = "windows"
    else:
        sys_key = "linux"

    for template in roots_map.get(sys_key, []):
        root = Path(ExpandPathTemplate(str(template)))
        if (
            (root / "bin" / "clang-cl.exe").exists()
            or (root / "bin" / "clang.exe").exists()
            or (root / "bin" / "clang").exists()
        ):
            return NormalizePath(str(root))

    return ""


def FindLibClangDllPath(llvm_path: str) -> str:
    """libclang 공유 라이브러리 경로를 찾습니다."""
    sys_name = platform.system()
    if sys_name == "Windows":
        lib_names = ["libclang.dll"]
    elif sys_name == "Darwin":
        lib_names = ["libclang.dylib"]
    else:
        lib_names = ["libclang.so", "libclang.so.1"]

    search_dirs: List[Path] = []
    if llvm_path:
        llvm_root = Path(llvm_path)
        search_dirs.extend([llvm_root / "bin", llvm_root / "lib"])

    found = FindFirstExistingFile(search_dirs, lib_names)
    if found:
        return NormalizePath(found)

    for lib_name in lib_names:
        which = shutil.which(lib_name)
        if which:
            return NormalizePath(which)

    if llvm_path:
        found = FindFirstExistingFileRecursive([Path(llvm_path)], lib_names)
        if found:
            return NormalizePath(found)

    return ""
