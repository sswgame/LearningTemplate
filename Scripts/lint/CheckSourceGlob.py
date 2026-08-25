#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CMake GLOB 소스 누락 검사.

sw_collectSources 는 CONFIGURE_DEPENDS 없이 GLOB 하므로, 새 .cpp 추가 후
reconfigure 하지 않으면 타겟에서 빠질 수 있다. 이 스크립트는 디렉터리의
.cpp/.c 가 해당 타겟 compile_commands 또는 sources 목록에 있는지 힌트를 준다.

현재는 Engine/App/Editor/GameFramework/Games 아래 .cpp 존재 여부와
build/*/compile_commands.json 참조 여부를 비교한다.

  python Scripts/lint/CheckSourceGlob.py [--root <repo>] [--build <dir>]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ConfigHelper import getProjectRoot
import ConfigHelper

_kDirSourceGames = ConfigHelper.kDirSourceGames

_kScanRoots = (
    ConfigHelper.kDirSourceEngine,
    ConfigHelper.kDirSourceApp,
    ConfigHelper.kDirSourceEditor,
    ConfigHelper.kDirSourceGameFramework,
    _kDirSourceGames,
    ConfigHelper.kDirSourceCore,
)

_kIgnoreSubdirs = ("Graphics/RHI/Modules/", "/Linux/", "/Mac/", "/Cocoa", "/X11")

def main() -> int:
    parser = argparse.ArgumentParser(description="소스 GLOB 누락 검사")
    parser.add_argument("--root", type=Path, default=None)
    parser.add_argument("--build", type=Path, default=None, help="compile_commands.json 이 있는 빌드 디렉터리")
    parser.add_argument("--active-game", default="Demo", help="SW_ACTIVE_GAME 팩 이름")
    args = parser.parse_args()
    repo = (args.root or getProjectRoot()).resolve()

    sources: list[Path] = []
    for rel in _kScanRoots:
        base = repo / rel
        if not base.is_dir():
            continue
        sources.extend(p for p in base.rglob("*.cpp") if p.is_file())
        sources.extend(p for p in base.rglob("*.c") if p.is_file())

    # Delay-load hook / module entries are intentional special cases; still count them.
    buildDir = args.build
    if buildDir is None:
        candidates = sorted((repo / "build").glob("*/compile_commands.json"), reverse=True)
        if (repo / "build" / "compile_commands.json").is_file():
            candidates.insert(0, repo / "build" / "compile_commands.json")
        buildDir = candidates[0].parent if candidates else None
    else:
        buildDir = buildDir.resolve()

    if buildDir is None or not (buildDir / "compile_commands.json").is_file():
        print("[CheckSourceGlob] compile_commands.json 없음 — 소스 목록만 보고합니다.")
        print(f"[CheckSourceGlob] scanned {len(sources)} translation units under Source/")
        return 0

    compiled: set[str] = set()
    data = json.loads((buildDir / "compile_commands.json").read_text(encoding="utf-8"))
    for entry in data:
        filePath = Path(entry.get("file", "")).resolve()
        try:
            compiled.add(filePath.relative_to(repo).as_posix().lower())
        except ValueError:
            compiled.add(filePath.as_posix().lower())

    missing: list[str] = []
    for src in sources:
        rel = src.resolve().relative_to(repo).as_posix().replace("\\", "/")
        # MODULE entries / inactive packs / other-OS sources are expected absences.
        if any(ignore in rel for ignore in _kIgnoreSubdirs):
            continue
        if rel.startswith(f"{_kDirSourceGames}/"):
            active = f"/{args.active_game}/"
            if active not in rel.replace("\\", "/"):
                continue
        if rel.lower() not in compiled:
            missing.append(rel)

    if missing:
        print(f"[CheckSourceGlob] compile_commands에 없는 소스 {len(missing)}개 (reconfigure 필요할 수 있음):")
        for line in missing[:40]:
            print(f"  - {line}")
        if len(missing) > 40:
            print(f"  ... +{len(missing) - 40} more")
        return 1

    print(f"[CheckSourceGlob] OK ({len(sources)} sources referenced in {buildDir})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
