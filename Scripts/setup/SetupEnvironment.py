#!/usr/bin/env python3
"""
Scripts/setup/SetupEnvironment.py

개발 PC 도구 경로를 모아 Config/engine_config.json / parser_config.json 에 기록하는 오케스트레이터.
실제 탐색 로직은 setup/find/*, vcpkg/FindVcpkg, SetupNinja 에 있습니다.

  python3 Scripts/setup/SetupEnvironment.py
"""

from __future__ import annotations

import json
import os
import platform
import sys
from pathlib import Path
from typing import Any, Dict

_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from ConfigHelper import (
    EnsureScriptsOnPath,
    GetOrFindCached,
    GetProjectRoot,
    LoadEngineConfig,
    NormalizePath,
)

EnsureScriptsOnPath()

from setup.find.FindDxc import FindDxcDlls
from setup.find.FindLlvm import FindLibClangDllPath, FindLlvmPath
from setup.find.FindMsvc import FindMsvcPath
from setup.find.FindSystemIncludes import FindSystemIncludeDirs
from setup.find.FindWindowsSdk import FindWindowsSdkPath
from setup.UpdateParserConfig import UpdateParserConfig


def SetupEnvironment() -> Dict[str, Any]:
    """시스템을 쿼리하여 engine_config / parser_config 를 갱신합니다."""
    project_root = GetProjectRoot()
    config_dir = project_root / "Config"
    config_dir.mkdir(parents=True, exist_ok=True)

    engine_config_file = config_dir / "engine_config.json"
    parser_config_file = config_dir / "parser_config.json"
    existing_config = LoadEngineConfig()

    llvm_path = GetOrFindCached(existing_config, "llvm_path", FindLlvmPath)
    libclang_dll_path = GetOrFindCached(
        existing_config, "libclang_dll_path", FindLibClangDllPath, llvm_path
    )

    sdk_dir_val = existing_config.get("windows_sdk_dir", "")
    sdk_ver_val = existing_config.get("windows_sdk_version", "")
    if platform.system() == "Windows" and (not sdk_dir_val or not os.path.exists(sdk_dir_val)):
        sdk_dir_val, sdk_ver_val = FindWindowsSdkPath()
    if platform.system() != "Windows":
        sdk_dir_val = ""
        sdk_ver_val = ""

    found_dxc, found_dxil = FindDxcDlls(sdk_dir_val, sdk_ver_val, project_root)
    msvc_path = GetOrFindCached(existing_config, "msvc_tools_dir", FindMsvcPath)

    def _find_vcpkg_root() -> str:
        try:
            from vcpkg.FindVcpkg import FindVcpkg

            path_obj = FindVcpkg(allow_bootstrap=False)
            return NormalizePath(str(path_obj)) if path_obj else ""
        except ImportError:
            return ""

    def _find_ninja_path() -> str:
        try:
            from setup.SetupNinja import SetupNinja

            return NormalizePath(SetupNinja())
        except ImportError:
            return ""

    vcpkg_path = GetOrFindCached(existing_config, "vcpkg_root", _find_vcpkg_root)
    ninja_path = GetOrFindCached(existing_config, "ninja_path", _find_ninja_path)
    sys_includes = GetOrFindCached(existing_config, "system_include_dirs", FindSystemIncludeDirs)

    target_os = platform.system().lower()
    engine_config_data = {
        "target_platform": target_os,
        "target_arch": platform.machine().lower(),
        "llvm_path": llvm_path,
        "libclang_dll_path": libclang_dll_path,
        "windows_sdk_dir": sdk_dir_val,
        "windows_sdk_version": sdk_ver_val,
        "dxc_dll_path": found_dxc,
        "dxil_dll_path": found_dxil,
        "msvc_tools_dir": msvc_path,
        "vcpkg_root": vcpkg_path,
        "ninja_path": ninja_path,
        "system_include_dirs": sys_includes,
    }

    new_json_str = json.dumps(engine_config_data, indent=4)
    should_write = True
    if engine_config_file.exists():
        try:
            with open(engine_config_file, "r", encoding="utf-8") as f:
                if f.read() == new_json_str:
                    should_write = False
        except Exception:
            pass

    if should_write:
        with open(engine_config_file, "w", encoding="utf-8") as f:
            f.write(new_json_str)

    UpdateParserConfig(target_os, parser_config_file)

    if platform.system() == "Linux":
        try:
            from setup.SetupLinuxDevEnvironment import SetupLinuxDevEnvironment

            SetupLinuxDevEnvironment()
        except Exception as exc:
            print(f"[SetupEnvironment] SetupLinuxDevEnvironment skipped: {exc}")

    print(f"[SetupEnvironment] Resolved Config/engine_config.json for OS '{platform.system()}':")
    for key in (
        "target_platform",
        "target_arch",
        "llvm_path",
        "libclang_dll_path",
        "windows_sdk_dir",
        "windows_sdk_version",
        "dxc_dll_path",
        "dxil_dll_path",
        "msvc_tools_dir",
        "vcpkg_root",
        "ninja_path",
        "system_include_dirs",
    ):
        print(f"  - {key:22}: {engine_config_data[key]}")
    print(
        f"  - config_file          : {NormalizePath(str(engine_config_file))} "
        f"({'updated' if should_write else 'unchanged'})"
    )
    return engine_config_data


if __name__ == "__main__":
    EnsureScriptsOnPath()
    SetupEnvironment()
