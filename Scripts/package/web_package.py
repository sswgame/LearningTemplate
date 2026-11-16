#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file web_package.py
@brief Web-Shipping 빌드 산출물(App.js · App.wasm + 쿠킹한 팩)을 브라우저에 올릴 폴더 하나로 묶고, 로컬에서 띄울 서버를 연다.

| 단계 | 도구 | 하는 일 |
| --- | --- | --- |
| packs | Emscripten `tools/file_packager.py` | 공용 팩 + 그 게임 팩(`select_staged_packs`)을 `packs.data` 와 로더 `packs.js` 로 — 가상 파일 시스템 `/Packs`(웹의 실행 파일은 `/App`, PosixFileSystem) |
| page | (이 스크립트) | `index.html` — `#canvas`(WebWindow::kCanvasSelector) 하나, `Module` → `packs.js` → `App.js` 순서 |
| copy | (shutil) | `App.js` · `App.wasm` |

`serve` 는 교차 출처 격리 헤더(COOP `same-origin` · COEP `require-corp`)를 주는 정적 서버다 — 엔진이 pthread(SharedArrayBuffer)라 그 헤더가 없으면 기동하지 못한다.
배포 서버도 같은 두 헤더를 줘야 한다. 유니티 WebGL 의 Build 폴더(.data + 로더 + .wasm)와 같은 모양이다(기억).
shortcut: 팩을 한 번에 내려받아 메모리 파일 시스템에 둔다(common 팩이 공용 에셋 전체라 수백 MB), 큰 게임은 팩을 나눠 필요할 때 fetch 하는 길로 바꾼다.

사용법:
    py -3 -m Scripts web-package --game Empty
    py -3 -m Scripts web-package --game Empty --serve        # 묶은 뒤 http://localhost:8080 으로 띄운다
"""

from __future__ import annotations

import argparse
import functools
import http.server
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from package.pack import PackageError, PACK_FOLDER_NAME, select_staged_packs  # noqa: E402

PRESET_NAME = "Web-Shipping"
#: 빌드가 내는 두 파일 — pthread 판 Emscripten 은 작업자 JS 를 따로 두지 않고 App.js 가 작업자도 띄운다.
LIST_APP_FILE_NAME = ("App.js", "App.wasm")
#: 팩이 놓일 가상 파일 시스템 자리 — 웹의 실행 파일 경로가 `/App` 이라 실행 파일 옆 `Packs` 다(App 이 exeDir/Packs 를 먼저 찾는다).
VIRTUAL_PACK_FOLDER = f"/{PACK_FOLDER_NAME}"
PACK_DATA_NAME = "packs.data"
PACK_LOADER_NAME = "packs.js"
FILE_PACKAGER_PATH = Path("Tools") / "emsdk" / "upstream" / "emscripten" / "tools" / "file_packager.py"
DEFAULT_PORT = 8080
#: 페이지 틀. `reporter` 는 배포 페이지에서 비고, 확인 페이지(`web-check` 의 check.html)만 보고 스크립트를 넣는다 — Module 을 만든 뒤 · App.js 앞.
INDEX_HTML = """<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>html, body {{ margin: 0; height: 100%; background: #000; overflow: hidden; }} #canvas {{ display: block; width: 100%; height: 100%; }}</style>
</head>
<body>
<canvas id="canvas" tabindex="-1"></canvas>
<script>
var Module = {{
    canvas: document.getElementById("canvas"),
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
}};
</script>
{reporter}<script src="{loader}"></script>
<script src="App.js"></script>
</body>
</html>
"""


class WebPackageError(Exception):
    """단계 하나가 실패했다. 메시지가 `[web-package] FAILED` 줄의 이유다."""


def make_web_package(game: str, build_dir: Path, packs_dir: Path, out_dir: Path, project_root: Path) -> Path:
    """`outDir` 에 브라우저 폴더를 만든다. 산출물이 없으면 멈춘다(빠진 것을 둔 폴더는 기동하지 못한다)."""
    list_app_file = [build_dir / "Bin" / name for name in LIST_APP_FILE_NAME]
    list_missing = [str(path) for path in list_app_file if path.is_file() is False]
    if list_missing:
        raise WebPackageError(f"{', '.join(list_missing)} not found - build the {PRESET_NAME} preset first")
    try:
        list_pack = select_staged_packs(project_root, packs_dir, game)
    except PackageError as error:
        raise WebPackageError(str(error)) from error
    file_packager = project_root / FILE_PACKAGER_PATH
    if file_packager.is_file() is False:
        raise WebPackageError(f"{file_packager} not found - install the web SDK (py -3 Scripts/setup/platform_sdk.py web)")

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    # file_packager 는 `원본@가상경로` 로 파일마다 자리를 받는다 — 팩 폴더의 다른 팩(다른 게임 · 지난 쿠킹)은 담지 않는다.
    list_preload = [f"{pack.as_posix()}@{VIRTUAL_PACK_FOLDER}/{pack.name}" for pack in list_pack]
    command = [sys.executable, str(file_packager), str(out_dir / PACK_DATA_NAME), "--preload", *list_preload, f"--js-output={out_dir / PACK_LOADER_NAME}", "--no-node"]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    if result.returncode != 0:
        raise WebPackageError(f"file_packager failed ({result.returncode}): {result.stderr.strip() or result.stdout.strip()}")
    for app_file in list_app_file:
        shutil.copy2(app_file, out_dir / app_file.name)
    (out_dir / "index.html").write_text(INDEX_HTML.format(title=game, loader=PACK_LOADER_NAME, reporter=""), encoding="utf-8", newline="\n")
    return out_dir


class IsolatedRequestHandler(http.server.SimpleHTTPRequestHandler):
    """교차 출처 격리 헤더를 붙이는 정적 파일 처리기 — SharedArrayBuffer(pthread)가 그 헤더를 요구한다."""

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")  # 다시 묶은 App.wasm 을 브라우저가 옛것으로 쓰지 않게
        super().end_headers()


def serve(folder: Path, port: int) -> None:
    """`folder` 를 localhost:`port` 로 띄운다(Ctrl+C 로 끝). localhost 는 보안 출처라 WebGPU 가 열린다."""
    handler = functools.partial(IsolatedRequestHandler, directory=str(folder))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"[web-package] serving {folder} at http://localhost:{port}/")
        server.serve_forever()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts web-package", description="Web-Shipping 빌드를 브라우저 폴더로 묶고 로컬 서버로 띄운다")
    parser.add_argument("--game", required=True, help="게임 프리셋 이름(Config/Game/<게임>.json — 담을 팩을 고른다)")
    parser.add_argument("--build-dir", default=f"build/{PRESET_NAME}", help="Web-Shipping 빌드 폴더(저장소 기준)")
    parser.add_argument("--packs-dir", default=None, help="웹 대상으로 쿠킹한 팩 폴더(기본 <build-dir>/Bin/Packs)")
    parser.add_argument("--out-dir", default=None, help="묶은 폴더(기본 <build-dir>/Web/<게임>)")
    parser.add_argument("--serve", action="store_true", help="묶은 뒤 교차 출처 격리 헤더를 주는 로컬 서버로 띄운다")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="로컬 서버 포트")
    args = parser.parse_args(argv)
    project_root = get_project_root()
    build_dir = (project_root / args.build_dir).resolve()
    packs_dir = (project_root / args.packs_dir).resolve() if args.packs_dir else build_dir / "Bin" / PACK_FOLDER_NAME
    out_dir = (project_root / args.out_dir).resolve() if args.out_dir else build_dir / "Web" / args.game
    try:
        folder = make_web_package(args.game, build_dir, packs_dir, out_dir, project_root)
    except WebPackageError as error:
        print(f"[web-package] FAILED {error}")
        return ExitCode.VIOLATION
    size = sum(path.stat().st_size for path in folder.iterdir() if path.is_file())
    print(f"[web-package] done {folder} {size}")
    if args.serve:
        os.chdir(folder)
        serve(folder, args.port)
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
