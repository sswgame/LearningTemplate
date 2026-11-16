#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file android_apk.py
@brief Android-Shipping 빌드 산출물(`lib<이름>.so` + 쿠킹한 팩)을 서명한 디버그 APK 하나로 묶는다.

Java 코드가 없는 NativeActivity 앱이라 Gradle · d8 을 쓰지 않는다 — 매니페스트를 이진 XML 로 바꾸고 자산을 담는 aapt2, 정렬하는 zipalign,
서명하는 apksigner(모두 `Tools/Android/build-tools/<판>`)와 디버그 키를 만드는 keytool(`Tools/JDK`) 넷이다. NDK 샘플의 native-activity 와 같은 구성이고(기억),
언리얼 · Godot 은 Gradle 프로젝트를 만들지만 그것은 Java 쪽(결제 · 알림 플러그인)을 붙이기 위해서다 — 붙일 Java 가 생기면 Gradle 로 옮긴다.

| 단계 | 도구 | 하는 일 |
| --- | --- | --- |
| manifest | (이 스크립트) | `cmake/Templates/AndroidManifest.xml` 틀을 `Config/Environment/android_package.json` 값으로 채운다 |
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
from common.exit_code import ExitCode  # noqa: E402
from common.host_platform import is_windows_host  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from common.tool_download import resolve_tools_subdir  # noqa: E402
from common.toolchain_config import ConfigError, load_search_paths, require_config  # noqa: E402
from package.pack import PackageError, PACK_FOLDER_NAME, select_staged_packs  # noqa: E402

#: 패키징 값 파일과 매니페스트 틀(저장소 기준).
PACKAGE_CONFIG_PATH = Path("Config") / "Environment" / "android_package.json"
MANIFEST_TEMPLATE_PATH = Path("cmake/Templates/AndroidManifest.xml")   # 한 글로 적는다 — 조각으로 쪼개면 경로 옮기기(move-source)가 찾지 못한다
PRESET_FILE_NAME = "CMakePresets.json"
PRESET_NAME = "Android-Shipping"
#: 팩 확장자 — aapt2 가 압축하지 않게 한다(AAsset 이 오프셋으로 바로 읽는다).
PACK_EXTENSION = "pack"
#: 매니페스트 `android:screen_orientation` 이 받는 값(Android 문서의 열거, 기억) — 오타가 설치 뒤 조용히 기본값이 되지 않게 패키징에서 막는다.
SET_SCREEN_ORIENTATION: frozenset[str] = frozenset({
    "unspecified", "behind", "landscape", "portrait", "reverseLandscape", "reversePortrait", "sensorLandscape", "sensorPortrait",
    "userLandscape", "userPortrait", "sensor", "fullSensor", "nosensor", "user", "fullUser", "locked",
})
#: 디버그 키의 고정 값 — Android SDK 의 debug.keystore 와 같은 이름 · 암호(기억). 배포 서명이 아니다.
DEBUG_KEY_ALIAS = "androiddebugkey"
DEBUG_KEY_PASSWORD = "android"
DEBUG_KEY_DISTINGUISHED_NAME = "CN=Android Debug,O=Android,C=US"
DEBUG_KEY_VALIDITY_DAYS = 10000
#: 매니페스트 틀의 자리 `${이름}`.
PLACEHOLDER_PATTERN = re.compile(r"\$\{([A-Za-z]+)\}")


class ApkError(Exception):
    """단계 하나가 실패했다. 메시지가 `[android-apk] FAILED` 줄의 이유다."""


@dataclass(frozen=True)
class ApkSettings:
    """한 게임 APK 의 값 — 설정 파일(게임 덮기 포함)과 프리셋에서 모은다."""

    application_id: str
    label: str
    lib_name: str
    version_code: int
    version_name: str
    screen_orientation: str
    vulkan_version: str
    page_size_kb: int
    debug_keystore: Path
    min_sdk: int
    target_sdk: int
    abi: str


def require_key(config: dict[str, Any], key: str, source: str) -> Any:
    """@p config 의 @p key 입니다. 없거나 비면 ApkError(폴백 없음)."""
    value = config.get(key)
    if value is None or value == "":
        raise ApkError(f"{source}: missing required key '{key}'")
    return value


def read_preset_android(list_preset: list[dict[str, Any]]) -> tuple[int, str]:
    """프리셋 `Android-Shipping` 의 최소 API(`ANDROID_PLATFORM` = `android-<N>`)와 ABI 입니다."""
    preset = next((entry for entry in list_preset if entry.get("name") == PRESET_NAME), None)
    if preset is None:
        raise ApkError(f"{PRESET_FILE_NAME}: no configure preset '{PRESET_NAME}'")
    cache_variables = preset.get("cacheVariables", {})
    platform = str(require_key(cache_variables, "ANDROID_PLATFORM", f"{PRESET_NAME}.cacheVariables"))
    match = re.fullmatch(r"android-(\d+)", platform)
    if match is None:
        raise ApkError(f"{PRESET_NAME}.cacheVariables.ANDROID_PLATFORM is '{platform}' - expected android-<api level>")
    return int(match.group(1)), str(require_key(cache_variables, "ANDROID_ABI", f"{PRESET_NAME}.cacheVariables"))


def resolve_settings(config: dict[str, Any], game: str, min_sdk: int, target_sdk: int, abi: str, project_root: Path) -> ApkSettings:
    """설정 @p config 에 게임 @p game 의 덮기(`games.<게임>`)를 얹은 값입니다. 틀린 방향 · 판 순서는 ApkError."""
    games = config.get("games", {})
    if not isinstance(games, dict):
        raise ApkError(f"{PACKAGE_CONFIG_PATH}: 'games' must be an object")
    merged = dict(config)
    merged.update(games.get(game, {}))
    source = f"{PACKAGE_CONFIG_PATH} (game {game})"
    orientation = str(require_key(merged, "screen_orientation", source))
    if orientation not in SET_SCREEN_ORIENTATION:
        raise ApkError(f"{source}: screen_orientation '{orientation}' is not an Android screenOrientation value")
    if target_sdk < min_sdk:
        raise ApkError(f"target API {target_sdk} (search_paths android_platform) is below the minimum API {min_sdk} ({PRESET_NAME} ANDROID_PLATFORM)")
    application_id = str(require_key(merged, "application_id_prefix", source)) + re.sub(r"[^a-z0-9_]", "", game.lower())
    if re.fullmatch(r"[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+", application_id) is None:
        raise ApkError(f"{source}: application id '{application_id}' is not a valid Java package name")
    return ApkSettings(
        application_id=application_id,
        label=str(require_key(merged, "label", source)),
        lib_name=str(require_key(merged, "lib_name", source)),
        version_code=int(require_key(merged, "version_code", source)),
        version_name=str(require_key(merged, "version_name", source)),
        screen_orientation=orientation,
        vulkan_version=str(require_key(merged, "vulkan_version", source)),
        page_size_kb=int(require_key(merged, "page_size_kb", source)),
        debug_keystore=project_root / str(require_key(merged, "debug_keystore", source)),
        min_sdk=min_sdk,
        target_sdk=target_sdk,
        abi=abi,
    )


def render_manifest(template: str, settings: ApkSettings) -> str:
    """매니페스트 틀의 `${이름}` 자리를 채운 글입니다. 모르는 자리가 남으면 ApkError(빈 값으로 넘기지 않는다)."""
    map_value = {
        "applicationId": settings.application_id,
        "versionCode": str(settings.version_code),
        "versionName": settings.version_name,
        "vulkanVersion": settings.vulkan_version,
        "label": settings.label,
        "screenOrientation": settings.screen_orientation,
        "libName": settings.lib_name,
    }
    list_unknown = sorted({name for name in PLACEHOLDER_PATTERN.findall(template) if name not in map_value})
    if list_unknown:
        raise ApkError(f"{MANIFEST_TEMPLATE_PATH}: unknown placeholders {', '.join(list_unknown)}")
    return PLACEHOLDER_PATTERN.sub(lambda match: map_value[match.group(1)], template)


class AndroidTools:
    """build-tools · 플랫폼 · JDK 의 실행 파일입니다. 없으면 ApkError(설치 `py -3 Scripts/setup/platform_sdk.py android`)."""

    def __init__(self, search: dict[str, Any]) -> None:
        sdk_root = resolve_tools_subdir("android_sdk_tools_subdir", search)
        build_tools_version = str(search.get("android_build_tools") or "")
        platform = str(search.get("android_platform") or "")
        if not build_tools_version or not platform:
            raise ApkError("search_paths: android_build_tools and android_platform are required")
        build_tools = sdk_root / "build-tools" / build_tools_version
        self.target_sdk = int(platform)
        self.android_jar = sdk_root / "platforms" / f"android-{platform}" / "android.jar"
        self.aapt2 = build_tools / ("aapt2.exe" if is_windows_host() else "aapt2")
        self.zipalign = build_tools / ("zipalign.exe" if is_windows_host() else "zipalign")
        self.apksigner = build_tools / ("apksigner.bat" if is_windows_host() else "apksigner")
        list_java = sorted(resolve_tools_subdir("jdk_tools_subdir", search).glob("*/bin/java*"))
        if not list_java:
            raise ApkError("no JDK under Tools/JDK - install with py -3 Scripts/setup/platform_sdk.py android")
        self.java_home = list_java[0].parents[1]
        self.keytool = self.java_home / "bin" / ("keytool.exe" if is_windows_host() else "keytool")
        list_missing = [str(path) for path in (self.android_jar, self.aapt2, self.zipalign, self.apksigner, self.keytool) if not path.is_file()]
        if list_missing:
            raise ApkError("missing Android tools (py -3 Scripts/setup/platform_sdk.py android): " + ", ".join(list_missing))

    def run(self, step: str, command: list[str | Path]) -> None:
        """@p command 를 JDK 를 잡은 환경으로 돌립니다(apksigner 는 java 를 찾는다). 0 이 아니면 ApkError."""
        environment = dict(os.environ, JAVA_HOME=str(self.java_home))
        environment["PATH"] = str(self.java_home / "bin") + os.pathsep + environment.get("PATH", "")
        completed = subprocess.run([str(part) for part in command], env=environment, capture_output=True, encoding="utf-8", errors="replace", check=False)
        if completed.returncode != 0:
            raise ApkError(f"{step}: {Path(str(command[0])).name} exited {completed.returncode}: {(completed.stderr or completed.stdout).strip()}")


def ensure_debug_keystore(tools: AndroidTools, keystore: Path) -> None:
    """디버그 키가 없으면 만듭니다(build/ 아래 — 저장소 밖이다). 있으면 그대로 씁니다."""
    if keystore.is_file():
        return
    keystore.parent.mkdir(parents=True, exist_ok=True)
    tools.run("keystore", [tools.keytool, "-genkeypair", "-keystore", keystore, "-storepass", DEBUG_KEY_PASSWORD, "-alias", DEBUG_KEY_ALIAS,
                           "-keypass", DEBUG_KEY_PASSWORD, "-keyalg", "RSA", "-keysize", "2048", "-validity", str(DEBUG_KEY_VALIDITY_DAYS),
                           "-dname", DEBUG_KEY_DISTINGUISHED_NAME])


def add_native_library(apk_path: Path, library: Path, abi: str) -> None:
    """@p library 를 `lib/<ABI>/` 에 압축하지 않고 더합니다(`extractNativeLibs=false` 는 저장 항목만 바로 연다)."""
    with zipfile.ZipFile(apk_path, "a") as archive:
        archive.write(library, f"lib/{abi}/{library.name}", compress_type=zipfile.ZIP_STORED)


def make_apk(game: str, build_dir: Path, packs_dir: Path, project_root: Path) -> Path:
    """게임 @p game 의 서명한 디버그 APK 를 `<buildDir>/Apk/<게임>-debug.apk` 에 만들고 그 경로를 돌려줍니다."""
    search = load_search_paths()
    tools = AndroidTools(search)
    list_preset = json.loads((project_root / PRESET_FILE_NAME).read_text(encoding="utf-8"))["configurePresets"]
    min_sdk, abi = read_preset_android(list_preset)
    try:
        config = require_config(project_root / PACKAGE_CONFIG_PATH, PACKAGE_CONFIG_PATH.name)
    except ConfigError as error:
        raise ApkError(str(error)) from error
    settings = resolve_settings(config, game, min_sdk, tools.target_sdk, abi, project_root)

    library = build_dir / "Lib" / f"lib{settings.lib_name}.so"  # 공유 라이브러리는 CMAKE_LIBRARY_OUTPUT_DIRECTORY(cmake/Modules/Build/BuildLayout.cmake 의 Lib)로 간다
    if not library.is_file():
        raise ApkError(f"{library} not found - build the {PRESET_NAME} preset first")
    try:
        list_pack = select_staged_packs(project_root, packs_dir, game)
    except PackageError as error:
        raise ApkError(str(error)) from error

    work_dir = build_dir / "Apk" / game
    shutil.rmtree(work_dir, ignore_errors=True)
    asset_pack_dir = work_dir / "assets" / PACK_FOLDER_NAME
    asset_pack_dir.mkdir(parents=True)
    for pack in list_pack:
        shutil.copy2(pack, asset_pack_dir / pack.name)
    manifest = work_dir / "AndroidManifest.xml"
    manifest.write_text(render_manifest((project_root / MANIFEST_TEMPLATE_PATH).read_text(encoding="utf-8"), settings), encoding="utf-8")

    unaligned = work_dir / "unaligned.apk"
    aligned = work_dir / "aligned.apk"
    signed = build_dir / "Apk" / f"{game}-debug.apk"
    print(f"[android-apk] link {settings.application_id} (API {settings.min_sdk}..{settings.target_sdk}, {settings.screen_orientation})")
    tools.run("link", [tools.aapt2, "link", "-o", unaligned, "-I", tools.android_jar, "--manifest", manifest, "--min-sdk-version", str(settings.min_sdk),
                       "--target-sdk-version", str(settings.target_sdk), "--debug-mode", "-A", work_dir / "assets", "-0", PACK_EXTENSION])
    add_native_library(unaligned, library, settings.abi)
    print(f"[android-apk] align ({settings.page_size_kb} KB pages)")
    tools.run("align", [tools.zipalign, "-f", "-P", str(settings.page_size_kb), "4", unaligned, aligned])
    ensure_debug_keystore(tools, settings.debug_keystore)
    print("[android-apk] sign (debug key)")
    tools.run("sign", [tools.apksigner, "sign", "--ks", settings.debug_keystore, "--ks-pass", f"pass:{DEBUG_KEY_PASSWORD}", "--key-pass", f"pass:{DEBUG_KEY_PASSWORD}",
                       "--ks-key-alias", DEBUG_KEY_ALIAS, "--out", signed, aligned])
    tools.run("verify", [tools.apksigner, "verify", signed])
    return signed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts android-apk", description="Android-Shipping 빌드를 서명한 디버그 APK 로 묶는다")
    parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json — 담을 팩을 고른다)")
    parser.add_argument("--build-dir", default=f"build/{PRESET_NAME}", help="Android-Shipping 빌드 폴더(저장소 기준, 기본 build/Android-Shipping)")
    parser.add_argument("--packs-dir", default=None, help="Android 대상으로 쿠킹한 팩 폴더(기본 <build-dir>/Bin/Packs)")
    args = parser.parse_args(argv)
    project_root = get_project_root()
    build_dir = (project_root / args.build_dir).resolve()
    packs_dir = (project_root / args.packs_dir).resolve() if args.packs_dir else build_dir / "Bin" / PACK_FOLDER_NAME
    try:
        apk = make_apk(args.game, build_dir, packs_dir, project_root)
    except ApkError as error:
        print(f"[android-apk] FAILED {error}")
        return ExitCode.VIOLATION
    print(f"[android-apk] done {apk} {apk.stat().st_size}")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
