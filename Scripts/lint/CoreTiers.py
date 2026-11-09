#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CoreTiers.py

Core 폴더 티어 표(`Config/Rules/Layers/CoreLayers.toml`)의 역류 — 낮은 티어 폴더가 높은 티어 폴더 헤더를 include — 를 **모든 티어에서** 막는다.
**왜 빌드가 아니라 린트인가.** Log 아래 · 위는 빌드가 가른다(`Source/Core/CMakeLists.txt` 의 낮은 부분 `#error` 껍데기). 티어마다 같은 방식으로 가르면
OBJECT 부분과 pch 가 늘어 빌드가 느려진다 — 형식으로 싸게 막을 수 없는 틈이라 include 줄을 본다. 직접 include 만 본다(전이는 그 헤더가 걸린다).
처음엔 Log 위만 봤는데 아래 티어 역류(Concurrency → Container/vector.h)를 놓쳤다(FolderMoves 13 절) — 그래서 모든 티어를 본다.
하위 폴더 티어(`[subtier]` — 예 `Concurrency/Queue`, `Log/Sink`)는 바깥 폴더보다 먼저 본다. 폴더의 `Tests/` 는 위쪽이라 보지 않는다.
"""

from __future__ import annotations

import re
from pathlib import Path

kIncludePattern = re.compile(r'^\s*#\s*include\s+"Core/([\w/]+)/[^/"]+"')
kTierLine = re.compile(r'^"?([\w/]+)"?\s*=\s*(\d+)')


def readTiers(projectRoot: Path) -> dict[str, int]:
    """폴더(또는 `폴더/하위` — `[subtier]` 절) → 티어."""
    tiers: dict[str, int] = {}
    section = ""
    for line in (projectRoot / "Config/Rules/Layers/CoreLayers.toml").read_text(encoding="utf-8").splitlines():
        if line.startswith("["):
            section = line.strip()
            continue
        if section not in ("[tier]", "[subtier]"):
            continue
        match = kTierLine.match(line)
        if match:
            tiers[match.group(1)] = int(match.group(2))
    return tiers


def findTier(tiers: dict[str, int], folderPath: str) -> tuple[str, int] | None:
    """`Concurrency/Queue/Sub` 같은 폴더 경로에서 가장 긴 표 키와 그 티어."""
    parts = folderPath.split("/")
    for count in range(len(parts), 0, -1):
        key = "/".join(parts[:count])
        if key in tiers:
            return key, tiers[key]
    return None


def checkCoreTiers(projectRoot: Path, listFile: list[str]) -> list[str]:
    """@p listFile 가운데 Core 파일이 더 높은 티어 폴더를 include 하면 그 줄을 돌려준다."""
    tiers = readTiers(projectRoot)
    listViolation: list[str] = []
    for file in listFile:
        relative = Path(file).resolve().relative_to(projectRoot).as_posix()
        parts = relative.split("/")
        if len(parts) < 4 or parts[0] != "Source" or parts[1] != "Core" or "Tests" in parts:
            continue
        own = findTier(tiers, "/".join(parts[2:-1]))
        if own is None:
            continue
        ownKey, tier = own
        for lineNumber, line in enumerate(Path(file).read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            match = kIncludePattern.match(line)
            if not match:
                continue
            target = findTier(tiers, match.group(1))
            if target is not None and target[1] > tier:
                listViolation.append(f"{relative}:{lineNumber}: Core/{ownKey}(티어 {tier}) 가 더 높은 Core/{target[0]}(티어 {target[1]}) 를 include 한다")
    return listViolation
