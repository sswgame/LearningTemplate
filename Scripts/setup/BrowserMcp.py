#!/usr/bin/env python3
"""
Scripts/setup/BrowserMcp.py

에이전트가 브라우저를 조종하는 MCP 서버(Google Chrome DevTools MCP, Apache-2.0)를 Tools/BrowserMCP 에 고정 판으로 깝니다.
홈 폴더 · 전역 npm 에 깔지 않는다(저장소 규칙 — 도구는 Tools/). 실행은 Scripts/dev/BrowserMcp.py 가 맡고 .mcp.json 이 그것을 부른다.

  py -3 -m Scripts browser-mcp
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence

kPackage = "chrome-devtools-mcp"
kVersion = "1.12.0"  # 판을 올릴 때는 이 줄만 바꾼다(출시 2 주가 지난 판)


def getInstallDir() -> Path:
    return Path(__file__).resolve().parents[2] / "Tools" / "BrowserMCP"


def getEntryScript() -> Path:
    return getInstallDir() / "node_modules" / kPackage / "build" / "src" / "bin" / "chrome-devtools-mcp.js"


def main(listArgument: Sequence[str] | None = None) -> int:
    npm = shutil.which("npm")
    if npm is None:
        print("[BrowserMcp] npm 이 없다 — Node.js 를 깔아야 한다", file=sys.stderr)
        return 1
    installDir = getInstallDir()
    installDir.mkdir(parents=True, exist_ok=True)
    command = [npm, "install", "--prefix", str(installDir), "--no-fund", "--no-audit", "--ignore-scripts", f"{kPackage}@{kVersion}"]
    if subprocess.run(command, check=False).returncode != 0:
        print(f"[BrowserMcp] {kPackage}@{kVersion} 설치 실패", file=sys.stderr)
        return 1
    if getEntryScript().is_file() is False:
        print(f"[BrowserMcp] 설치는 끝났지만 진입 스크립트가 없다: {getEntryScript()}", file=sys.stderr)
        return 1
    print(f"[BrowserMcp] {kPackage}@{kVersion} -> {installDir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
