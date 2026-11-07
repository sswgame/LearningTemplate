#!/usr/bin/env python3
"""
Scripts/setup/PlatformSdk.py

웹 · Android 빌드 도구를 Tools/ 아래에 받는다(LLVM · Ninja 와 같은 자리, .gitignore 대상).
판과 주소는 Config/Environment/search_paths.defaults.json 이 정본이다.

  py -3 Scripts/setup/PlatformSdk.py web        # Tools/emsdk (emsdk install · activate)
  py -3 Scripts/setup/PlatformSdk.py android    # Tools/JDK + Tools/Android (명령줄 도구 → NDK · 플랫폼 · 빌드 도구)
  py -3 Scripts/setup/PlatformSdk.py all

라이브러리(Dawn · Tint)는 여기가 아니라 ThirdParty/dawn 이 vcpkg 로 받는다.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.ToolDownload import ensureCachedDownload, extractZipSafe, resolveToolsSubdir, toolsCacheDir  # noqa: E402
from common.ToolchainConfig import loadSearchPaths  # noqa: E402
from common.HostPlatform import isWindowsHost  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402


class PlatformSdk:
    """설정 한 벌(search_paths)을 읽어 웹 · Android 도구를 받는다. 값이 없으면 KeyError 로 멈춘다(폴백 없음)."""

    def __init__(self) -> None:
        self._search = loadSearchPaths()
        self._hostKey = "windows" if isWindowsHost() else "linux"

    def require(self, key: str) -> str:
        value = self._search.get(key)
        if not value:
            raise KeyError(f"[Config Error] Missing required key '{key}' in search_paths config")
        if isinstance(value, dict):
            value = value.get(self._hostKey)
            if not value:
                raise KeyError(f"[Config Error] '{key}' has no '{self._hostKey}' entry")
        return str(value)

    def installWeb(self) -> Path:
        root = resolveToolsSubdir("emsdk_tools_subdir", self._search)
        version = self.require("emsdk_version")
        if not (root / ".git").is_dir():
            subprocess.run(["git", "clone", "--depth", "1", "--branch", version, self.require("emsdk_git_url"), str(root)], check=True)
        tool = str(root / ("emsdk.bat" if isWindowsHost() else "emsdk"))
        subprocess.run([tool, "install", version], check=True, cwd=root)
        subprocess.run([tool, "activate", version], check=True, cwd=root)
        print(f"[PlatformSdk] emsdk {version}: {root}")
        return root

    def installJdk(self) -> Path:
        root = resolveToolsSubdir("jdk_tools_subdir", self._search)
        if any(root.glob("*/bin/java*")):
            return next(root.glob("*/bin/java*")).parents[1]
        url = self.require("jdk_download_urls")
        archive = ensureCachedDownload(url, toolsCacheDir() / f"jdk-{self.require('jdk_version').replace('+', '_')}-{self._hostKey}{'.zip' if isWindowsHost() else '.tar.gz'}", label="JDK")
        root.mkdir(parents=True, exist_ok=True)
        if isWindowsHost():
            extractZipSafe(archive, root)
        else:
            shutil.unpack_archive(str(archive), str(root))
        return next(root.glob("*/bin/java*")).parents[1]

    def installAndroid(self) -> Path:
        javaHome = self.installJdk()
        root = resolveToolsSubdir("android_sdk_tools_subdir", self._search)
        latest = root / "cmdline-tools" / "latest"
        if not latest.is_dir():
            archive = ensureCachedDownload(self.require("android_cmdline_tools_urls"), toolsCacheDir() / f"android-cmdline-tools-{self._hostKey}.zip", label="Android")
            staging = root / "cmdline-tools" / "_staging"
            shutil.rmtree(staging, ignore_errors=True)
            extractZipSafe(archive, staging)
            (staging / "cmdline-tools").rename(latest)
            shutil.rmtree(staging, ignore_errors=True)
        sdkManager = latest / "bin" / ("sdkmanager.bat" if isWindowsHost() else "sdkmanager")
        env = dict(os.environ, JAVA_HOME=str(javaHome))
        packages = [f"ndk;{self.require('android_ndk_version')}", f"platforms;android-{self.require('android_platform')}", f"build-tools;{self.require('android_build_tools')}", "platform-tools"]
        subprocess.run([str(sdkManager), f"--sdk_root={root}", "--licenses"], input="y\n" * 20, text=True, check=True, env=env, shell=isWindowsHost())
        subprocess.run([str(sdkManager), f"--sdk_root={root}", *packages], check=True, env=env, shell=isWindowsHost())
        print(f"[PlatformSdk] Android SDK: {root} (NDK {self.require('android_ndk_version')}, JDK {javaHome})")
        return root


def main() -> int:
    parser = argparse.ArgumentParser(description="웹 · Android 빌드 도구를 Tools/ 아래에 받는다")
    parser.add_argument("target", choices=["web", "android", "all"])
    args = parser.parse_args()
    sdk = PlatformSdk()
    if args.target in ("web", "all"):
        sdk.installWeb()
    if args.target in ("android", "all"):
        sdk.installAndroid()
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
