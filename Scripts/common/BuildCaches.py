#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
빌드 시간에 끼는 캐시의 목록과 콜드 측정용 지우기.

정본은 데이터 한 곳 `cmake/Config/BuildCaches.json` 이다(CMake 의 `BuildLayout.cmake` 도 같은 파일에서 LTO 캐시 이름을 읽는다).
`ninja clean` 은 출력이 아닌 캐시(링크 캐시 · 쿠킹 기록 · DDC)를 지우지 않아, 콜드 기준선(`RunBuildBaseline.py full`)이 두 번째 판부터 캐시가 찬 값이 된다.
여기서는 `cold_clear` 목록을 읽어 지운다. 목록을 못 읽거나 모양이 틀리면 기본값 없이 멈춘다.

링크 폴더(`build/vcpkg_*` · `Tools/*` — main 과 나눠 쓴다, docs/11_Workflow.md)는 지우지 않는다: 목록의 경로가 링크이거나 링크 위에 있으면 지우지 않고 멈춘다.
"""

from __future__ import annotations

import json
import shutil
from dataclasses import dataclass
from pathlib import Path

__all__ = ["ColdCache", "clearColdCaches", "findLinkOnPath", "loadColdCaches", "resolveColdCachePaths"]

#: 정본 데이터 파일(저장소 루트 기준).
kBuildCachesFile = "cmake/Config/BuildCaches.json"
#: 캐시 경로의 기준 — `build` 는 빌드 폴더, `source` 는 저장소(프로젝트) 루트.
kBaseBuild = "build"
kBaseSource = "source"
kBases = (kBaseBuild, kBaseSource)


@dataclass(frozen=True)
class ColdCache:
    """콜드 측정이 지울 캐시 하나."""

    name: str
    base: str
    path: str
    reason: str


def loadColdCaches(repositoryRoot: Path) -> list[ColdCache]:
    """`cold_clear` 목록. 파일이 없거나 · JSON 이 아니거나 · 모양이 틀리면 RuntimeError."""
    filePath = repositoryRoot / kBuildCachesFile
    try:
        document = json.loads(filePath.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError(f"{kBuildCachesFile} 를 읽지 못했다: {error}") from error
    mapCold = document.get("cold_clear") if isinstance(document, dict) else None
    if not isinstance(mapCold, dict) or not mapCold:
        raise RuntimeError(f"{kBuildCachesFile}: cold_clear 객체가 없거나 비었다")
    listCache: list[ColdCache] = []
    for name, entry in mapCold.items():
        if not isinstance(entry, dict):
            raise RuntimeError(f"{kBuildCachesFile}: cold_clear.{name} 가 객체가 아니다")
        base, path, reason = entry.get("base"), entry.get("path"), entry.get("reason")
        if base not in kBases:
            raise RuntimeError(f"{kBuildCachesFile}: cold_clear.{name}.base 는 {' | '.join(kBases)} 여야 한다(받은 값 {base!r})")
        if not isinstance(path, str) or not path or Path(path).is_absolute() or ".." in Path(path).parts:
            raise RuntimeError(f"{kBuildCachesFile}: cold_clear.{name}.path 는 {base} 기준 상대 경로여야 한다(받은 값 {path!r})")
        if not isinstance(reason, str) or not reason:
            raise RuntimeError(f"{kBuildCachesFile}: cold_clear.{name}.reason 이 없다")
        listCache.append(ColdCache(name, base, path, reason))
    return listCache


def resolveColdCachePaths(listCache: list[ColdCache], buildDir: Path, repositoryRoot: Path) -> list[tuple[ColdCache, Path]]:
    """캐시마다 실제 경로(`build` 는 @p buildDir, `source` 는 @p repositoryRoot 기준)."""
    mapRoot = {kBaseBuild: buildDir, kBaseSource: repositoryRoot}
    return [(cache, mapRoot[cache.base] / cache.path) for cache in listCache]


def findLinkOnPath(path: Path, root: Path) -> Path | None:
    """@p root 아래 @p path 까지의 단계(자신 포함) 가운데 링크(심볼릭 링크 · 정션)인 첫 경로. 없으면 None."""
    current = root
    for part in path.relative_to(root).parts:
        current = current / part
        isJunction = getattr(current, "is_junction", None)
        if current.is_symlink() or bool(isJunction and isJunction()):
            return current
    return None


def clearColdCaches(buildDir: Path, repositoryRoot: Path) -> list[Path]:
    """
    목록의 캐시 폴더를 지운다(없는 것은 건너뛴다). 지운 경로를 돌려준다.

    목록을 못 읽으면 RuntimeError 로 멈춘다. 지우려는 경로가 링크이거나 링크 위에 있으면 아무것도 지우지 않고 멈춘다(공유 폴더를 지우지 않는다).
    """
    listTarget = resolveColdCachePaths(loadColdCaches(repositoryRoot), buildDir, repositoryRoot)
    for cache, path in listTarget:
        root = buildDir if cache.base == kBaseBuild else repositoryRoot
        link = findLinkOnPath(path, root)
        if link is not None:
            raise RuntimeError(f"{kBuildCachesFile}: cold_clear.{cache.name} 의 {path} 는 링크({link}) 위에 있어 지우지 않는다 — 공유 폴더다")
    listRemoved: list[Path] = []
    for _, path in listTarget:
        if path.is_dir():
            shutil.rmtree(path)
            listRemoved.append(path)
        elif path.exists():
            raise RuntimeError(f"{path} 가 폴더가 아니다 — 캐시 폴더만 지운다")
    return listRemoved
