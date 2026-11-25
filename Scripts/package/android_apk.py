"""
Scripts/package/android_apk.py

Android 빌드(lib<이름>.so + 쿠킹한 팩)를 서명한 디버그 APK 로 묶는 명령 — 묶는 일은 `apk_packager.py` 가 한다.
"""

from __future__ import annotations

import argparse

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from package.apk_packager import DEFAULT_PRESET_NAME, LIST_PRESET_NAME, make_apk
from package.pack_files import PACK_FOLDER_NAME


class AndroidApkCommand(Command):
    name = "android-apk"
    help = "Android 빌드(lib<이름>.so + 쿠킹한 팩)를 서명한 디버그 APK 로 묶는다(--game, --preset Android-Shipping|Android-Release, aapt2 · zipalign · apksigner — Tools/Android)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json — 담을 팩을 고른다)")
        parser.add_argument("--preset", choices=LIST_PRESET_NAME, default=DEFAULT_PRESET_NAME,
                            help="지은 구성 프리셋 — 최소 API · ABI 를 이 프리셋(상속 포함)에서 읽고 빌드 폴더의 기본값이 된다(기본 Android-Shipping)")
        parser.add_argument("--build-dir", default=None, help="빌드 폴더(저장소 기준, 기본 build/<프리셋>)")
        parser.add_argument("--packs-dir", default=None, help="Android 대상으로 쿠킹한 팩 폴더(기본 <build-dir>/Bin/Packs)")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        project_root = context.root
        build_dir = (project_root / (args.build_dir if args.build_dir else f"build/{args.preset}")).resolve()
        packs_dir = (project_root / args.packs_dir).resolve() if args.packs_dir else build_dir / "Bin" / PACK_FOLDER_NAME
        apk = make_apk(args.game, args.preset, build_dir, packs_dir, project_root)
        print(f"[android-apk] done {apk} {apk.stat().st_size}")
        return ExitCode.PASSED
