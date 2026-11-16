"""
Scripts/setup/toolchain_detect.py

도구 · SDK 경로 탐색. 두 층이다.

1. 선언적 도구 탐색(`ToolSpec` · `find_tool_root` — Ninja · LLVM · vcpkg 가 같이 쓴다): OS 환경 변수 → 시스템 PATH →
   search_paths.json 탐색 루트 → 프로젝트 로컬 `Tools/<subdir>` 키트. 없으면 부르는 쪽이 부트스트랩 · 다운로드한다.
2. 호스트 SDK/컴파일러 경로 탐색(MSVC, Windows SDK, DXC, vcpkg installed, Ninja 설치).
"""

from __future__ import annotations

import logging
import os
import shutil
import sys
from collections.abc import Callable
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from common.file_collect import find_first_valid_root, platform_search_roots, select_latest_version
from common.paths import normalize_path, platform_key
from common.host_platform import is_windows_host
from common.project_constants import KEY_NINJA_DOWNLOAD_URLS, KEY_NINJA_SEARCH_ROOTS, KEY_NINJA_TOOLS_SUBDIR
from common.run_process import run_process
from common.tool_download import ensure_cached_download, extract_zip_safe, resolve_tools_subdir, tools_cache_dir
from common.toolchain_config import load_search_paths, record_engine_path


@dataclass(frozen=True)
class ToolSpec:
    """
    외부 빌드/호스트 도구의 탐색 및 설정 규격을 정의하는 데이터클래스입니다.

    Attributes:
        name: 도구 표시명 (예: 'Ninja', 'LLVM', 'vcpkg').
        tools_subdir_key: search_paths 내 로컬 서브디렉터리 키 (예: 'ninja_tools_subdir').
        search_roots_key: search_paths 내 탐색 루트 배열 키 (예: 'ninja_search_roots').
        bin_names: 시스템 PATH 상에서 탐색할 실행 파일 이름 튜플 (예: ('ninja.exe', 'ninja')).
        env_vars: 우선 검사할 환경 변수 이름 튜플 (예: ('VCPKG_ROOT',)).
        download_urls_key: 자동 다운로드 URL 딕셔너리 키 (선택 사항).
        auto_bootstrap_key: search_paths 내 자동 부트스트랩 활성화 키 (선택 사항).
        env_auto_bootstrap_key: 환경 변수 자동 부트스트랩 오버라이드 키 (선택 사항).
        validate_func: 특정 디렉터리가 해당 도구의 유효한 루트인지 검증하는 콜백.
    """

    name: str
    tools_subdir_key: str
    search_roots_key: str
    bin_names: tuple[str, ...] = ()
    env_vars: tuple[str, ...] = ()
    download_urls_key: str | None = None
    auto_bootstrap_key: str | None = None
    env_auto_bootstrap_key: str | None = None
    validate_func: Callable[[Path], bool] | None = None


def find_tool_root(
    spec: ToolSpec,
    search: dict[str, Any] | None = None,
    *,
    logger: logging.Logger | None = None,
) -> Path | None:
    """
    선언된 ToolSpec에 따라 환경변수 -> 시스템 PATH -> search_paths -> 로컬 Tools 디렉터리 순서로
    도구의 설치 루트 디렉터리를 탐색합니다.

    Args:
        spec: 도구 명세 (ToolSpec).
        search: search_paths 딕셔너리 (None일 경우 load_search_paths() 호출).
        logger: 로깅에 사용할 Logger 객체 (선택 사항).

    Returns:
        유효한 도구 루트 Path 객체. 찾지 못한 경우 None.
    """
    search_config = search if search is not None else load_search_paths()
    log = logger or logging.getLogger(spec.name)
    tools_dir = resolve_tools_subdir(spec.tools_subdir_key, search_config)
    extras = {spec.tools_subdir_key: search_config[spec.tools_subdir_key]}

    def is_valid(candidate: Path) -> bool:
        if spec.validate_func is not None:
            return spec.validate_func(candidate)
        return candidate.is_dir()

    # 1. 환경변수 확인
    for env_var in spec.env_vars:
        if (env_val := os.environ.get(env_var)) and is_valid(Path(env_val)):
            log.info(f"[{spec.name}] Using {env_var}: {env_val}")
            return Path(normalize_path(env_val))

    # 2. 시스템 PATH 상의 바이너리 확인
    for bin_name in spec.bin_names:
        if found_bin := shutil.which(bin_name):
            bin_path = Path(found_bin).resolve()
            for parent_candidate in (bin_path.parent, bin_path.parent.parent):
                if is_valid(parent_candidate):
                    log.info(f"[{spec.name}] Using PATH: {parent_candidate}")
                    return Path(normalize_path(str(parent_candidate)))

    # 3. search_paths.json 탐색 루트 확인
    found = find_first_valid_root(
        platform_search_roots(search_config, spec.search_roots_key),
        is_valid,
        extras=extras,
        skip=tools_dir,
    )
    if found:
        log.info(f"[{spec.name}] Using search root: {found}")
        return Path(normalize_path(str(found)))

    # 4. 로컬 Tools/<subdir> 키트 확인
    if is_valid(tools_dir):
        log.info(f"[{spec.name}] Using project kit: {tools_dir}")
        return tools_dir

    return None


# ==============================================================================
# 호스트 SDK / 컴파일러 / 빌드 도구 경로
# ==============================================================================

@lru_cache(maxsize=1)
def find_msvc_path() -> str:
    """
    시스템에 설치된 최신 MSVC(Microsoft Visual C++) 툴체인 경로를 탐색하여 반환합니다.
    (Windows 환경에서 vswhere를 사용)

    Returns:
        MSVC VC/Tools/MSVC 하위의 최신 버전 디렉터리 경로. 찾지 못하면 빈 문자열.
    """
    if not is_windows_host():
        return ""
    if (env_vc_tools := os.environ.get("VCToolsInstallDir")) and Path(env_vc_tools).exists():
        return normalize_path(env_vc_tools)

    program_files = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if not program_files:
        return ""
    vswhere = Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return ""

    command = [
        str(vswhere), "-latest", "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools",
        "-property", "installationPath",
    ]
    result = run_process(command)
    if not result.succeeded:
        # "설치 안 됨" 과 "찾다가 실패" 를 가른다 — 돌려주는 값은 둘 다 빈 문자열이지만 configure 로그에 이유가 남는다.
        print(f"[ToolchainDetect] MSVC 위치를 vswhere 로 찾지 못했습니다(종료 코드 {result.return_code}): {result.stderr.strip()}", file=sys.stderr)
        return ""
    vs_path = result.stdout.strip()
    if not vs_path:
        return ""
    msvc_base = Path(vs_path) / "VC" / "Tools" / "MSVC"
    try:
        latest_version = select_latest_version([entry.name for entry in msvc_base.iterdir() if entry.is_dir()]) if msvc_base.is_dir() else ""
    except OSError as error:
        print(f"[ToolchainDetect] MSVC 버전 폴더를 읽지 못했습니다: {msvc_base}: {error}", file=sys.stderr)
        return ""
    return normalize_path(msvc_base / latest_version) if latest_version else ""


@lru_cache(maxsize=1)
def find_windows_sdk_path() -> tuple[str, str]:
    """
    시스템에 설치된 최신 Windows 10/11 SDK 경로와 버전을 레지스트리에서 탐색하여 반환합니다.

    Returns:
        (SDK 설치 경로, SDK 버전) 튜플. (Windows 환경이 아니거나 찾지 못하면 빈 문자열 튜플)
    """
    if not is_windows_host():
        return "", ""
    # 개발자 셸의 두 변수가 함께 있을 때만 쓴다 — 버전 없이 폴더만 돌려주면 쓰는 쪽이 판을 모른다. 아니면 레지스트리가 정본이다.
    env_sdk_dir, env_sdk_version = os.environ.get("WindowsSdkDir"), os.environ.get("WindowsSDKVersion")
    if env_sdk_dir and env_sdk_version and Path(env_sdk_dir).exists():
        return normalize_path(env_sdk_dir), env_sdk_version.strip("\\")
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
            kits_root, _ = winreg.QueryValueEx(key, "KitsRoot10")
        include_dir = Path(kits_root) / "Include"
        latest_version = select_latest_version([entry.name for entry in include_dir.iterdir() if entry.name.startswith("10.")]) if kits_root and include_dir.is_dir() else ""
    except OSError as error:
        # 레지스트리 키가 없거나 폴더를 못 읽음 — "설치 안 됨" 과 구별되도록 이유를 남긴다.
        print(f"[ToolchainDetect] Windows SDK 위치를 레지스트리에서 찾지 못했습니다: {error}", file=sys.stderr)
        return "", ""
    return (normalize_path(kits_root), latest_version) if latest_version else ("", "")


# ==============================================================================
# 빌드 도구 탐색 및 다운로드 (Ninja)
# ==============================================================================

def _setup_build_tool(tool_name: str,
                           exe_name: str,
                           subdir_key: str,
                           search_roots_key: str,
                           download_urls_key: str) -> str:
    """도구 탐색 및 다운로드(zip 으로 받아 `Tools/<이름>` 에 푼다)."""
    import logging
    logger = logging.getLogger("Environment")
    logger.info(f"[{tool_name}] Checking {tool_name}...")

    search = load_search_paths()
    tools_dir = resolve_tools_subdir(subdir_key, search)
    local_exe_path = tools_dir / exe_name

    def _is_usable(root: Path) -> bool:
        exe_path = root if root.is_file() else root / exe_name
        return exe_path.is_file()

    spec = ToolSpec(
        name=tool_name,
        tools_subdir_key=subdir_key,
        search_roots_key=search_roots_key,
        bin_names=(exe_name, exe_name.replace(".exe", "") if is_windows_host() else exe_name),
        validate_func=_is_usable,
    )
    if found := find_tool_root(spec, search, logger=logger):
        resolved_exe = found if found.is_file() else (found / exe_name if (found / exe_name).is_file() else found)
        return record_engine_path(f"{tool_name.lower()}_path", resolved_exe)

    url = search[download_urls_key][platform_key()]   # search_paths.defaults 의 필수 키 — 없으면 KeyError

    logger.info(f"[{tool_name}] Downloading into {tools_dir}")
    tools_dir.mkdir(parents=True, exist_ok=True)
    archive_name = url.rsplit("/", 1)[-1]
    archive_path = ensure_cached_download(url, tools_cache_dir() / archive_name, min_size=50_000, label=tool_name)

    # 받은 압축을 풀다 실패하면 "도구 없음" 이 아니라 오류다 — 예외 그대로 멈춘다.
    extract_zip_safe(archive_path, tools_dir)
    if not local_exe_path.is_file():
        raise RuntimeError(f"[{tool_name}] 압축을 풀었지만 {local_exe_path} 가 없습니다: {archive_path}")
    if not is_windows_host():
        os.chmod(local_exe_path, 0o755)
    logger.info(f"[{tool_name}] Installed: {local_exe_path}")
    return record_engine_path(f"{tool_name.lower()}_path", local_exe_path)


def setup_ninja() -> str:
    """시스템 PATH 또는 search_paths.json에서 Ninja 빌드 도구를 탐색하며, 없을 경우 다운로드하여 Tools/Ninja에 설치합니다."""
    exe_name = "ninja.exe" if is_windows_host() else "ninja"

    return _setup_build_tool("SetupNinja", exe_name, KEY_NINJA_TOOLS_SUBDIR, KEY_NINJA_SEARCH_ROOTS, KEY_NINJA_DOWNLOAD_URLS)
