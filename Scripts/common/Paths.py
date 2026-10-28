"""
Scripts/common/Paths.py

프로젝트 루트 디렉터리 탐색, 경로 정규화, 템플릿 치환 및 플랫폼 판별 유틸리티.
"""

from __future__ import annotations

import functools
import os
import platform
import re
from pathlib import Path

PathLike = str | Path


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

    게이트가 include 마다 부른다(트리 전체에서 수십만 번) — 이미 정규형인 글자(`\\` · `//` · `.` 조각 · 끝 `/` · 드라이브 `:` 가 없는 것)는
    `Path` 를 만들지 않고 그대로 돌려준다. 답은 `Path(x).as_posix()` 와 같다.
    """
    if isinstance(pathString, str):
        if not pathString:
            return ""
        if ("\\" not in pathString and "//" not in pathString and "/./" not in pathString and ":" not in pathString
                and not pathString.startswith("./") and not pathString.endswith(("/", "/."))):
            return pathString
    return Path(pathString).as_posix()


def expandPathTemplate(template: str, extras: dict[str, str] | None = None) -> str:
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
    현재 시스템에 대한 플랫폼 키('windows', 'linux')를 반환합니다.
    """
    systemName = platform.system().lower()
    if systemName.startswith("win"):
        return "windows"
    return "linux"


def sharedLibraryNames(baseName: str) -> list[str]:
    """
    플랫폼별 동적 라이브러리 파일명 목록을 반환합니다 (.dll, .so).
    """
    match platform.system():
        case "Windows":
            return [f"{baseName}.dll"]
        case _:
            name = baseName if baseName.startswith("lib") else f"lib{baseName}"
            return [f"{name}.so", f"{name}.so.1"]


def platformScriptCommand(scriptPath: PathLike) -> list[str]:
    """
    플랫폼별 쉘 스크립트 실행 명령어 리스트를 반환합니다 (Windows: cmd /c, POSIX: bash).
    """
    resolved = str(scriptPath)
    if platform.system() == "Windows":
        return ["cmd", "/c", resolved]  # .bat · .cmd 는 cmd 만 돌린다(CreateProcess 가 직접 띄우지 못한다)
    return ["bash", resolved]

