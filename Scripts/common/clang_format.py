"""
Scripts/common/clang_format.py

clang-format 을 찾고, 고정 버전이 없으면 설치합니다. **포맷을 돌리는 쪽과 설치하는 쪽이 같은 규칙을 쓰도록 한 곳에 둡니다.**

주의: 두 쪽이 규칙을 따로 들면 포맷하는 쪽이 toolchain 의 LLVM 경로나 PATH 를 **버전 확인 없이** 먼저 쓰게 된다 —
toolchain 이 시스템 LLVM(21.x)을 가리키는 PC 는 고정 버전(`Tools/LLVM/bin`, 20.1.8)이 설치돼 있어도 다른 판으로 포맷한다.
그래서 고정본을 먼저 보는 규칙이 여기 하나뿐이고, `setup` 도 이 모듈을 부른다(`common` 은 `setup` 을 import 하지 않는다).

  resolve_clang_format()   포맷을 돌릴 clang-format — 고정본, 없으면 설치, 그래도 없으면 RuntimeError
  ensure_clang_format()    설치까지 하는 같은 규칙(부트스트랩이 부른다)

고정본을 못 구했을 때 "아무 clang-format" 으로 대신하지 않는다 — 판이 다르면 포맷이 달라져 두 PC 의 커밋이 서로를 되돌린다.
"""

from __future__ import annotations

import json
import platform
import shutil
import sys
from pathlib import Path

from .tool_download import ensure_cached_download, resolve_tools_subdir, tools_cache_dir
from .toolchain_config import load_search_paths, load_toolchain_config
from .project_constants import KEY_CLANG_FORMAT_VERSION, KEY_LLVM_PATH, KEY_LLVM_TOOLS_SUBDIR
from .paths import normalize_path
from .host_platform import is_windows_host
from .run_process import run_process

_CLANG_FORMAT_WIN = "clang-format.exe"
_CLANG_FORMAT_POSIX = "clang-format"


def _clang_format_file_name() -> str:
    return _CLANG_FORMAT_WIN if is_windows_host() else _CLANG_FORMAT_POSIX


def pinned_clang_format_version() -> str:
    """search_paths.json 이 고정한 clang-format 버전입니다(필수 키 — 없으면 KeyError)."""
    return str(load_search_paths()[KEY_CLANG_FORMAT_VERSION]).strip()


def clang_format_version_of(path: Path | str) -> str:
    """`clang-format --version` 이 찍는 버전 문자열을 뽑습니다. 못 뜨면 빈 문자열."""
    completed = run_process([path, "--version"], timeout_seconds=20)
    if not completed.succeeded:
        return ""
    for token in (completed.stdout or "").split():
        if token and token[0].isdigit():
            return token
    return ""


def find_clang_format_path(llvm_path: str = "") -> str:
    """
    고정 버전으로 설치해 둔 clang-format 을 찾습니다 (주어진 LLVM 경로, 그다음 Tools/LLVM/bin).

    **PATH 의 clang-format 은 마지막 수단이다.** 그 버전은 PC 마다 다르고, clang-format 은 버전이
    다르면 결과가 달라진다 — 실제로 18 과 20 은 이 저장소 400 파일 중 2 개를 다르게 포맷한다
    (동아시아 문자 폭 계산이 달라져 한글 주석 정렬이 갈린다). 포매터만은 "설치된 것" 이 아니라
    "정해진 것" 을 써야 두 PC 의 커밋이 서로를 되돌리지 않는다.
    """
    name = _clang_format_file_name()
    want = pinned_clang_format_version()

    list_candidate: list[Path] = []
    if llvm_path:
        list_candidate.append(Path(llvm_path) / "bin" / name)
    list_candidate.append(resolve_tools_subdir(KEY_LLVM_TOOLS_SUBDIR, load_search_paths()) / "bin" / name)

    for candidate in list_candidate:
        if candidate.is_file() and clang_format_version_of(candidate) == want:
            return normalize_path(candidate)
    return ""


def _resolve_clang_format_wheel_url(version: str) -> str:
    """
    PyPI 에서 이 플랫폼용 clang-format 휠 URL 을 찾습니다.

    LLVM 공식 릴리스는 전체 배포판만 올린다 — clang-format 하나를 얻자고 335 MB(리눅스 18) 나
    2 GB(20.1.8 의 `LLVM-*-Linux-X64.tar.xz`) 를 받아야 했고, 게다가 그 바이너리는 빌드된 배포판의
    `libtinfo.so.5` 를 요구해 요즘 리눅스에서는 실행조차 안 됐다. PyPI 의 `clang-format` 패키지는
    **바이너리 하나만** 담은 휠(약 1.4~1.7 MB)을 플랫폼별로 내고 버전이 LLVM 릴리스를 그대로 따른다.

    URL 에 해시가 들어 있어 손으로 적어 둘 수 없으므로 버전만 고정하고 URL 은 여기서 받아 온다.
    """
    import urllib.request  # PyPI 를 물을 때만(ToolDownload.downloadUrl 과 같은 이유)
    if not version:
        return ""

    machine = platform.machine().lower()
    arm = machine in ("arm64", "aarch64")
    if is_windows_host():
        list_tag = ["win_amd64"] if machine.endswith("64") else ["win32"]
    else:
        list_tag = ["manylinux", "aarch64"] if arm else ["manylinux", "x86_64"]

    url = f"https://pypi.org/pypi/clang-format/{version}/json"
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (OSError, ValueError) as error:
        sys.stderr.write(f"[ClangFormat] PyPI 조회 실패 ({url}): {error}\n")
        return ""

    for entry in payload["urls"]:
        file_name = str(entry["filename"])
        if not file_name.endswith(".whl"):
            continue
        if all(tag in file_name for tag in list_tag):
            return str(entry["url"])
    return ""


def _install_clang_format_from_wheel(wheel: Path, dest_bin: Path) -> bool:
    """휠(zip)에서 clang-format 실행 파일만 dest_bin 으로 꺼냅니다."""
    import zipfile  # 휠을 풀 때만
    want_name = _clang_format_file_name()
    dest_bin.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(wheel) as archive:
            for member in archive.namelist():
                if member.endswith("/"):
                    continue
                if Path(member).name != want_name:
                    continue
                out_path = dest_bin / want_name
                with archive.open(member) as source, open(out_path, "wb") as out_file:
                    shutil.copyfileobj(source, out_file)
                if not is_windows_host():
                    out_path.chmod(out_path.stat().st_mode | 0o111)
                print(f"[ClangFormat] Installed {out_path}", file=sys.stderr)
                return True
    except (OSError, zipfile.BadZipFile) as error:
        sys.stderr.write(f"[ClangFormat] clang-format 휠을 열 수 없습니다: {error}\n")
    return False


def ensure_clang_format(llvm_path: str = "", *, allow_download: bool = True) -> str:
    """
    `clang_format_version` 이 고정한 clang-format 을 확보합니다 (없으면 PyPI 휠로 설치).

    Returns:
        clang-format 실행 파일의 절대 경로. 고정본을 찾지도 설치하지도 못하면 RuntimeError.
    """
    if found := find_clang_format_path(llvm_path):
        return found

    version = pinned_clang_format_version()
    if not version:
        raise RuntimeError(f"[ClangFormat] search_paths 의 {KEY_CLANG_FORMAT_VERSION} 이 비어 있습니다")

    if allow_download:
        dest_bin = resolve_tools_subdir(KEY_LLVM_TOOLS_SUBDIR, load_search_paths()) / "bin"
        if url := _resolve_clang_format_wheel_url(version):
            wheel = ensure_cached_download(url, tools_cache_dir() / Path(url).name, label="clang-format")
            if _install_clang_format_from_wheel(wheel, dest_bin):
                if found := find_clang_format_path(llvm_path):
                    return found
                sys.stderr.write(f"[ClangFormat] 설치한 clang-format 이 {version} 이 아닙니다.\n")
        else:
            sys.stderr.write(f"[ClangFormat] clang-format {version} 휠을 이 플랫폼에서 찾지 못했습니다.\n")

    raise RuntimeError(f"[ClangFormat] 고정 버전 clang-format {version} 을 찾지도 설치하지도 못했습니다 "
                       f"(위 줄이 이유다) — 'py -3 Scripts/setup/environment.py' 로 설치하십시오")


def resolve_clang_format(llvm_path: str = "", *, allow_download: bool = True) -> str:
    """
    포맷을 돌릴 clang-format 실행 파일 경로입니다. `ensure_clang_format` 과 같은 규칙이고, LLVM 경로를 주지 않으면
    toolchain_config.json 의 것을 씁니다.

    `.clang-format` 은 규칙만 고정하지 **도구 버전은 고정하지 못합니다.** 한쪽에서 커밋한 줄을 다른 쪽 pre-commit 훅이
    거부하고, 고쳐서 올리면 원래 PC 가 다시 되돌리는 왕복이 생깁니다(판마다 이어붙인 줄의 정렬 칸 수가 다릅니다).
    """
    if not llvm_path:
        cache = load_toolchain_config()
        # 캐시가 없으면 LLVM 경로를 모른다 — 빈 경로는 고정본 탐색 · 설치로 넘어간다(ensure_clang_format).
        llvm_path = "" if cache is None else str(cache.get(KEY_LLVM_PATH, "") or "")
    return ensure_clang_format(llvm_path, allow_download=allow_download)
