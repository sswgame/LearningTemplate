"""
Scripts/common/paths.py

프로젝트 루트 디렉터리 탐색, 경로 정규화, 템플릿 치환, 폴더 아래 파일 목록. 호스트 플랫폼 판별은 `host_platform.py` 다.
"""

from __future__ import annotations

import functools
import os
import re
from pathlib import Path


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


def get_build_dir(preset: str, project_root: Path | None = None) -> Path:
    """빌드 프리셋의 빌드 폴더 — CMakePresets.json 의 `binaryDir`(`${sourceDir}/build/${presetName}`)와 같은 자리."""
    return (project_root or get_project_root()) / "build" / preset


def get_bin_dir(preset: str, project_root: Path | None = None) -> Path:
    """빌드 프리셋의 실행 파일 폴더(`build/<프리셋>/Bin`)."""
    return get_build_dir(preset, project_root) / "Bin"


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


@functools.lru_cache(maxsize=None)
def list_files_under(root: Path) -> tuple[Path, ...]:
    """`root` 아래 모든 파일(경로 정렬 — `sorted(root.rglob("*"))` 의 파일과 같은 순서). 한 프로세스에서 폴더마다 한 번만 훑는다.

    configure 생성기(`Scripts/generate/run_generators.py`)는 한 프로세스에서 층 표 둘 · 타깃 가드 · 매니페스트가 같은 `Source/` 를
    따로 rglob 하고 항목마다 `is_file()` 을 물어 같은 트리를 네 번 훑었다. 부분 폴더는 `Source/` 목록을 `is_relative_to` 로 거른다.
    프로세스 안에서 트리가 바뀌지 않는 짧은 도구 전용이다(오래 도는 프로세스는 쓰지 않는다).
    """
    list_path: list[Path] = []
    for directory, _, list_file_name in os.walk(root):
        list_path.extend(Path(directory, name) for name in list_file_name)
    return tuple(sorted(list_path))
