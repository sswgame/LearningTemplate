"""
Scripts/common/paths.py

프로젝트 루트 디렉터리 탐색, 경로 정규화, 템플릿 치환 및 플랫폼 판별 유틸리티.
"""

from __future__ import annotations

import functools
import os
import re
from pathlib import Path

from .host_platform import is_windows_host, HOST

PathLike = str | Path


@functools.lru_cache(maxsize=1)
def get_project_root() -> Path:
    """
    CMakeLists.txt 파일이 존재하는 디렉터리를 찾아 프로젝트 최상위 루트 경로를 반환합니다.
    """
    start_dir = Path(__file__).resolve().parents[1]
    for candidate in [start_dir, *start_dir.parents]:
        if (candidate / "CMakeLists.txt").is_file():
            return candidate
    return start_dir.parent


def normalize_path(path_string: PathLike) -> str:
    """
    경로 문자열을 정규화하여 POSIX 스타일(슬래시 사용) 문자열로 반환합니다.

    게이트가 include 마다 부른다(트리 전체에서 수십만 번) — 이미 정규형인 글자(`\\` · `//` · `.` 조각 · 끝 `/` · 드라이브 `:` 가 없는 것)는
    `Path` 를 만들지 않고 그대로 돌려준다. 답은 `Path(x).as_posix()` 와 같다.
    """
    if isinstance(path_string, str):
        if not path_string:
            return ""
        if ("\\" not in path_string and "//" not in path_string and "/./" not in path_string and ":" not in path_string
                and not path_string.startswith("./") and not path_string.endswith(("/", "/."))):
            return path_string
    return Path(path_string).as_posix()


def expand_path_template(template: str, extras: dict[str, str] | None = None) -> str:
    """
    경로 템플릿 내의 예약된 변수(${sourceDir}, ${ProjectRoot})와
    추가 변수(extras), 환경 변수를 실제 값으로 확장하여 반환합니다.
    """
    if not template:
        return ""
    project_root = str(get_project_root())
    expanded = template.replace("${sourceDir}", project_root).replace("${ProjectRoot}", project_root)
    if extras:
        for key, value in extras.items():
            expanded = expanded.replace(f"${{{key}}}", str(value))

    def _replace_brace_env(match: re.Match[str]) -> str:
        key = match.group(1)
        env_value = os.environ.get(key)
        return env_value if env_value is not None else match.group(0)

    expanded = re.sub(r"\$\{([A-Za-z0-9_()]+)\}", _replace_brace_env, expanded)
    return os.path.expandvars(expanded)


def platform_key() -> str:
    """
    현재 시스템에 대한 플랫폼 키('windows', 'linux')를 반환합니다.
    """
    return HOST.key


def shared_library_names(base_name: str) -> list[str]:
    """
    플랫폼별 동적 라이브러리 파일명 목록을 반환합니다 (.dll, .so).
    """
    if is_windows_host():
        return [f"{base_name}{HOST.shared_library_suffix}"]
    name = base_name if base_name.startswith(HOST.shared_library_prefix) else f"{HOST.shared_library_prefix}{base_name}"
    return [f"{name}{HOST.shared_library_suffix}", f"{name}{HOST.shared_library_suffix}.1"]


def platform_script_command(script_path: PathLike) -> list[str]:
    """
    플랫폼별 쉘 스크립트 실행 명령어 리스트를 반환합니다 (Windows: cmd /c, POSIX: bash).
    """
    resolved = str(script_path)
    if is_windows_host():
        return ["cmd", "/c", resolved]  # .bat · .cmd 는 cmd 만 돌린다(CreateProcess 가 직접 띄우지 못한다)
    return ["bash", resolved]

