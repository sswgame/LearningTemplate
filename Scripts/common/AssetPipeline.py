"""
Scripts/common/AssetPipeline.py

쿠커가 쓰는 출력 위치 도우미:
  - resolveDefaultOutputDir: 가장 최근에 구성된 빌드의 산출물 디렉터리(build/*/Bin/<subDir>) 탐색
"""

from __future__ import annotations

import sys
from pathlib import Path


def resolveDefaultOutputDir(projectRoot: Path, subDir: str = "Packs") -> Path:
    """
    프로젝트의 build/ 디렉터리 하위에서 가장 최근에 구성/빌드된 산출물 경로(build/*/Bin/<subDir>)를 동적으로 탐색합니다.

    Args:
        projectRoot: 프로젝트 루트 경로.
        subDir: Bin 하위의 출력 서브디렉터리 명 (예: "Packs").

    Returns:
        탐색된 대상 출력 디렉터리 Path.
    """
    buildDir = projectRoot / "build"
    if buildDir.is_dir():
        candidates: list[tuple[float, Path]] = []
        for child in buildDir.iterdir():
            if not child.is_dir():
                continue
            binDir = child / "Bin"
            if binDir.is_dir():
                mtime = 0.0
                for marker in ("build.ninja", "CMakeCache.txt", "compile_commands.json"):
                    markerPath = child / marker
                    if markerPath.is_file():
                        mtime = max(mtime, markerPath.stat().st_mtime)
                if mtime == 0.0:
                    mtime = binDir.stat().st_mtime
                target = (binDir / subDir) if subDir else binDir
                candidates.append((mtime, target))

        if candidates:
            candidates.sort(key=lambda item: item[0], reverse=True)
            return candidates[0][1]

    print(f"[Error] Could not find a valid build output directory (build/*/Bin/{subDir}). Please specify output explicitly.", file=sys.stderr)
    sys.exit(1)
