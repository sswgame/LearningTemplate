#!/usr/bin/env python3
"""
Scripts/setup/platform_sdk.py

웹 · Android 빌드 도구를 Tools/ 아래에 받는다(LLVM · Ninja 와 같은 자리, .gitignore 대상).
판과 주소는 Config/Environment/search_paths.defaults.json 이 정본이다.

  py -3 Scripts/setup/platform_sdk.py web        # Tools/emsdk (emsdk install · activate)
  py -3 Scripts/setup/platform_sdk.py android    # Tools/JDK + Tools/Android (명령줄 도구 → NDK · 플랫폼 · 빌드 도구)
  py -3 Scripts/setup/platform_sdk.py all
  py -3 Scripts/setup/platform_sdk.py check      # 프리셋(Web-Shipping · Android-Shipping)이 가리키는 Tools 경로가 이 설정의 판과 같은지만 본다

라이브러리(Dawn · Tint)는 여기가 아니라 ThirdParty/dawn 이 vcpkg 로 받는다.
프리셋은 JSON 이라 이 설정을 읽지 못한다 — NDK 판이 경로에 한 번 더 적히므로 `check`(설치 끝에도 돈다)가 둘이 같은지 본다.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.tool_download import ensure_cached_download, extract_zip_safe, resolve_tools_subdir, tools_cache_dir  # noqa: E402
from common.toolchain_config import load_search_paths  # noqa: E402
from common.host_platform import is_windows_host  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.git import run_git_or_raise  # noqa: E402


PRESET_FILE = Path(__file__).resolve().parents[2] / "CMakePresets.json"


class PlatformSdk:
    """설정 한 벌(search_paths)을 읽어 웹 · Android 도구를 받는다. 값이 없으면 KeyError 로 멈춘다(폴백 없음)."""

    def __init__(self) -> None:
        self._search = load_search_paths()
        self._host_key = "windows" if is_windows_host() else "linux"

    def require(self, key: str) -> str:
        value = self._search.get(key)
        if not value:
            raise KeyError(f"[Config Error] Missing required key '{key}' in search_paths config")
        if isinstance(value, dict):
            value = value.get(self._host_key)
            if not value:
                raise KeyError(f"[Config Error] '{key}' has no '{self._host_key}' entry")
        return str(value)

    def install_web(self) -> Path:
        root = resolve_tools_subdir("emsdk_tools_subdir", self._search)
        version = self.require("emsdk_version")
        if not (root / ".git").is_dir():
            run_git_or_raise(["clone", "--depth", "1", "--branch", version, self.require("emsdk_git_url"), str(root)])
        tool = str(root / ("emsdk.bat" if is_windows_host() else "emsdk"))
        subprocess.run([tool, "install", version], check=True, cwd=root)
        subprocess.run([tool, "activate", version], check=True, cwd=root)
        print(f"[PlatformSdk] emsdk {version}: {root}")
        return root

    def install_jdk(self) -> Path:
        root = resolve_tools_subdir("jdk_tools_subdir", self._search)
        if any(root.glob("*/bin/java*")):
            return next(root.glob("*/bin/java*")).parents[1]
        url = self.require("jdk_download_urls")
        archive = ensure_cached_download(url, tools_cache_dir() / f"jdk-{self.require('jdk_version').replace('+', '_')}-{self._host_key}{'.zip' if is_windows_host() else '.tar.gz'}", label="JDK")
        root.mkdir(parents=True, exist_ok=True)
        if is_windows_host():
            extract_zip_safe(archive, root)
        else:
            shutil.unpack_archive(str(archive), str(root))
        return next(root.glob("*/bin/java*")).parents[1]

    def install_android(self) -> Path:
        java_home = self.install_jdk()
        root = resolve_tools_subdir("android_sdk_tools_subdir", self._search)
        latest = root / "cmdline-tools" / "latest"
        if not latest.is_dir():
            archive = ensure_cached_download(self.require("android_cmdline_tools_urls"), tools_cache_dir() / f"android-cmdline-tools-{self._host_key}.zip", label="Android")
            staging = root / "cmdline-tools" / "_staging"
            shutil.rmtree(staging, ignore_errors=True)
            extract_zip_safe(archive, staging)
            (staging / "cmdline-tools").rename(latest)
            shutil.rmtree(staging, ignore_errors=True)
        # sdkmanager 는 Android CLI(`android sdk`)로 넘기는 껍데기가 됐고, Windows 배치 인자 분리가 `ndk;<판>` 의 `;` 를 끊어 "not found" 를 내고도 0 으로 끝났다.
        # 새 CLI 를 셸 없이 부르고(꾸러미 경로는 `/`), 끝나면 폴더를 대조한다 — 종료 코드만 믿지 않는다.
        android_cli = latest / "bin" / ("android.exe" if is_windows_host() else "android")
        env = dict(os.environ, JAVA_HOME=str(java_home))
        packages = {
            f"ndk/{self.require('android_ndk_version')}": root / "ndk" / self.require("android_ndk_version"),
            f"platforms/android-{self.require('android_platform')}": root / "platforms" / f"android-{self.require('android_platform')}",
            f"build-tools/{self.require('android_build_tools')}": root / "build-tools" / self.require("android_build_tools"),
            "platform-tools": root / "platform-tools",
        }
        subprocess.run([str(android_cli), f"--sdk={root}", "sdk", "install", *packages], check=True, env=env)
        missing = [name for name, folder in packages.items() if not folder.is_dir()]
        if missing:
            raise RuntimeError(f"[PlatformSdk] Android packages not installed after `android sdk install`: {', '.join(missing)}")
        print(f"[PlatformSdk] Android SDK: {root} (NDK {self.require('android_ndk_version')}, JDK {java_home})")
        return root


    def check_presets(self) -> None:
        """프리셋의 Tools 경로가 설정의 판 · 폴더와 같은지 봅니다. 다르면 ValueError 로 멈춘다(엉뚱한 NDK · emsdk 로 짓지 않게)."""
        list_preset = json.loads(PRESET_FILE.read_text(encoding="utf-8"))["configurePresets"]
        presets = {preset["name"]: preset for preset in list_preset}
        ndk_home = f"${{sourceDir}}/{self.require('android_sdk_tools_subdir')}/ndk/{self.require('android_ndk_version')}"
        emsdk_home = f"${{sourceDir}}/{self.require('emsdk_tools_subdir')}"
        list_expected = [
            ("Android-Shipping", "environment", "ANDROID_NDK_HOME", ndk_home),
            ("Android-Shipping", "cacheVariables", "VCPKG_CHAINLOAD_TOOLCHAIN_FILE", f"{ndk_home}/build/cmake/android.toolchain.cmake"),
            ("Web-Shipping", "environment", "EMSDK", emsdk_home),
            ("Web-Shipping", "cacheVariables", "VCPKG_CHAINLOAD_TOOLCHAIN_FILE", f"{emsdk_home}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"),
        ]
        list_mismatch = []
        for preset_name, section, key, expected in list_expected:
            actual = presets[preset_name][section][key]
            if actual != expected:
                list_mismatch.append(f"{preset_name}.{section}.{key} = {actual} (expected {expected})")
        if list_mismatch:
            raise ValueError("[PlatformSdk] CMakePresets.json does not match Config/Environment/search_paths: " + "; ".join(list_mismatch))
        print("[PlatformSdk] presets match the configured emsdk · NDK")


def main() -> int:
    parser = argparse.ArgumentParser(description="웹 · Android 빌드 도구를 Tools/ 아래에 받는다")
    parser.add_argument("target", choices=["web", "android", "all", "check"])
    args = parser.parse_args()
    sdk = PlatformSdk()
    sdk.check_presets()
    if args.target in ("web", "all"):
        sdk.install_web()
    if args.target in ("android", "all"):
        sdk.install_android()
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
