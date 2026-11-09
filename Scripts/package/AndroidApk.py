#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file AndroidApk.py
@brief Android-Shipping 빌드 산출물(`lib<이름>.so` + 쿠킹한 팩)을 서명한 디버그 APK 하나로 묶는다.

Java 코드가 없는 NativeActivity 앱이라 Gradle · d8 을 쓰지 않는다 — 매니페스트를 이진 XML 로 바꾸고 자산을 담는 aapt2, 정렬하는 zipalign,
서명하는 apksigner(모두 `Tools/Android/build-tools/<판>`)와 디버그 키를 만드는 keytool(`Tools/JDK`) 넷이다. NDK 샘플의 native-activity 와 같은 구성이고(기억),
언리얼 · Godot 은 Gradle 프로젝트를 만들지만 그것은 Java 쪽(결제 · 알림 플러그인)을 붙이기 위해서다 — 붙일 Java 가 생기면 Gradle 로 옮긴다.

| 단계 | 도구 | 하는 일 |
| --- | --- | --- |
| manifest | (이 스크립트) | `cmake/Modules/Platform/AndroidManifest.xml` 틀을 `Config/Environment/android_package.json` 값으로 채운다 |
| link | aapt2 | 이진 매니페스트 + `assets/Packs/*.pack`(압축하지 않는다 — AAsset 으로 바로 읽는다) |
| native | (zipfile) | `lib/<ABI>/lib<이름>.so` 를 압축하지 않고 넣는다(`extractNativeLibs=false` — 설치가 풀지 않고 APK 에서 바로 연다) |
| align | zipalign | 압축하지 않은 항목 4 바이트, .so 는 페이지(`page_size_kb`, Android 15 의 16KB 페이지 기기) 경계 |
| sign | keytool · apksigner | 디버그 키(`debug_keystore`, build/ 아래 — 저장소에 두지 않는다)로 서명하고 검증 |

판 · 경로의 정본: 대상 API 와 build-tools 판은 `Config/Environment/search_paths.defaults.json`(`android_platform` · `android_build_tools`),
최소 API · ABI 는 `CMakePresets.json` 의 `Android-Shipping`(`ANDROID_PLATFORM` · `ANDROID_ABI`) — 같은 값을 두 번 적지 않는다. 값이 없으면 멈춘다(폴백 없음).

사용법:
    py -3 -m Scripts android-apk --game Empty
    py -3 -m Scripts android-apk --game Empty --build-dir build/Android-Shipping --packs-dir build/Android-Shipping/Bin/Packs
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.HostPlatform import isWindowsHost  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ToolDownload import resolveToolsSubdir  # noqa: E402
from common.ToolchainConfig import ConfigError, loadSearchPaths, requireConfig  # noqa: E402
from package.Pack import PackageError, kPackFolderName, selectStagedPacks  # noqa: E402

#: 패키징 값 파일과 매니페스트 틀(저장소 기준).
kPackageConfigPath = Path("Config") / "Environment" / "android_package.json"
kManifestTemplatePath = Path("cmake") / "Modules" / "Platform" / "AndroidManifest.xml"
kPresetFileName = "CMakePresets.json"
kPresetName = "Android-Shipping"
#: 팩 확장자 — aapt2 가 압축하지 않게 한다(AAsset 이 오프셋으로 바로 읽는다).
kPackExtension = "pack"
#: 매니페스트 `android:screenOrientation` 이 받는 값(Android 문서의 열거, 기억) — 오타가 설치 뒤 조용히 기본값이 되지 않게 패키징에서 막는다.
kSetScreenOrientation: frozenset[str] = frozenset({
    "unspecified", "behind", "landscape", "portrait", "reverseLandscape", "reversePortrait", "sensorLandscape", "sensorPortrait",
    "userLandscape", "userPortrait", "sensor", "fullSensor", "nosensor", "user", "fullUser", "locked",
})
#: 디버그 키의 고정 값 — Android SDK 의 debug.keystore 와 같은 이름 · 암호(기억). 배포 서명이 아니다.
kDebugKeyAlias = "androiddebugkey"
kDebugKeyPassword = "android"
kDebugKeyDistinguishedName = "CN=Android Debug,O=Android,C=US"
kDebugKeyValidityDays = 10000
#: 매니페스트 틀의 자리 `${이름}`.
kPlaceholderPattern = re.compile(r"\$\{([A-Za-z]+)\}")


class ApkError(Exception):
    """단계 하나가 실패했다. 메시지가 `[android-apk] FAILED` 줄의 이유다."""


@dataclass(frozen=True)
class ApkSettings:
    """한 게임 APK 의 값 — 설정 파일(게임 덮기 포함)과 프리셋에서 모은다."""

    applicationId: str
    label: str
    libName: str
    versionCode: int
    versionName: str
    screenOrientation: str
    vulkanVersion: str
    pageSizeKb: int
    debugKeystore: Path
    minSdk: int
    targetSdk: int
    abi: str


def requireKey(config: dict[str, Any], key: str, source: str) -> Any:
    """@p config 의 @p key 입니다. 없거나 비면 ApkError(폴백 없음)."""
    value = config.get(key)
    if value is None or value == "":
        raise ApkError(f"{source}: missing required key '{key}'")
    return value


def readPresetAndroid(listPreset: list[dict[str, Any]]) -> tuple[int, str]:
    """프리셋 `Android-Shipping` 의 최소 API(`ANDROID_PLATFORM` = `android-<N>`)와 ABI 입니다."""
    preset = next((entry for entry in listPreset if entry.get("name") == kPresetName), None)
    if preset is None:
        raise ApkError(f"{kPresetFileName}: no configure preset '{kPresetName}'")
    cacheVariables = preset.get("cacheVariables", {})
    platform = str(requireKey(cacheVariables, "ANDROID_PLATFORM", f"{kPresetName}.cacheVariables"))
    match = re.fullmatch(r"android-(\d+)", platform)
    if match is None:
        raise ApkError(f"{kPresetName}.cacheVariables.ANDROID_PLATFORM is '{platform}' - expected android-<api level>")
    return int(match.group(1)), str(requireKey(cacheVariables, "ANDROID_ABI", f"{kPresetName}.cacheVariables"))


def resolveSettings(config: dict[str, Any], game: str, minSdk: int, targetSdk: int, abi: str, projectRoot: Path) -> ApkSettings:
    """설정 @p config 에 게임 @p game 의 덮기(`games.<게임>`)를 얹은 값입니다. 틀린 방향 · 판 순서는 ApkError."""
    games = config.get("games", {})
    if not isinstance(games, dict):
        raise ApkError(f"{kPackageConfigPath}: 'games' must be an object")
    merged = dict(config)
    merged.update(games.get(game, {}))
    source = f"{kPackageConfigPath} (game {game})"
    orientation = str(requireKey(merged, "screen_orientation", source))
    if orientation not in kSetScreenOrientation:
        raise ApkError(f"{source}: screen_orientation '{orientation}' is not an Android screenOrientation value")
    if targetSdk < minSdk:
        raise ApkError(f"target API {targetSdk} (search_paths android_platform) is below the minimum API {minSdk} ({kPresetName} ANDROID_PLATFORM)")
    applicationId = str(requireKey(merged, "application_id_prefix", source)) + re.sub(r"[^a-z0-9_]", "", game.lower())
    if re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+", applicationId) is None:
        raise ApkError(f"{source}: application id '{applicationId}' is not a valid Java package name")
    return ApkSettings(
        applicationId=applicationId,
        label=str(requireKey(merged, "label", source)),
        libName=str(requireKey(merged, "lib_name", source)),
        versionCode=int(requireKey(merged, "version_code", source)),
        versionName=str(requireKey(merged, "version_name", source)),
        screenOrientation=orientation,
        vulkanVersion=str(requireKey(merged, "vulkan_version", source)),
        pageSizeKb=int(requireKey(merged, "page_size_kb", source)),
        debugKeystore=projectRoot / str(requireKey(merged, "debug_keystore", source)),
        minSdk=minSdk,
        targetSdk=targetSdk,
        abi=abi,
    )


def renderManifest(template: str, settings: ApkSettings) -> str:
    """매니페스트 틀의 `${이름}` 자리를 채운 글입니다. 모르는 자리가 남으면 ApkError(빈 값으로 넘기지 않는다)."""
    mapValue = {
        "applicationId": settings.applicationId,
        "versionCode": str(settings.versionCode),
        "versionName": settings.versionName,
        "vulkanVersion": settings.vulkanVersion,
        "label": settings.label,
        "screenOrientation": settings.screenOrientation,
        "libName": settings.libName,
    }
    listUnknown = sorted({name for name in kPlaceholderPattern.findall(template) if name not in mapValue})
    if listUnknown:
        raise ApkError(f"{kManifestTemplatePath}: unknown placeholders {', '.join(listUnknown)}")
    return kPlaceholderPattern.sub(lambda match: mapValue[match.group(1)], template)


class AndroidTools:
    """build-tools · 플랫폼 · JDK 의 실행 파일입니다. 없으면 ApkError(설치 `py -3 Scripts/setup/PlatformSdk.py android`)."""

    def __init__(self, search: dict[str, Any]) -> None:
        sdkRoot = resolveToolsSubdir("android_sdk_tools_subdir", search)
        buildToolsVersion = str(search.get("android_build_tools") or "")
        platform = str(search.get("android_platform") or "")
        if not buildToolsVersion or not platform:
            raise ApkError("search_paths: android_build_tools and android_platform are required")
        buildTools = sdkRoot / "build-tools" / buildToolsVersion
        self.targetSdk = int(platform)
        self.androidJar = sdkRoot / "platforms" / f"android-{platform}" / "android.jar"
        self.aapt2 = buildTools / ("aapt2.exe" if isWindowsHost() else "aapt2")
        self.zipalign = buildTools / ("zipalign.exe" if isWindowsHost() else "zipalign")
        self.apksigner = buildTools / ("apksigner.bat" if isWindowsHost() else "apksigner")
        listJava = sorted(resolveToolsSubdir("jdk_tools_subdir", search).glob("*/bin/java*"))
        if not listJava:
            raise ApkError("no JDK under Tools/JDK - install with py -3 Scripts/setup/PlatformSdk.py android")
        self.javaHome = listJava[0].parents[1]
        self.keytool = self.javaHome / "bin" / ("keytool.exe" if isWindowsHost() else "keytool")
        listMissing = [str(path) for path in (self.androidJar, self.aapt2, self.zipalign, self.apksigner, self.keytool) if not path.is_file()]
        if listMissing:
            raise ApkError("missing Android tools (py -3 Scripts/setup/PlatformSdk.py android): " + ", ".join(listMissing))

    def run(self, step: str, command: list[str | Path]) -> None:
        """@p command 를 JDK 를 잡은 환경으로 돌립니다(apksigner 는 java 를 찾는다). 0 이 아니면 ApkError."""
        environment = dict(os.environ, JAVA_HOME=str(self.javaHome))
        environment["PATH"] = str(self.javaHome / "bin") + os.pathsep + environment.get("PATH", "")
        completed = subprocess.run([str(part) for part in command], env=environment, capture_output=True, encoding="utf-8", errors="replace", check=False)
        if completed.returncode != 0:
            raise ApkError(f"{step}: {Path(str(command[0])).name} exited {completed.returncode}: {(completed.stderr or completed.stdout).strip()}")


def ensureDebugKeystore(tools: AndroidTools, keystore: Path) -> None:
    """디버그 키가 없으면 만듭니다(build/ 아래 — 저장소 밖이다). 있으면 그대로 씁니다."""
    if keystore.is_file():
        return
    keystore.parent.mkdir(parents=True, exist_ok=True)
    tools.run("keystore", [tools.keytool, "-genkeypair", "-keystore", keystore, "-storepass", kDebugKeyPassword, "-alias", kDebugKeyAlias,
                           "-keypass", kDebugKeyPassword, "-keyalg", "RSA", "-keysize", "2048", "-validity", str(kDebugKeyValidityDays),
                           "-dname", kDebugKeyDistinguishedName])


def addNativeLibrary(apkPath: Path, library: Path, abi: str) -> None:
    """@p library 를 `lib/<ABI>/` 에 압축하지 않고 더합니다(`extractNativeLibs=false` 는 저장 항목만 바로 연다)."""
    with zipfile.ZipFile(apkPath, "a") as archive:
        archive.write(library, f"lib/{abi}/{library.name}", compress_type=zipfile.ZIP_STORED)


def makeApk(game: str, buildDir: Path, packsDir: Path, projectRoot: Path) -> Path:
    """게임 @p game 의 서명한 디버그 APK 를 `<buildDir>/Apk/<게임>-debug.apk` 에 만들고 그 경로를 돌려줍니다."""
    search = loadSearchPaths()
    tools = AndroidTools(search)
    listPreset = json.loads((projectRoot / kPresetFileName).read_text(encoding="utf-8"))["configurePresets"]
    minSdk, abi = readPresetAndroid(listPreset)
    try:
        config = requireConfig(projectRoot / kPackageConfigPath, kPackageConfigPath.name)
    except ConfigError as error:
        raise ApkError(str(error)) from error
    settings = resolveSettings(config, game, minSdk, tools.targetSdk, abi, projectRoot)

    library = buildDir / "Lib" / f"lib{settings.libName}.so"  # 공유 라이브러리는 CMAKE_LIBRARY_OUTPUT_DIRECTORY(cmake/Engine/BuildLayout.cmake 의 Lib)로 간다
    if not library.is_file():
        raise ApkError(f"{library} not found - build the {kPresetName} preset first")
    try:
        listPack = selectStagedPacks(projectRoot, packsDir, game)
    except PackageError as error:
        raise ApkError(str(error)) from error

    workDir = buildDir / "Apk" / game
    shutil.rmtree(workDir, ignore_errors=True)
    assetPackDir = workDir / "assets" / kPackFolderName
    assetPackDir.mkdir(parents=True)
    for pack in listPack:
        shutil.copy2(pack, assetPackDir / pack.name)
    manifest = workDir / "AndroidManifest.xml"
    manifest.write_text(renderManifest((projectRoot / kManifestTemplatePath).read_text(encoding="utf-8"), settings), encoding="utf-8")

    unaligned = workDir / "unaligned.apk"
    aligned = workDir / "aligned.apk"
    signed = buildDir / "Apk" / f"{game}-debug.apk"
    print(f"[android-apk] link {settings.applicationId} (API {settings.minSdk}..{settings.targetSdk}, {settings.screenOrientation})")
    tools.run("link", [tools.aapt2, "link", "-o", unaligned, "-I", tools.androidJar, "--manifest", manifest, "--min-sdk-version", str(settings.minSdk),
                       "--target-sdk-version", str(settings.targetSdk), "--debug-mode", "-A", workDir / "assets", "-0", kPackExtension])
    addNativeLibrary(unaligned, library, settings.abi)
    print(f"[android-apk] align ({settings.pageSizeKb} KB pages)")
    tools.run("align", [tools.zipalign, "-f", "-P", str(settings.pageSizeKb), "4", unaligned, aligned])
    ensureDebugKeystore(tools, settings.debugKeystore)
    print("[android-apk] sign (debug key)")
    tools.run("sign", [tools.apksigner, "sign", "--ks", settings.debugKeystore, "--ks-pass", f"pass:{kDebugKeyPassword}", "--key-pass", f"pass:{kDebugKeyPassword}",
                       "--ks-key-alias", kDebugKeyAlias, "--out", signed, aligned])
    tools.run("verify", [tools.apksigner, "verify", signed])
    return signed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts android-apk", description="Android-Shipping 빌드를 서명한 디버그 APK 로 묶는다")
    parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json — 담을 팩을 고른다)")
    parser.add_argument("--build-dir", default=f"build/{kPresetName}", help="Android-Shipping 빌드 폴더(저장소 기준, 기본 build/Android-Shipping)")
    parser.add_argument("--packs-dir", default=None, help="Android 대상으로 쿠킹한 팩 폴더(기본 <build-dir>/Bin/Packs)")
    args = parser.parse_args(argv)
    projectRoot = getProjectRoot()
    buildDir = (projectRoot / args.build_dir).resolve()
    packsDir = (projectRoot / args.packs_dir).resolve() if args.packs_dir else buildDir / "Bin" / kPackFolderName
    try:
        apk = makeApk(args.game, buildDir, packsDir, projectRoot)
    except ApkError as error:
        print(f"[android-apk] FAILED {error}")
        return ExitCode.Violation
    print(f"[android-apk] done {apk} {apk.stat().st_size}")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
