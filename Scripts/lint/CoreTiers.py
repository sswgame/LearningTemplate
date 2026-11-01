#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CoreTiers.py

Core 폴더 티어 표(`Scripts/generate/rules/CoreLayers.toml`)의 **높은 티어끼리**(Log 이상) 역류 — 낮은 티어 폴더가 높은 티어 폴더 헤더를 include — 를 막는다.
**왜 빌드가 아니라 린트인가.** Log 아래 · 위는 빌드가 가른다(`Source/Core/CMakeLists.txt` 의 낮은 부분 `#error` 껍데기). 높은 티어 여섯을 같은 방식으로 가르면 티어마다
OBJECT 부분과 pch 가 늘어 빌드가 느려진다 — 형식으로 싸게 막을 수 없는 틈이라 include 줄을 본다(StructureRefactor 7 절 11). 직접 include 만 본다(전이는 그 헤더가 걸린다).
`Log/Sink` 는 맨 위 층이다(표 주석). 폴더의 `Tests/` 는 위쪽이라 보지 않는다.
"""

from __future__ import annotations

import re
from pathlib import Path

kLogTier = 5
kIncludePattern = re.compile(r'^\s*#\s*include\s+"Core/(\w+)/')


def readTiers(projectRoot: Path) -> dict[str, int]:
    tiers: dict[str, int] = {}
    for line in (projectRoot / "Scripts/generate/rules/CoreLayers.toml").read_text(encoding="utf-8").splitlines():
        match = re.match(r'^(\w+)\s*=\s*(\d+)', line)
        if match:
            tiers[match.group(1)] = int(match.group(2))
    return tiers


def checkCoreTiers(projectRoot: Path, listFile: list[str]) -> list[str]:
    """@p listFile 가운데 Core 높은 티어 파일이 더 높은 티어 폴더를 include 하면 그 줄을 돌려준다."""
    tiers = readTiers(projectRoot)
    listViolation: list[str] = []
    for file in listFile:
        relative = Path(file).resolve().relative_to(projectRoot).as_posix()
        parts = relative.split("/")
        if len(parts) < 4 or parts[0] != "Source" or parts[1] != "Core" or "Tests" in parts or relative.startswith("Source/Core/Log/Sink/"):
            continue
        tier = tiers.get(parts[2])
        if tier is None or tier < kLogTier:
            continue
        for lineNumber, line in enumerate(Path(file).read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            match = kIncludePattern.match(line)
            if match and tiers.get(match.group(1), -1) > tier:
                listViolation.append(f"{relative}:{lineNumber}: Core/{parts[2]}(티어 {tier}) 가 더 높은 Core/{match.group(1)}(티어 {tiers[match.group(1)]}) 를 include 한다")
    return listViolation
