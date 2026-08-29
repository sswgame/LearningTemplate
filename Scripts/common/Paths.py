"""
Scripts/common/Paths.py

프로젝트 루트 디렉터리 탐색, 경로 정규화, 템플릿 치환 및 플랫폼 판별 유틸리티.
"""

from __future__ import annotations

import functools
import os
import platform
import re
import sys
from pathlib import Path
from typing import Dict, Optional

PathLike = str | Path


def ensureScriptsOnPath() -> Path:
    """
    Scripts/ 디렉터리가 파이썬 탐색 경로(sys.path)에 없으면 중복되지 않게 추가합니다.
    """
    scriptsDir = Path(__file__).resolve().parents[1]
    scriptsPathString = str(scriptsDir)
    if scriptsPathString not in sys.path:
        sys.path.insert(0, scriptsPathString)
    return scriptsDir


@functools.lru_cache(maxsize=1)
def getProjectRoot() -> Path:
    """
    CMakeLists.txt 파일이 존재하는 디렉터리를 찾아 프로젝트 최상위 루트 경로를 반환합니다.
    """
    startDir = Path(__file__).resolve().parents[1]
    for candidate in [startDir, *startDir.parents]:
        if (candidate / "CMakeLists.txt").is_file():
            return candidate
    return startDir.parent


def normalizePath(pathString: PathLike) -> str:
    """
    경로 문자열을 정규화하여 POSIX 스타일(슬래시 사용) 문자열로 반환합니다.
    """
    if isinstance(pathString, str) and not pathString:
        return ""
    return Path(pathString).as_posix()


def joinPath(root: PathLike, *relativeParts: PathLike) -> str:
    """
    루트와 상대 구간을 POSIX `/` 로 이어 붙입니다. root가 비면 empty.
    """
    if isinstance(root, str) and not root:
        return ""
    result = Path(root)
    for part in relativeParts:
        if isinstance(part, str) and not part:
            continue
        result = result / part
    return result.as_posix()


def startsWithPathComponent(path: PathLike, component: PathLike) -> bool:
    """
    path가 component 자체이거나 `component/` 로 시작하는지 (경로 세그먼트 경계).
    """
    pathPosix = normalizePath(path)
    componentPosix = normalizePath(component).rstrip("/")
    if not componentPosix:
        return False
    if pathPosix == componentPosix:
        return True
    return pathPosix.startswith(componentPosix + "/")


def expandPathTemplate(template: str, extras: Optional[Dict[str, str]] = None) -> str:
    """
    경로 템플릿 내의 예약된 변수(${sourceDir}, ${ProjectRoot})와
    추가 변수(extras), 환경 변수를 실제 값으로 확장하여 반환합니다.
    """
    if not template:
        return ""
    projectRoot = str(getProjectRoot())
    expanded = template.replace("${sourceDir}", projectRoot).replace("${ProjectRoot}", projectRoot)
    if extras:
        for key, value in extras.items():
            expanded = expanded.replace(f"${{{key}}}", str(value))

    def replaceBraceEnvInternal(match: re.Match[str]) -> str:
        key = match.group(1)
        envValue = os.environ.get(key)
        return envValue if envValue is not None else match.group(0)

    expanded = re.sub(r"\$\{([A-Za-z0-9_()]+)\}", replaceBraceEnvInternal, expanded)
    return os.path.expandvars(expanded)


def platformKey() -> str:
    """
    현재 시스템에 대한 플랫폼 키('windows', 'darwin', 'linux')를 반환합니다.
    """
    systemName = platform.system().lower()
    if systemName.startswith("win"):
        return "windows"
    if systemName == "darwin":
        return "darwin"
    return "linux"
