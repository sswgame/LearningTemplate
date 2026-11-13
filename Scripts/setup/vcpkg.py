#!/usr/bin/env python3
"""
Scripts/setup/vcpkg.py

vcpkg: VCPKG_ROOT → PATH → vcpkg_search_roots → Tools/vcpkg → opt-in clone.

  python3 Scripts/setup/vcpkg.py
  python3 Scripts/setup/vcpkg.py --install
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.tool_download import resolve_tools_subdir
from common.toolchain_config import auto_bootstrap_enabled, load_search_paths, record_engine_path
from common.project_constants import (
    ENV_SW_VCPKG_AUTO_BOOTSTRAP,
    KEY_VCPKG_AUTO_BOOTSTRAP,
    KEY_VCPKG_GIT_COMMIT,
    KEY_VCPKG_GIT_URL,
    KEY_VCPKG_ROOT,
    KEY_VCPKG_SEARCH_ROOTS,
    KEY_VCPKG_TOOLS_SUBDIR,
)
from common.git import ensure_git_on_path, run_git
from common.paths import platform_key, platform_script_command
from common.run_process import run_process
from common.file_collect import is_vcpkg_root
from setup.toolchain_detect import ToolSpec, find_tool_root
from common.exit_code import ExitCode  # noqa: E402

VCPKG_TOOL_SPEC = ToolSpec(
    name="Vcpkg",
    tools_subdir_key=KEY_VCPKG_TOOLS_SUBDIR,
    search_roots_key=KEY_VCPKG_SEARCH_ROOTS,
    bin_names=("vcpkg.exe", "vcpkg"),
    env_vars=("VCPKG_ROOT", "VCPKG_INSTALLATION_ROOT"),
    validate_func=is_vcpkg_root,
)


def _resolve_vcpkg_commit(search: dict) -> str:
    """
    Tools/vcpkg 를 고정할 커밋을 정합니다. `search_paths.json` 의 `vcpkg_git_commit` 이 있으면 그것이고, 비어 있으면
    저장소 루트 `vcpkg.json` 의 `builtin-baseline` 입니다 — 기준선보다 오래된 체크아웃은 configure 가 "no version database entry" 로 진다.
    """
    explicit = str(search.get(KEY_VCPKG_GIT_COMMIT, "")).strip()
    if explicit:
        return explicit
    # 고정본의 정본은 vcpkg.json 의 builtin-baseline 이다 — 못 읽거나 없으면 멈춘다(빈 값이면 최신 vcpkg 로 조용히 간다).
    manifest_path = Path(__file__).resolve().parents[2] / "vcpkg.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    return str(manifest["builtin-baseline"]).strip()


def _pin_vcpkg_commit(vcpkg_root: Path, commit: str) -> None:
    """
    Tools/vcpkg 디렉터리가 Git 워크트리인 경우, search_paths.json에 지정된 특정 커밋(vcpkg_git_commit)으로 체크아웃합니다.
    (단, 시스템 환경변수 VCPKG_ROOT나 PATH로 탐색된 외부 트리는 수정하지 않습니다.)
    """
    commit = (commit or "").strip()
    if not commit:
        return
    git_probe = run_git(["-C", str(vcpkg_root), "rev-parse", "--is-inside-work-tree"])
    if git_probe.return_code != 0 or git_probe.stdout.strip() != "true":
        print(f"[Vcpkg] Skip pin (not a git work tree): {vcpkg_root}", file=sys.stderr)
        return

    head = run_git(["-C", str(vcpkg_root), "rev-parse", "HEAD"])
    resolved = run_git(["-C", str(vcpkg_root), "rev-parse", commit])
    if (
        head.return_code == 0
        and resolved.return_code == 0
        and head.stdout.strip() == resolved.stdout.strip()
    ):
        return

    print(f"[Vcpkg] Pinning {vcpkg_root} to {commit}", file=sys.stderr)
    checkout = run_git(["-C", str(vcpkg_root), "checkout", "--detach", commit])
    if checkout.return_code == 0:
        return
    fetch = run_git(["-C", str(vcpkg_root), "fetch", "--depth", "1", "origin", commit])
    if fetch.return_code != 0:
        sys.stderr.write(
            f"[Vcpkg] Could not fetch {commit} ({fetch.stderr.strip() or checkout.stderr.strip()})\n"
        )
        return
    checkout = run_git(["-C", str(vcpkg_root), "checkout", "--detach", commit])
    if checkout.return_code != 0:
        sys.stderr.write(f"[Vcpkg] checkout {commit} failed: {checkout.stderr.strip()}\n")


def _remove_incomplete_vcpkg_tree(tools_dir: Path) -> None:
    """다운로드나 부트스트랩 도중 중단되어 불완전하게 남은 Tools/vcpkg 디렉터리를 삭제하여 정리합니다."""
    if not tools_dir.exists():
        return
    if is_vcpkg_root(tools_dir):
        return
    print(f"[Vcpkg] Removing incomplete vcpkg tree: {tools_dir}", file=sys.stderr)
    shutil.rmtree(tools_dir, ignore_errors=True)


def has_vcpkg_executable(vcpkg_root: Path) -> bool:
    """
    이 OS 의 vcpkg 실행 파일(윈도우 `vcpkg.exe`, 그 밖 `vcpkg`)이 루트에 있는지 봅니다.

    `is_vcpkg_root` 는 `vcpkg.cmake` 만 본다 — 윈도우에서 부트스트랩한 트리를 리눅스에서 쓰면 그 파일은 있지만 리눅스 실행 파일은 없다.
    """
    return (vcpkg_root / ("vcpkg.exe" if platform_key() == "windows" else "vcpkg")).is_file()


def _run_bootstrap_script(vcpkg_root: Path) -> bool:
    """이미 있는 vcpkg 트리에서 이 OS 의 bootstrap 스크립트만 돌립니다(클론 · 지우기 없음). 성공하면 True 입니다."""
    bootstrap = vcpkg_root / ("bootstrap-vcpkg.bat" if platform_key() == "windows" else "bootstrap-vcpkg.sh")
    if not bootstrap.is_file():
        sys.stderr.write(f"[Vcpkg Error] bootstrap script missing: {bootstrap}\n")
        return False
    print(f"[Vcpkg] Running {bootstrap.name} in {vcpkg_root} (no vcpkg executable for this OS)...", file=sys.stderr)
    process = run_process(platform_script_command(bootstrap), cwd=vcpkg_root)
    if process.return_code != 0 or not has_vcpkg_executable(vcpkg_root):
        sys.stderr.write(f"[Vcpkg Error] bootstrap failed (exit {process.return_code}):\n"
                         f"{process.stderr.strip() or process.stdout.strip()}\n")
        return False
    return True


def _bootstrap_vcpkg(tools_dir: Path, git_url: str, git_commit: str, git_exe: str) -> bool:
    """
    vcpkg Git 저장소를 Tools/vcpkg로 클론하고 bootstrap 스크립트를 실행하여 빌드 환경을 구축합니다.

    Returns:
        부트스트랩 성공 시 True, 실패 시 False
    """
    tools_dir.parent.mkdir(parents=True, exist_ok=True)
    _remove_incomplete_vcpkg_tree(tools_dir)

    print(f"[Vcpkg] Cloning into {tools_dir}...", file=sys.stderr)
    print(f"[Vcpkg] Using git: {git_exe}", file=sys.stderr)

    clone = run_git(["clone", git_url, str(tools_dir)])
    if clone.return_code != 0:
        sys.stderr.write(
            f"[Vcpkg Error] git clone failed:\n{clone.stderr.strip() or clone.stdout.strip()}\n"
        )
        _remove_incomplete_vcpkg_tree(tools_dir)
        return False

    if git_commit:
        checkout = run_git(["-C", str(tools_dir), "checkout", "--detach", git_commit])
        if checkout.return_code != 0:
            sys.stderr.write(
                f"[Vcpkg Error] checkout {git_commit} failed:\n{checkout.stderr.strip()}\n"
            )
            _remove_incomplete_vcpkg_tree(tools_dir)
            return False

    bootstrap_name = "bootstrap-vcpkg.bat" if platform_key() == "windows" else "bootstrap-vcpkg.sh"
    bootstrap = tools_dir / bootstrap_name
    bootstrap_cmd = platform_script_command(bootstrap)

    if not bootstrap.is_file():
        sys.stderr.write(f"[Vcpkg Error] bootstrap script missing: {bootstrap}\n")
        _remove_incomplete_vcpkg_tree(tools_dir)
        return False

    print(f"[Vcpkg] Running {bootstrap.name}...", file=sys.stderr)
    bootstrap_process = run_process(bootstrap_cmd, cwd=tools_dir)
    if bootstrap_process.return_code != 0:
        sys.stderr.write(
            f"[Vcpkg Error] bootstrap failed (exit {bootstrap_process.return_code}):\n"
            f"{bootstrap_process.stderr.strip() or bootstrap_process.stdout.strip()}\n"
        )
        _remove_incomplete_vcpkg_tree(tools_dir)
        return False

    if not is_vcpkg_root(tools_dir):
        sys.stderr.write("[Vcpkg Error] bootstrap finished but vcpkg.cmake is missing.\n")
        _remove_incomplete_vcpkg_tree(tools_dir)
        return False
    return True


def setup_vcpkg(allow_bootstrap: bool = False) -> Path | None:
    """
    시스템 또는 프로젝트 내에서 vcpkg 경로를 탐색하고, 필요한 경우 새로 부트스트랩합니다.

    Args:
        allow_bootstrap: True일 경우 vcpkg가 없으면 Tools/vcpkg에 자동으로 clone 및 빌드합니다.
            False여도 search_paths.vcpkg_auto_bootstrap / SW_VCPKG_AUTO_BOOTSTRAP 이면 허용됩니다.

    Returns:
        탐색된 vcpkg 경로(Path 객체). 찾지 못한 경우 None을 반환합니다.
    """
    search = load_search_paths()
    tools_dir = resolve_tools_subdir(KEY_VCPKG_TOOLS_SUBDIR, search)
    if found_root := find_tool_root(VCPKG_TOOL_SPEC, search):
        if found_root == tools_dir:
            git_commit = _resolve_vcpkg_commit(search)
            if ensure_git_on_path() is not None and git_commit:
                _pin_vcpkg_commit(tools_dir, git_commit)
            print(f"[Vcpkg] Using project kit: {tools_dir}", file=sys.stderr)
        # 다른 OS 에서 부트스트랩한 트리면 이 OS 의 실행 파일이 없다. 설치를 요청받았으면 그 자리에서 부트스트랩하고, 아니면 알리기만 한다
        # (CMake 툴체인이 configure 때 스스로 부트스트랩한다).
        if not has_vcpkg_executable(Path(found_root)):
            if auto_bootstrap_enabled(allow_bootstrap, KEY_VCPKG_AUTO_BOOTSTRAP, ENV_SW_VCPKG_AUTO_BOOTSTRAP, search=search):
                if not _run_bootstrap_script(Path(found_root)):
                    return None
            else:
                sys.stderr.write(f"[Vcpkg] {found_root} has no vcpkg executable for this OS — "
                                 "run with --install to bootstrap it here (CMake configure also bootstraps it).\n")
        return Path(record_engine_path(KEY_VCPKG_ROOT, found_root))

    if not auto_bootstrap_enabled(
        allow_bootstrap,
        KEY_VCPKG_AUTO_BOOTSTRAP,
        ENV_SW_VCPKG_AUTO_BOOTSTRAP,
        search=search,
    ):
        sys.stderr.write(
            "[Vcpkg] vcpkg not found. Set VCPKG_ROOT, use Tools/vcpkg, "
            "or --install / vcpkg_auto_bootstrap / SW_VCPKG_AUTO_BOOTSTRAP.\n"
        )
        return None

    git_exe = ensure_git_on_path()
    if git_exe is None:
        sys.stderr.write(
            "[Vcpkg Error] git not found on PATH (or typical Git for Windows locations).\n"
            "  Install Git for Windows, or add git to PATH, then re-run CMake configure / "
            "Scripts/setup/vcpkg.py --install.\n"
        )
        return None

    git_url = str(search[KEY_VCPKG_GIT_URL])
    git_commit = _resolve_vcpkg_commit(search)
    if not _bootstrap_vcpkg(tools_dir, git_url, git_commit, git_exe):
        return None
    return Path(record_engine_path(KEY_VCPKG_ROOT, tools_dir))


def main(argv: list[str] | None = None) -> int:
    """
    vcpkg 설정 스크립트의 CLI 진입점입니다.

    Args:
        argv: 명령줄 인수 리스트.

    Returns:
        종료 코드 (성공 시 0).
    """
    parser = argparse.ArgumentParser(description="Locate or bootstrap vcpkg.")
    parser.add_argument(
        "--install",
        action="store_true",
        help="If not found, git clone + bootstrap under Tools/vcpkg",
    )
    parsed_args = parser.parse_args(argv)
    resolved_path = setup_vcpkg(allow_bootstrap=parsed_args.install)
    if resolved_path:
        print(resolved_path.as_posix())
        return ExitCode.PASSED
    sys.stderr.write("[Vcpkg Error] vcpkg not found\n")
    return ExitCode.VIOLATION


if __name__ == "__main__":
    sys.exit(main())
