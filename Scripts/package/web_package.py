"""
Scripts/package/web_package.py

Web-Shipping 빌드(App.js · App.wasm + 쿠킹한 팩)를 브라우저 폴더로 묶는 명령 — 묶는 일은 `web_packager.py` 가 한다.
"""

from __future__ import annotations

import argparse
import os

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from common.paths import get_build_dir
from package.pack_files import PACK_FOLDER_NAME
from package.web_packager import DEFAULT_PORT, PRESET_NAME, make_web_package, serve


class WebPackageCommand(Command):
    name = "web-package"
    help = "Web-Shipping 빌드(App.js · App.wasm + 쿠킹한 팩)를 브라우저 폴더로 묶는다(--game, file_packager — Tools/emsdk), --serve 로 교차 출처 격리 헤더를 주는 로컬 서버까지"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json — 담을 팩을 고른다)")
        parser.add_argument("--serve", action="store_true", help="묶은 뒤 교차 출처 격리 헤더를 주는 로컬 서버로 띄운다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        project_root = context.root
        build_dir = get_build_dir(PRESET_NAME, project_root).resolve()
        folder = make_web_package(args.game, build_dir, build_dir / "Bin" / PACK_FOLDER_NAME, build_dir / "Web" / args.game, project_root)
        size = sum(path.stat().st_size for path in folder.rglob("*") if path.is_file())
        print(f"[web-package] done {folder} {size}")
        if args.serve:
            os.chdir(folder)
            serve(folder, DEFAULT_PORT)
        return ExitCode.PASSED
