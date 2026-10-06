"""
Scripts/common/AssetPipeline.py

쿠커가 쓰는 출력 위치 도우미:
  - resolveDefaultOutputDir: 가장 최근에 구성된 빌드의 산출물 디렉터리(build/*/Bin/<subDir>) 탐색
"""

from __future__ import annotations

from pathlib import Path

from .BuildTree import BuildTree, BuildTreeError


def resolveDefaultOutputDir(projectRoot: Path, subDir: str = "Packs") -> Path:
    """
    프로젝트의 build/ 디렉터리 하위에서 가장 최근에 구성/빌드된 산출물 경로(build/*/Bin/<subDir>)를 동적으로 탐색합니다.

    Args:
        projectRoot: 프로젝트 루트 경로.
        subDir: Bin 하위의 출력 서브디렉터리 명 (예: "Packs").

    Returns:
        탐색된 대상 출력 디렉터리 Path. 구성된 빌드가 없으면 `BuildTreeError`(부르는 쪽이 1 로 끝낸다).
    """
    buildDir = projectRoot / "build"
    if buildDir.is_dir():
        candidates: list[tuple[float, Path]] = []
        for child in buildDir.iterdir():
            if not child.is_dir():
                continue
            tree = BuildTree(child)
            if tree.binDir.is_dir() and tree.bConfigured:
                mtime = (tree.path / "CMakeCache.txt").stat().st_mtime
                candidates.append((mtime, (tree.binDir / subDir) if subDir else tree.binDir))

        if candidates:
            candidates.sort(key=lambda item: item[0], reverse=True)
            return candidates[0][1]

    raise BuildTreeError(f"구성된 빌드 폴더가 없어 기본 출력 폴더(build/*/Bin/{subDir})를 정하지 못했다 — 출력 폴더를 직접 주거나 cmake --preset 을 먼저 돌린다")
