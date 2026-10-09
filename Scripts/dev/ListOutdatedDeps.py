#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListOutdatedDeps.py
@brief vcpkg 의존성의 지금 판과 레지스트리 최신 판을 나란히 보입니다(고치지 않는다).

    py -3 -m Scripts deps-outdated               # Tools/vcpkg 를 fetch 한 뒤 origin/master 와 견준다
    py -3 -m Scripts deps-outdated --no-fetch    # 받아 둔 origin/master 로만
    py -3 -m Scripts deps-outdated --github      # 레지스트리에 없는 오버레이 포트(RTM · ACL …)는 GitHub 최신 릴리스로(공개 API)
    py -3 -m Scripts deps-outdated --installed   # 간접 의존(build/vcpkg_installed 의 설치 목록)까지

지금 판은 오버레이 포트(`ThirdParty/*/vcpkg-port/<포트>/vcpkg.json`)가 있으면 그 판, 없으면 `vcpkg.json` 의 `builtin-baseline` 커밋의 판입니다.
최신 판은 `Tools/vcpkg` 의 `origin/master` 의 `versions/baseline.json` 입니다. 기준선을 올리는 법과 올리지 않는 것은 docs/09_Decisions.md 5-2 입니다.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, loadSearchPaths, runGit  # noqa: E402
from common.Constants import kKeyVcpkgInstalledRel  # noqa: E402

#: 기준선과 따로 올리는 것(docs/09_Decisions.md 5-2) — 최신이 이 접두로 시작하면 "따로" 로 적는다.
kHeldBackPrefix: dict[str, tuple[str, str]] = {
    "openssl": ("4.", "OpenSSL 4 는 따로 올린다"),
    "glad": ("2.", "glad2 는 따로 올린다"),
}
#: 판을 견주지 않고 "따로" 로만 적는 포트(셰이더 컴파일러 — 구운 셰이더 전부가 바뀐다).
kHeldBackPort: dict[str, str] = {"directx-dxc": "DXC 는 따로 올린다"}
_kGithubRepo = re.compile(r"\bREPO\s+([\w.-]+/[\w.-]+)")


@dataclass
class DepRow:
    """의존성 한 줄."""

    name: str
    current: str
    latest: str
    source: str
    note: str = ""


def formatVersionInternal(entry: dict | None) -> str:
    """baseline.json · vcpkg.json 의 판 칸을 `판#포트판` 글로."""
    if not entry:
        return "-"
    version = entry.get("baseline") or entry.get("version") or entry.get("version-string") or entry.get("version-semver") or entry.get("version-date") or "?"
    portVersion = entry.get("port-version", 0)
    return f"{version}#{portVersion}" if portVersion else str(version)


def readBaselineInternal(vcpkgRoot: Path, revision: str) -> dict[str, dict]:
    """레지스트리 커밋 하나의 `versions/baseline.json` 의 default 표입니다."""
    result = runGit(["-C", str(vcpkgRoot), "show", f"{revision}:versions/baseline.json"])
    if not result.bSucceeded:
        raise SystemExit(f"[deps-outdated] cannot read versions/baseline.json at {revision}: {result.stderr.strip()}")
    return json.loads(result.stdout).get("default", {})


def collectOverlayPortsInternal(projectRoot: Path) -> dict[str, Path]:
    """오버레이 포트 이름 → 그 포트 폴더."""
    return {path.parent.name: path.parent for path in sorted((projectRoot / "ThirdParty").glob("*/vcpkg-port/*/vcpkg.json"))}


def collectDirectDepsInternal(manifest: dict) -> list[str]:
    """매니페스트의 직접 의존 이름(같은 이름이 플랫폼별로 여럿이면 하나)."""
    listName: list[str] = []
    for dependency in manifest.get("dependencies", []):
        name = dependency if isinstance(dependency, str) else dependency.get("name", "")
        if name and name not in listName:
            listName.append(name)
    return listName


def collectInstalledPortsInternal(projectRoot: Path) -> list[str]:
    """vcpkg 설치 폴더(search_paths 의 vcpkg_installed_rel)의 `vcpkg/status` 에 적힌 포트 이름(기능 줄 · 호스트 도구 포트 제외)."""
    status = projectRoot / loadSearchPaths()[kKeyVcpkgInstalledRel] / "vcpkg" / "status"
    if not status.is_file():
        return []
    listName: list[str] = []
    for block in status.read_text(encoding="utf-8").split("\n\n"):
        fields = dict(line.split(": ", 1) for line in block.splitlines() if ": " in line)
        name = fields.get("Package", "")
        if not name or "Feature" in fields or name.startswith("vcpkg-") or "install ok installed" not in fields.get("Status", ""):
            continue
        if name not in listName:
            listName.append(name)
    return listName


def fetchGithubLatestInternal(repository: str) -> str:
    """GitHub 저장소의 최신 릴리스 태그입니다(실패하면 "?")."""
    import urllib.request  # 부를 때만 — --help 가 http 모듈을 올리지 않게

    headers = {"Accept": "application/vnd.github+json", "User-Agent": "sw-engine-deps-outdated"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    try:
        request = urllib.request.Request(f"https://api.github.com/repos/{repository}/releases/latest", headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return str(json.load(response).get("tag_name", "?")).lstrip("v")
    except OSError:
        return "?"


def buildRowsInternal(projectRoot: Path, vcpkgRoot: Path, listName: list[str], bGithub: bool) -> list[DepRow]:
    """이름마다 지금 판 · 최신 판 · 출처를 채웁니다."""
    manifest = json.loads((projectRoot / "vcpkg.json").read_text(encoding="utf-8"))
    baseline = readBaselineInternal(vcpkgRoot, manifest["builtin-baseline"])
    latest = readBaselineInternal(vcpkgRoot, "origin/master")
    overlays = collectOverlayPortsInternal(projectRoot)
    listRow: list[DepRow] = []
    for name in listName:
        if name in overlays:
            current = formatVersionInternal(json.loads((overlays[name] / "vcpkg.json").read_text(encoding="utf-8")))
            source = "overlay"
        else:
            current = formatVersionInternal(baseline.get(name))
            source = "baseline"
        newest = formatVersionInternal(latest.get(name))
        if newest == "-" and bGithub and name in overlays:
            portfile = (overlays[name] / "portfile.cmake").read_text(encoding="utf-8")
            match = _kGithubRepo.search(portfile)
            newest = f"{fetchGithubLatestInternal(match.group(1))} (github)" if match else "-"
        row = DepRow(name, current, newest, source)
        if name in kHeldBackPort:
            row.note = kHeldBackPort[name]
        elif name in kHeldBackPrefix and newest.startswith(kHeldBackPrefix[name][0]):
            row.note = kHeldBackPrefix[name][1]
        elif newest not in ("-", current) and newest.startswith("?") is False:
            row.note = "올릴 수 있음" if source == "baseline" else "레지스트리와 다름(오버레이 판을 확인)"
        listRow.append(row)
    return listRow


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="vcpkg 의존성의 지금 판과 레지스트리 최신 판을 견준다")
    parser.add_argument("--no-fetch", action="store_true", help="Tools/vcpkg 를 fetch 하지 않는다")
    parser.add_argument("--github", action="store_true", help="레지스트리에 없는 오버레이 포트는 GitHub 최신 릴리스로 본다")
    parser.add_argument("--installed", action="store_true", help="간접 의존(설치 목록)까지 보인다")
    parser.add_argument("--outdated-only", action="store_true", help="올릴 수 있는 줄만")
    args = parser.parse_args(argv)

    projectRoot = getProjectRoot()
    vcpkgRoot = projectRoot / "Tools" / "vcpkg"
    if not (vcpkgRoot / ".git").exists():
        print(f"[deps-outdated] {vcpkgRoot} is not a git checkout", file=sys.stderr)
        return 2
    if not args.no_fetch:
        fetch = runGit(["-C", str(vcpkgRoot), "fetch", "--quiet", "origin", "master"])
        if not fetch.bSucceeded:
            print(f"[deps-outdated] fetch failed — using the cached origin/master: {fetch.stderr.strip()}", file=sys.stderr)

    manifest = json.loads((projectRoot / "vcpkg.json").read_text(encoding="utf-8"))
    listName = collectDirectDepsInternal(manifest)
    for name in collectOverlayPortsInternal(projectRoot):
        if name not in listName:
            listName.append(name)
    if args.installed:
        for name in collectInstalledPortsInternal(projectRoot):
            if name not in listName:
                listName.append(name)

    listRow = buildRowsInternal(projectRoot, vcpkgRoot, listName, args.github)
    if args.outdated_only:
        listRow = [row for row in listRow if row.note]
    head = runGit(["-C", str(vcpkgRoot), "log", "-1", "--format=%h %cs", "origin/master"])
    print(f"기준선 {manifest['builtin-baseline'][:10]} · 레지스트리 origin/master {head.stdout.strip() if head.bSucceeded else '?'}")
    widthName = max((len(row.name) for row in listRow), default=4)
    widthCurrent = max((len(row.current) for row in listRow), default=4)
    widthLatest = max((len(row.latest) for row in listRow), default=4)
    for row in listRow:
        print(f"  {row.name:<{widthName}}  {row.current:<{widthCurrent}}  {row.latest:<{widthLatest}}  {row.source:<8}  {row.note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
