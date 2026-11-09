#!/usr/bin/env python3
"""
Scripts/setup/BrowserMcp.py

에이전트가 브라우저를 조종하는 MCP 서버(Google Chrome DevTools MCP, Apache-2.0)를 Tools/BrowserMCP 에 고정 판으로 깔고(인자 없음), 띄운다(`serve`).
홈 폴더 · 전역 npm 에 깔지 않는다(저장소 규칙 — 도구는 Tools/). `.mcp.json` 의 chrome-devtools 서버가 `serve` 를 부른다.
`serve` 는 Chrome, 없으면 Edge(둘 다 Chromium 이라 같은 DevTools 프로토콜)를 사용자 프로필을 건드리지 않는 임시 프로필(--isolated)로 띄운다.

  py -3 -m Scripts browser-mcp          # 깐다
  py -3 -m Scripts browser-mcp serve    # 띄운다(.mcp.json)
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence

kPackage = "chrome-devtools-mcp"
kVersion = "1.12.0"  # 판을 올릴 때는 이 줄만 바꾼다(출시 2 주가 지난 판)
kBrowserCandidates = (
    r"%ProgramFiles%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
    r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
    r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe",
)


def getInstallDir() -> Path:
    return Path(__file__).resolve().parents[2] / "Tools" / "BrowserMCP"


def getEntryScript() -> Path:
    return getInstallDir() / "node_modules" / kPackage / "build" / "src" / "bin" / "chrome-devtools-mcp.js"


def findBrowser() -> str | None:
    for candidate in kBrowserCandidates:
        path = os.path.expandvars(candidate)
        if os.path.isfile(path):
            return path
    return None


def install() -> int:
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


def serve(listArgument: Sequence[str]) -> int:
    entry = getEntryScript()
    if entry.is_file() is False:
        print(f"[BrowserMcp] 서버가 깔리지 않았다 — py -3 -m Scripts browser-mcp ({entry})", file=sys.stderr)
        return 1
    browser = findBrowser()
    if browser is None:
        print("[BrowserMcp] Chrome · Edge 를 찾지 못했다", file=sys.stderr)
        return 1
    node = shutil.which("node")
    if node is None:
        print("[BrowserMcp] node 가 없다", file=sys.stderr)
        return 1
    return subprocess.call([node, str(entry), "--executablePath", browser, "--isolated", *listArgument])


def main(listArgument: Sequence[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if listArgument is None else listArgument)
    if listArgument[:1] == ["serve"]:
        return serve(listArgument[1:])
    return install()


if __name__ == "__main__":
    raise SystemExit(main())
