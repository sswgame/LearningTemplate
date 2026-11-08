#!/usr/bin/env python3
"""
Scripts/dev/BrowserMcp.py

.mcp.json 의 chrome-devtools 서버 입구 — Tools/BrowserMCP 에 깐 Chrome DevTools MCP 를 이 컴퓨터의 Chromium 브라우저로 띄웁니다.
브라우저는 Chrome, 없으면 Edge(둘 다 Chromium 이라 같은 DevTools 프로토콜)를 찾는다. 둘 다 없거나 서버가 깔리지 않았으면 이유를 남기고 멈춘다.
--isolated: 사용자의 프로필을 건드리지 않고 임시 프로필로 띄운다.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from setup.BrowserMcp import getEntryScript  # noqa: E402

kBrowserCandidates = (
    r"%ProgramFiles%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
    r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe",
    r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe",
    r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe",
)


def findBrowser() -> str | None:
    for candidate in kBrowserCandidates:
        path = os.path.expandvars(candidate)
        if os.path.isfile(path):
            return path
    return None


def main() -> int:
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
    return subprocess.call([node, str(entry), "--executablePath", browser, "--isolated", *sys.argv[1:]])


if __name__ == "__main__":
    raise SystemExit(main())
