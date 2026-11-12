#!/usr/bin/env python3
"""
Scripts/setup/PlatformSdk.py

웹 · Android 빌드 도구를 Tools/ 아래에 받는다(LLVM · Ninja 와 같은 자리, .gitignore 대상).
판과 주소는 Config/Environment/search_paths.defaults.json 이 정본이다.

  py -3 Scripts/setup/PlatformSdk.py web        # Tools/emsdk (emsdk install · activate)
  py -3 Scripts/setup/PlatformSdk.py android    # Tools/JDK + Tools/Android (명령줄 도구 → NDK · 플랫폼 · 빌드 도구)
  py -3 Scripts/setup/PlatformSdk.py all
  py -3 Scripts/setup/PlatformSdk.py check      # 프리셋(Web-Shipping · Android-Shipping)이 가리키는 Tools 경로가 이 설정의 판과 같은지만 본다

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

from common.ToolDownload import ensureCachedDownload, extractZipSafe, resolveToolsSubdir, toolsCacheDir  # noqa: E402
from common.ToolchainConfig import loadSearchPaths  # noqa: E402
from common.HostPlatform import isWindowsHost  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Git import runGitOrRaise  # noqa: E402


kPresetFile = Path(__file__).resolve().parents[2] / "CMakePresets.json"


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
            runGitOrRaise(["clone", "--depth", "1", "--branch", version, self.require("emsdk_git_url"), str(root)])
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
        # sdkmanager 는 Android CLI(`android sdk`)로 넘기는 껍데기가 됐고, Windows 배치 인자 분리가 `ndk;<판>` 의 `;` 를 끊어 "not found" 를 내고도 0 으로 끝났다.
        # 새 CLI 를 셸 없이 부르고(꾸러미 경로는 `/`), 끝나면 폴더를 대조한다 — 종료 코드만 믿지 않는다.
        androidCli = latest / "bin" / ("android.exe" if isWindowsHost() else "android")
        env = dict(os.environ, JAVA_HOME=str(javaHome))
        packages = {
            f"ndk/{self.require('android_ndk_version')}": root / "ndk" / self.require("android_ndk_version"),
            f"platforms/android-{self.require('android_platform')}": root / "platforms" / f"android-{self.require('android_platform')}",
            f"build-tools/{self.require('android_build_tools')}": root / "build-tools" / self.require("android_build_tools"),
            "platform-tools": root / "platform-tools",
        }
        subprocess.run([str(androidCli), f"--sdk={root}", "sdk", "install", *packages], check=True, env=env)
        missing = [name for name, folder in packages.items() if not folder.is_dir()]
        if missing:
            raise RuntimeError(f"[PlatformSdk] Android packages not installed after `android sdk install`: {', '.join(missing)}")
        print(f"[PlatformSdk] Android SDK: {root} (NDK {self.require('android_ndk_version')}, JDK {javaHome})")
        return root


    def checkPresets(self) -> None:
        """프리셋의 Tools 경로가 설정의 판 · 폴더와 같은지 봅니다. 다르면 ValueError 로 멈춘다(엉뚱한 NDK · emsdk 로 짓지 않게)."""
        listPreset = json.loads(kPresetFile.read_text(encoding="utf-8"))["configurePresets"]
        presets = {preset["name"]: preset for preset in listPreset}
        ndkHome = f"${{sourceDir}}/{self.require('android_sdk_tools_subdir')}/ndk/{self.require('android_ndk_version')}"
        emsdkHome = f"${{sourceDir}}/{self.require('emsdk_tools_subdir')}"
        listExpected = [
            ("Android-Shipping", "environment", "ANDROID_NDK_HOME", ndkHome),
            ("Android-Shipping", "cacheVariables", "VCPKG_CHAINLOAD_TOOLCHAIN_FILE", f"{ndkHome}/build/cmake/android.toolchain.cmake"),
            ("Web-Shipping", "environment", "EMSDK", emsdkHome),
            ("Web-Shipping", "cacheVariables", "VCPKG_CHAINLOAD_TOOLCHAIN_FILE", f"{emsdkHome}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"),
        ]
        listMismatch = []
        for presetName, section, key, expected in listExpected:
            actual = presets[presetName][section][key]
            if actual != expected:
                listMismatch.append(f"{presetName}.{section}.{key} = {actual} (expected {expected})")
        if listMismatch:
            raise ValueError("[PlatformSdk] CMakePresets.json does not match Config/Environment/search_paths: " + "; ".join(listMismatch))
        print("[PlatformSdk] presets match the configured emsdk · NDK")


def main() -> int:
    parser = argparse.ArgumentParser(description="웹 · Android 빌드 도구를 Tools/ 아래에 받는다")
    parser.add_argument("target", choices=["web", "android", "all", "check"])
    args = parser.parse_args()
    sdk = PlatformSdk()
    sdk.checkPresets()
    if args.target in ("web", "all"):
        sdk.installWeb()
    if args.target in ("android", "all"):
        sdk.installAndroid()
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
