"""
Scripts/common/file_collect.py

파일 및 디렉터리 탐색, 검색 루트 템플릿 확장, vcpkg 루트 검증 유틸리티.
"""

from __future__ import annotations

import functools
import os
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator

from .project_constants import CPP_ALL_EXTENSIONS, GENERATED_REL_DIRS, LINT_TARGET_REL_DIRS, NOT_OUR_DIR_NAMES
from .paths import expand_path_template, platform_key

_RGLOB_SKIP_DIR_NAMES = {"buildtrees", "downloads", "packages"}


@functools.lru_cache(maxsize=None)
def list_files_under(root: Path) -> tuple[Path, ...]:
    """`root` 아래 모든 파일(경로 정렬 — `sorted(root.rglob("*"))` 의 파일과 같은 순서). 한 프로세스에서 폴더마다 한 번만 훑는다.

    configure 생성기(`Scripts/generate/configure_files.py`)는 한 프로세스에서 층 표 둘 · 타깃 게이트 · 매니페스트가 같은 `Source/` 를
    따로 rglob 하고 항목마다 `is_file()` 을 물어 같은 트리를 네 번 훑었다. 부분 폴더는 `Source/` 목록을 `is_relative_to` 로 거른다.
    프로세스 안에서 트리가 바뀌지 않는 짧은 도구 전용이다(오래 도는 프로세스는 쓰지 않는다).
    """
    list_path: list[Path] = []
    for directory, _, list_file_name in os.walk(root):
        list_path.extend(Path(directory, name) for name in list_file_name)
    return tuple(sorted(list_path))


def get_lint_search_dirs(repository_root: Path) -> list[Path]:
    """린트 및 포맷팅 대상 루트 디렉터리 목록을 반환합니다 (Source, Test, Source/Programs/ReflectionParser)."""
    return [repository_root / rel for rel in LINT_TARGET_REL_DIRS]


def collect_source_files(roots: Iterable[Path],
                       extensions: set[str] | None = None,
                       *,
                       exclude_subdirs: Iterable[str] | None = None) -> list[Path]:
    """
    지정된 디렉터리들에서 C++ 파일(.cpp, .h 등) 목록을 재귀적으로 수집하여 정렬된 리스트로 반환합니다.

    Args:
        roots: 탐색할 루트 디렉터리 목록
        extensions: 대상 파일 확장자 집합 (기본값: CPP_ALL_EXTENSIONS)
        exclude_subdirs: 제외할 폴더 이름 목록 — 루트 **아래** 경로의 폴더 하나와 같으면 뺀다. 절대 경로의 부분 문자열로 보면
                        저장소가 `.../bl-buildlint/` 처럼 그 말을 이름에 품은 폴더에 있을 때 파일이 전부 빠진다.

    Returns:
        정렬된 Path 객체 리스트
    """
    target_extensions = extensions if extensions is not None else CPP_ALL_EXTENSIONS
    set_excluded_dir_name = set(exclude_subdirs) if exclude_subdirs else set()
    result_list: list[Path] = []
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix.lower() not in target_extensions:
                continue
            if set_excluded_dir_name and set_excluded_dir_name.intersection(path.relative_to(root).parts[:-1]):
                continue
            result_list.append(path)
    return sorted(result_list)


def _is_generated_dir(repository_root: Path, directory: Path) -> bool:
    """`directory` 가 저장소 기준 생성물 폴더(`GENERATED_REL_DIRS`)인가."""
    try:
        return directory.relative_to(repository_root).as_posix() in GENERATED_REL_DIRS
    except ValueError:
        return False


def collect_repository_files(repository_root: Path,
                           list_root: Iterable[str] = ("",),
                           *,
                           suffixes: Iterable[str] = (),
                           file_names: Iterable[str] = (),
                           excluded_dir_names: Iterable[str] = NOT_OUR_DIR_NAMES,
                           any_suffix: bool = False) -> list[Path]:
    """
    저장소의 `listRoot`(저장소 기준 경로, 비우면 전체) 아래에서 확장자가 `suffixes` 이거나 이름이 `file_names` 인 파일을 모읍니다
    (`any_suffix` 면 확장자를 보지 않는다 — 리소스 폴더처럼 파일 종류를 가리지 않는 검사).

    `excluded_dir_names` 의 폴더로는 **내려가지 않습니다**(걷는 중에 가지를 친다). `Path.rglob` 로 전부 걸은 뒤 거르면 빌드 트리와
    내려받은 외부 도구까지 다 걷는다. 정렬된 목록을 돌려줍니다.
    """
    set_suffix = {suffix.lower() for suffix in suffixes}
    set_file_name = set(file_names)
    set_excluded = set(excluded_dir_names)
    result_set: set[Path] = set()
    for rel_root in list_root:
        base_dir = repository_root / rel_root if rel_root else repository_root
        if not base_dir.is_dir():
            continue
        for current, list_dir_name, list_file_name in os.walk(base_dir):
            list_dir_name[:] = [name for name in list_dir_name
                              if name not in set_excluded and _is_generated_dir(repository_root, Path(current) / name) == False]
            for file_name in list_file_name:
                if any_suffix or file_name in set_file_name or os.path.splitext(file_name)[1].lower() in set_suffix:
                    result_set.add(Path(current) / file_name)
    return sorted(result_set)


def resolve_file_arguments(repository_root: Path,
                         list_file_argument: Iterable[str],
                         *,
                         list_scan_root: Iterable[str] = ("",),
                         suffixes: Iterable[str] = (),
                         file_names: Iterable[str] = (),
                         excluded_dir_names: Iterable[str] = NOT_OUR_DIR_NAMES,
                         any_suffix: bool = False) -> list[Path]:
    """
    `--files` 로 받은 경로를 대상 파일로 풉니다 — 게이트 · 픽서 · 훅이 **같은 규칙**을 쓴다(`collect_repository_files` 의 걷기 규칙과 같다).

    - 상대 경로는 **저장소 루트 기준**으로 풀고, 거기 없으면 현재 폴더 기준으로 푼다. 저장소 밖 파일 · 없는 파일은 뺀다.
    - 확장자가 `suffixes` 이거나 이름이 `file_names` 인 파일만(`any_suffix` 면 모두).
    - `excluded_dir_names` 의 폴더 아래 · `list_scan_root`(저장소 기준, `""` 은 전체) 밖은 뺀다.
    """
    set_suffix = {suffix.lower() for suffix in suffixes}
    set_file_name = set(file_names)
    set_excluded = set(excluded_dir_names)
    list_scan_prefix = [root.rstrip("/") + "/" for root in list_scan_root if root]
    result_set: set[Path] = set()
    for item in list_file_argument:
        path = Path(item)
        if not path.is_absolute():
            candidate = repository_root / path
            path = candidate if candidate.exists() else path.resolve()
        path = path.resolve()
        if not path.is_file() or not (any_suffix or path.name in set_file_name or path.suffix.lower() in set_suffix):
            continue
        try:
            relative = path.relative_to(repository_root.resolve()).as_posix()
        except ValueError:
            continue
        if any(part in set_excluded for part in relative.split("/")[:-1]):
            continue
        if any(relative.startswith(generated + "/") for generated in GENERATED_REL_DIRS):
            continue
        if list_scan_prefix and not any(relative.startswith(prefix) for prefix in list_scan_prefix):
            continue
        result_set.add(path)
    return sorted(result_set)


def get_or_find_cached(existing: dict[str, Any],
                    key: str,
                    find_func: Callable[..., Any],
                    *args: Any,
                    validate: Any | None = None) -> Any:
    """
    설정 파일에서 기존에 캐시된 값을 확인하고, 유효하지 않으면 탐색 함수(find_func)를 실행하여 새 값을 찾아 반환합니다.
    """
    cached_value = existing.get(key)
    if isinstance(cached_value, list) and cached_value:
        return cached_value
    if cached_value and isinstance(cached_value, str):
        is_valid_value = validate(cached_value) if callable(validate) else Path(cached_value).exists()
        if is_valid_value:
            return cached_value
    elif cached_value and not isinstance(cached_value, str):
        return cached_value
    return find_func(*args)


def find_first_existing_file(search_dirs: list[Path], file_names: list[str]) -> str:
    """
    지정된 디렉터리 목록을 순서대로 확인하여, 후보 파일명 중 가장 먼저 발견된 파일의 절대 경로를 반환합니다.
    """
    for directory in search_dirs:
        if not directory.is_dir():
            continue
        for file_name in file_names:
            if (candidate := directory / file_name).is_file():
                return str(candidate)
    return ""


def find_first_existing_file_recursive(search_dirs: list[Path], file_names: list[str]) -> str:
    """
    지정된 디렉터리들을 재귀적으로 탐색하여 일치하는 파일 경로를 찾습니다.
    (단, vcpkg의 buildtrees, downloads, packages 등 임시 빌드 폴더는 탐색에서 건너뜁니다.)
    """
    for directory in search_dirs:
        if not directory.is_dir():
            continue
        for file_name in file_names:
            try:
                for candidate in directory.rglob(file_name):
                    if not candidate.is_file():
                        continue
                    if any(part in _RGLOB_SKIP_DIR_NAMES for part in candidate.parts):
                        continue
                    return str(candidate)
            except (OSError, PermissionError):
                pass
    return ""


def iter_search_root_templates(templates: Iterable[str],
                            *,
                            extras: dict[str, str] | None = None,
                            skip: Path | None = None) -> Iterator[Path]:
    """
    경로 템플릿 목록(${sourceDir}, 환경변수 등)을 실제 절대 경로로 치환하고, 실제로 존재하는 디렉터리만 순회(yield)합니다.
    """
    skip_resolved: Path | None = None
    if skip is not None:
        try:
            skip_resolved = skip.resolve()
        except OSError:
            skip_resolved = None

    for template in templates:
        root = Path(expand_path_template(str(template), extras=extras))
        if not root.exists():
            continue
        if skip_resolved is not None:
            try:
                if root.resolve() == skip_resolved:
                    continue
            except OSError:
                pass
        yield root


def find_first_valid_root(templates: Iterable[str],
                       is_valid: Callable[[Path], bool],
                       *,
                       extras: dict[str, str] | None = None,
                       skip: Path | None = None) -> Path | None:
    """
    경로 템플릿 목록에서 유효성 검사 함수(isValid)를 통과하는 첫 번째 디렉터리의 절대 경로를 반환합니다.
    """
    return next(
        (
            root.resolve()
            for root in iter_search_root_templates(templates, extras=extras, skip=skip)
            if is_valid(root)
        ),
        None,
    )


def natural_version_key(text: str) -> list[Any]:
    """`llvm-9` 와 `llvm-21` 을 사전순이 아니라 숫자로 비교하기 위한 정렬 키입니다."""
    parts: list[Any] = []
    digits = ""
    for ch in text:
        if ch.isdigit():
            digits += ch
            continue
        if digits:
            parts.append(int(digits))
            digits = ""
        parts.append(ch)
    if digits:
        parts.append(int(digits))
    # int 와 str 이 섞이면 비교가 터진다 — 종류를 앞에 붙여 같은 종류끼리만 비교되게 한다.
    return [(0, v, "") if isinstance(v, int) else (1, 0, v) for v in parts]


def select_latest_version(list_version: list[str]) -> str:
    """버전 폴더 이름들 중 최신(자연순 — `14.44.x` 가 `14.9.x` 보다 새것). 비면 빈 문자열."""
    return max(list_version, key=natural_version_key, default="")


def _expand_search_root_globs(roots: list[str]) -> list[str]:
    """
    `*` 가 든 절대 경로 항목을 실제로 존재하는 디렉터리들로 펼칩니다.

    손으로 `/usr/lib/llvm-20 … llvm-14` 를 나열해 두면 배포판이 새 메이저를 올릴 때마다 목록
    밖으로 조용히 비켜간다(예: 새로 나온 llvm-21). 펼친 결과는 **높은 버전이 먼저** 오도록
    자연순 내림차순으로 둔다 — 먼저 찾은 것을 쓰는 탐색이므로 최신이 앞이어야 한다.

    `${sourceDir}` 같은 템플릿이 든 항목은 여기서 건드리지 않는다 — 확장은 뒷단이 한다.
    """
    expanded: list[str] = []
    for root in roots:
        if "*" not in root or "${" in root:
            expanded.append(root)
            continue
        try:
            matches = [str(m) for m in Path("/").glob(root.lstrip("/")) if m.is_dir()] if root.startswith("/") \
                else [str(m) for m in Path(".").glob(root) if m.is_dir()]
        except OSError:
            matches = []
        expanded.extend(sorted(matches, key=natural_version_key, reverse=True))
    return expanded


def platform_search_roots(search: dict[str, Any], roots_key: str) -> list[str]:
    """
    설정 파일(search_paths.json)에서 현재 OS 플랫폼(windows, linux)에 해당하는 탐색 경로 목록을 가져옵니다.

    `*` 가 든 항목은 실제 디렉터리로 펼쳐진다(자연순 내림차순).
    """
    raw = search[roots_key]   # search_paths.defaults 의 필수 키 — 플랫폼 표면 이 플랫폼 칸도 필수
    if isinstance(raw, dict):
        return _expand_search_root_globs(list(raw[platform_key()]))
    if isinstance(raw, list):
        return _expand_search_root_globs([str(item) for item in raw])
    return []


def is_vcpkg_root(path: Path | str) -> bool:
    """
    vcpkg.cmake 파일의 존재 여부를 확인하여, 지정된 경로가 올바른 vcpkg 설치 루트인지 검사합니다.
    """
    try:
        return (Path(path) / "scripts" / "buildsystems" / "vcpkg.cmake").is_file()
    except OSError:
        return False
