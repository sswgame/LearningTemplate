#!/usr/bin/env python3
"""
Scripts/setup/llvm.py

LLVM 탐색 + (없으면) GitHub tar 로 Tools/LLVM 최소 키트 확보.
Ninja/vcpkg 와 같이 find/setup 을 한 파일에 둡니다.

  python3 Scripts/setup/llvm.py
  python3 Scripts/setup/llvm.py --install

Windows 의 Tools/LLVM 에는 디버거(lldb.exe)와 그것이 부르는 Python 런타임(임베드 패키지)도 둔다 — 핫 리로드 뒤 브레이크포인트를
자동으로 확인하는 데 쓴다. 이미 있는 키트에 없는 파일만 더하므로 키트를 다시 풀지 않는다(다시 풀면 PCH 를 모두 버린다).
"""

from __future__ import annotations

import argparse
import os
import json
import shutil
import sys
import subprocess
import tarfile
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.tool_download import ensure_cached_download, extract_zip_safe, resolve_tools_subdir, tools_cache_dir
from common.clang_format import ensure_clang_format
from common.toolchain_config import auto_bootstrap_enabled, load_search_paths, record_engine_path
from common.project_constants import (
    ENV_SW_LLVM_AUTO_BOOTSTRAP,
    KEY_LIBCLANG_DLL_PATH,
    KEY_LLDB_PYTHON_EMBED_URL,
    KEY_LLDB_PYTHON_VERSION,
    KEY_LLVM_AUTO_BOOTSTRAP,
    KEY_LLVM_DOWNLOAD_URLS,
    KEY_LLVM_PATH,
    KEY_LLVM_SEARCH_ROOTS,
    KEY_LLVM_TOOLS_SUBDIR,
)
from common.paths import get_project_root, normalize_path, platform_key, shared_library_names
from common.host_platform import is_windows_host
from common.file_collect import find_first_existing_file, find_first_existing_file_recursive
from setup.toolchain_detect import ToolSpec, find_tool_root
from common.exit_code import ExitCode  # noqa: E402

# llvm-lib / llvm-ar 는 **LTO 때문에** 필요하다. clang 이 -flto 로 내는 .obj 는 LLVM 비트코드라
# MSVC lib.exe 가 못 읽고(LNK1107), 그러면 CMake 의 `check_ipo_supported` 가 실패해 IPO 가 통째로
# 꺼진다 — `SW_ENABLE_LTO=ON` 인데도 -flto 가 한 TU 에도 안 걸리고, 아무 오류도 나지 않는다.
_WIN_KEEP_BIN_EXES: set[str] = {
    "clang-cl.exe",
    "clang.exe",
    "clang++.exe",
    "clang-format.exe",
    "lld-link.exe",
    "lld.exe",
    "llvm-lib.exe",
    "llvm-rc.exe",
    "lldb.exe",  # 핫 리로드 디버그 확인(liblldb.dll 은 bin 의 DLL 로 이미 남는다) — _ensure_debugger
}
_POSIX_KEEP_BIN_NAMES: set[str] = {
    "clang",
    "clang++",
    "clang-cl",
    "clang-format",
    "lld",
    "ld.lld",
    "llvm-ar",
    "llvm-ranlib",
    "llvm-rc",
}
_TAR_KEEP_PREFIXES = (
    "bin/",
    "lib/clang/",
    "lib/libclang",
    "include/clang-c/",
)

# --- 1. LLVM 탐색 -----------------------------------------------------------

def find_lib_clang_dll_path(llvm_path: str) -> str:
    """
    LLVM 설치 경로를 기반으로 libclang(파서) 동적 라이브러리의 경로를 찾습니다.
    """
    lib_names = shared_library_names("libclang")

    search_dirs: list[Path] = []
    if llvm_path:
        root = Path(llvm_path)
        search_dirs.extend(
            [
                root / "bin",
                root / "lib",
                root / "lib" / "x86_64-linux-gnu",
                root / "lib64",
            ]
        )

    found = find_first_existing_file(search_dirs, lib_names)
    if found:
        return normalize_path(found)
    for name in lib_names:
        which = shutil.which(name)
        if which:
            return normalize_path(which)
    if llvm_path:
        found = find_first_existing_file_recursive([Path(llvm_path)], lib_names)
        if found:
            return normalize_path(found)
        root = Path(llvm_path)
        glob_matches: list[Path] = []
        for folder in (root / "bin", root / "lib", root / "lib" / "x86_64-linux-gnu", root / "lib64"):
            if folder.is_dir() is False:
                continue
            glob_matches.extend(folder.glob("libclang.so*"))
            glob_matches.extend(folder.glob("libclang-*.so*"))
        for match in sorted(glob_matches):
            if match.is_file():
                return normalize_path(str(match))
    return ""

def llvm_resource_major(path: str) -> int:
    """lib/clang/<major>/include 디렉터리에서 가장 큰 Major 버전을 읽어 반환합니다."""
    clang_res = Path(path) / "lib" / "clang"
    if not clang_res.is_dir():
        return 0
    best = 0
    for sub in clang_res.iterdir():
        if not sub.is_dir() or not (sub / "include").is_dir():
            continue
        try:
            major = int(sub.name.split(".", 1)[0])
        except ValueError:
            continue
        if major > best:
            best = major
    return best

def required_llvm_major() -> int:
    """
    Windows 환경의 최신 MSVC STL(VS 2022/18)은 Clang 20 이상 버전을 요구합니다 (STL1000 오류 방지).
    다른 플랫폼은 기본 최소 키트만 만족하면 됩니다.
    """
    if is_windows_host():
        return 20
    return 0

def is_minimal_llvm_root(path: str) -> bool:
    """
    지정된 디렉터리가 clang-cl/clang 컴파일러, libclang, 리소스 디렉터리 및 헤더를 포함하는
    최소 유효 LLVM 키트인지 검사합니다.
    """
    if not path:
        return False
    root = Path(path)
    if not root.is_dir():
        return False

    bin_dir = root / "bin"
    if is_windows_host():
        if not (bin_dir / "clang-cl.exe").is_file():
            return False
        # llvm-lib 도 "최소 유효" 의 일부다. 없으면 LTO 가 통째로 꺼진다 — 그것도 조용히
        # (자세한 이유는 _WIN_KEEP_BIN_EXES 주석). 이 검사에 넣어야 **이미 설치된 트리도 복구된다**.
        if not (bin_dir / "llvm-lib.exe").is_file():
            return False
        if not find_lib_clang_dll_path(str(root)):
            return False
        if not (root / "lib" / "libclang.lib").is_file():
            return False
        if not (root / "include" / "clang-c" / "Index.h").is_file():
            return False
    else:
        if not (bin_dir / "clang").is_file() and not (bin_dir / "clang-cl").is_file():
            return False
        # Windows 쪽 llvm-lib 과 같은 이유다 (위 주석). 리눅스는 증상만 다르다 —
        # CMake 가 IPO 정적 라이브러리를 CMAKE_<LANG>_COMPILER_AR 로 묶는데 그게 -NOTFOUND 가 된다.
        if not (bin_dir / "llvm-ar").is_file():
            return False
        if not find_lib_clang_dll_path(str(root)):
            return False
        if not (root / "include" / "clang-c" / "Index.h").is_file():
            return False

    clang_res = root / "lib" / "clang"
    if not clang_res.is_dir():
        return False
    if not any(sub.is_dir() and (sub / "include").is_dir() for sub in clang_res.iterdir()):
        return False

    need = required_llvm_major()
    if need > 0:
        major = llvm_resource_major(str(root))
        if major < need:
            return False
    return True

LLVM_TOOL_SPEC = ToolSpec(
    name="Llvm",
    tools_subdir_key=KEY_LLVM_TOOLS_SUBDIR,
    search_roots_key=KEY_LLVM_SEARCH_ROOTS,
    bin_names=("clang-cl.exe", "clang-cl", "clang.exe", "clang"),
    env_vars=("LLVM_DIR", "LLVM_HOME", "LLVM_ROOT", "LLVM_PATH"),
    validate_func=lambda p: is_minimal_llvm_root(str(p)),
)


def find_llvm_path() -> str:
    """
    환경변수 → 시스템 PATH → search_paths.json 탐색 루트 → Tools/LLVM 순서로 유효한 LLVM 키트를 찾습니다.
    """
    search = load_search_paths()
    if found := find_tool_root(LLVM_TOOL_SPEC, search):
        return normalize_path(str(found))
    return ""

# --- 2. LLVM 부트스트랩 및 설치 --------------------------------------------

def _record(root: Path) -> str:
    resolved = record_engine_path(KEY_LLVM_PATH, root.resolve())
    libclang = find_lib_clang_dll_path(resolved)
    if libclang:
        record_engine_path(KEY_LIBCLANG_DLL_PATH, libclang)
    ensure_clang_format(resolved, allow_download=True)
    return resolved


def _replace_dir(src: Path, dest: Path) -> None:
    """
    src 디렉터리를 dest 디렉터리로 안전하게 교체합니다.
    (Windows 환경에서 프로세스가 폴더를 점유 중일 경우를 대비해 .old로 먼저 이름 변경 후 교체합니다.)
    """
    parent = dest.parent
    old = parent / f"{dest.name}.old"
    if old.exists():
        shutil.rmtree(old, ignore_errors=True)
        if old.exists():
            old = parent / f"{dest.name}.old.{os.getpid()}"

    if dest.exists():
        try:
            dest.rename(old)
        except OSError as exc:
            raise RuntimeError(
                f"점유 중인 '{dest}' 디렉터리를 이동할 수 없습니다 (clangd, clang-cl, IDE 등을 종료해주세요). "
                f"OS 오류: {exc}"
            ) from exc

    try:
        src.rename(dest)
    except OSError as exc:
        # 실패 시 롤백하여 기존 설치본을 복구합니다.
        if old.exists() and not dest.exists():
            try:
                old.rename(dest)
            except OSError:
                pass
        raise RuntimeError(
            f"'{src}' -> '{dest}' 이동 실패 (Tools 폴더를 점유 중인 프로세스를 닫아주세요). OS 오류: {exc}"
        ) from exc

    shutil.rmtree(old, ignore_errors=True)
    if old.exists():
        print(
            f"[Llvm] 경고: '{old}' 임시 폴더를 삭제할 수 없습니다 "
            "(파일이 아직 점유 중입니다. IDE나 clangd 종료 후 수동 삭제 가능합니다)."
        )

def _invalidate_precompiled_headers(project_root: Path) -> int:
    """
    빌드 트리들의 PCH 를 지웁니다 (툴체인 교체 직후에만 부릅니다).

    LLVM 을 새로 깔면 `lib/clang/<major>/include` 의 헤더가 바뀌고, 그 순간 기존 PCH 는 전부
    쓸 수 없게 된다. 그런데 컴파일러 메시지는 "has been modified since the precompiled header
    was built" 뿐이라 원인도 조치도 안 알려 준다. 그래서 **툴체인을 바꾼 쪽이 치운다.**

    `.pch` 만 지우면 안 된다 — ninja 는 짝이 되는 `cmake_pch.cxx.obj` 가 최신이면 PCH 를 다시
    만들지 않고, 그것을 필요로 하는 TU 만 컴파일하다 "PCH file not found" 로 진다. 둘 다 지운다.
    """
    build_root = project_root / "build"
    if build_root.is_dir() is False:
        return 0

    removed = 0
    for pattern in ("cmake_pch*.pch", "cmake_pch*.obj", "cmake_pch*.gch"):
        for stale in build_root.rglob(pattern):
            try:
                stale.unlink()
                removed += 1
            except OSError:
                pass
    return removed


def _extract_tar_minimal(archive: Path, dest_root_dir: Path) -> None:
    """
    LLVM 아카이브에서 빌드에 필요한 최소 구성 파일들만 임시 스테이징 폴더에 압축 해제한 뒤 destRootDir로 교체합니다.
    """
    parent = dest_root_dir.parent
    parent.mkdir(parents=True, exist_ok=True)
    staging_parent = parent / f".{dest_root_dir.name}_extract_{os.getpid()}"
    if staging_parent.exists():
        shutil.rmtree(staging_parent, ignore_errors=True)
    staging_parent.mkdir(parents=True, exist_ok=True)

    try:
        with tarfile.open(archive, "r:*") as tar_handle:
            members = []
            top_directory: str | None = None
            for member in tar_handle.getmembers():
                parts = Path(member.name).parts
                if not parts:
                    continue
                if top_directory is None:
                    top_directory = parts[0]
                relative_member_path = "/".join(parts[1:]) if top_directory and parts[0] == top_directory else member.name
                if any(relative_member_path == p.rstrip("/") or relative_member_path.startswith(p) for p in _TAR_KEEP_PREFIXES):
                    members.append(member)
            print(f"[Llvm] Extracting {len(members)} archive members (minimal)...")
            safe_members = [
                member
                for member in members
                if not member.name.replace("\\", "/").startswith("/")
                and ".." not in Path(member.name).parts
            ]
            tar_handle.extractall(path=staging_parent, members=safe_members)

        extracted_dir = staging_parent / (top_directory or "")
        if not extracted_dir.is_dir():
            raise RuntimeError(f"Extracted LLVM root not found under {staging_parent}")

        _replace_dir(extracted_dir, dest_root_dir)
    finally:
        shutil.rmtree(staging_parent, ignore_errors=True)

def _prune_bin(root: Path) -> None:
    bin_dir = root / "bin"
    if not bin_dir.is_dir():
        return
    if is_windows_host():
        for path in bin_dir.iterdir():
            lower = path.name.lower()
            if path.is_file() and lower.endswith(".exe") and path.name not in _WIN_KEEP_BIN_EXES:
                path.unlink(missing_ok=True)
            elif path.is_file() and lower.endswith((".pdb", ".txt", ".html")):
                path.unlink(missing_ok=True)
        return
    for path in bin_dir.iterdir():
        if not path.is_file():
            continue
        name = path.name
        if name in _POSIX_KEEP_BIN_NAMES or name.startswith("clang-"):
            # clang-format 은 유지. clang-tidy/clangd 등만 제거.
            if name.startswith("clang-format"):
                continue
            if name.startswith(
                ("clang-tidy", "clangd", "clang-check", "clang-doc")
            ):
                path.unlink(missing_ok=True)
            continue
        if name.startswith(("lld", "ld.lld", "llvm-rc")):
            continue
        path.unlink(missing_ok=True)

def _prune_lib_include(root: Path) -> None:
    lib_dir = root / "lib"
    if lib_dir.is_dir():
        for path in list(lib_dir.iterdir()):
            if path.name == "clang" and path.is_dir():
                continue
            if path.is_file() and path.name.lower().startswith("libclang"):
                continue
            if path.is_dir():
                shutil.rmtree(path, ignore_errors=True)
            else:
                path.unlink(missing_ok=True)
    include_dir = root / "include"
    if include_dir.is_dir():
        for path in list(include_dir.iterdir()):
            if path.name == "clang-c" and path.is_dir():
                continue
            if path.is_dir():
                shutil.rmtree(path, ignore_errors=True)
            else:
                path.unlink(missing_ok=True)
    for drop_folder in ("share", "libexec", "msvc", "python", "tools", "local"):
        drop_path = root / drop_folder
        if drop_path.is_dir():
            shutil.rmtree(drop_path, ignore_errors=True)

def _ensure_debugger(root: Path, search: dict) -> None:
    """
    Windows 키트(`Tools/LLVM`)에 lldb.exe 와 Python 런타임을 둔다. 없는 것만 더한다 — 키트를 다시 풀면 PCH 를 모두 버리므로 그 길로 가지 않는다.
    lldb.exe 는 키트를 만든 같은 압축본(캐시)에서 한 파일만 꺼낸다. liblldb.dll 이 python3XX.dll 을 import 해 python.org 임베드 패키지를 bin 에 푼다
    (python.exe · pythonw.exe 는 빼고 DLL · 표준 라이브러리 zip · ._pth · .pyd 만). 마지막에 `lldb --version` 이 돌아야 끝난다.
    """
    bin_dir = root / "bin"
    if not (bin_dir / "lldb.exe").is_file():
        url = search[KEY_LLVM_DOWNLOAD_URLS][platform_key()]
        archive = ensure_cached_download(url, tools_cache_dir() / url.rsplit("/", 1)[-1], min_size=50_000_000, label="Llvm")
        print("[Llvm] Adding lldb.exe from the cached LLVM archive...")
        with tarfile.open(archive, "r:*") as tar_handle:
            member = next((m for m in tar_handle if m.isfile() and m.name.endswith("/bin/lldb.exe")), None)
            if member is None:
                raise RuntimeError(f"[Llvm] {archive.name} has no bin/lldb.exe")
            source = tar_handle.extractfile(member)
            (bin_dir / "lldb.exe").write_bytes(source.read())

    # lldb 의 파이썬 바인딩(`lib/site-packages/lldb` — `import lldb` 와 `command script import` 가 쓴다). liblldb.dll 이 bin 옆 `../lib/site-packages` 에서 찾는다.
    site_packages = root / "lib" / "site-packages"
    if not (site_packages / "lldb" / "__init__.py").is_file():
        url = search[KEY_LLVM_DOWNLOAD_URLS][platform_key()]
        archive = ensure_cached_download(url, tools_cache_dir() / url.rsplit("/", 1)[-1], min_size=50_000_000, label="Llvm")
        print("[Llvm] Adding the lldb Python package from the cached LLVM archive...")
        with tarfile.open(archive, "r:*") as tar_handle:
            count = 0
            for member in tar_handle:
                marker = "/lib/site-packages/"
                if marker not in member.name or not (member.isfile() or member.islnk() or member.issym()):
                    continue
                relative = member.name.split(marker, 1)[1]
                if ".." in Path(relative).parts:
                    raise RuntimeError(f"[Llvm] unsafe path in {archive.name}: {member.name}")
                target = site_packages / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                source = tar_handle.extractfile(member)
                if source is None:
                    raise RuntimeError(f"[Llvm] {archive.name}: cannot read {member.name}")
                target.write_bytes(source.read())
                count += 1
        if not (site_packages / "lldb" / "__init__.py").is_file():
            raise RuntimeError(f"[Llvm] {archive.name} has no lib/site-packages/lldb ({count} files extracted)")

    python_version = search[KEY_LLDB_PYTHON_VERSION]
    python_dll = "python" + "".join(python_version.split(".")[:2]) + ".dll"
    if not (bin_dir / python_dll).is_file():
        url = search[KEY_LLDB_PYTHON_EMBED_URL]
        archive = ensure_cached_download(url, tools_cache_dir() / url.rsplit("/", 1)[-1], min_size=1_000_000, label="Llvm")
        staging = root / f".python_embed_{os.getpid()}"
        shutil.rmtree(staging, ignore_errors=True)
        extract_zip_safe(archive, staging)
        for path in staging.iterdir():
            if path.is_file() and path.name.lower() not in ("python.exe", "pythonw.exe"):
                shutil.copy2(path, bin_dir / path.name)
        shutil.rmtree(staging, ignore_errors=True)

    result = subprocess.run([str(bin_dir / "lldb.exe"), "--version"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"[Llvm] {bin_dir / 'lldb.exe'} does not run ({result.returncode}): {result.stderr.strip()} — "
                           f"liblldb.dll needs {python_dll} ({KEY_LLDB_PYTHON_VERSION} in search_paths)")
    print(f"[Llvm] Debugger ready: {result.stdout.strip()}")


def setup_llvm(allow_bootstrap: bool = False) -> str:
    """
    LLVM/Clang 경로를 탐색하거나, 누락되었을 경우 GitHub에서 다운로드하여 설치(Bootstrap)합니다.
    (Windows 환경에서 clang-cl 버전 검증 포함)
    """
    existing = find_llvm_path()
    search = load_search_paths()
    tools = resolve_tools_subdir(KEY_LLVM_TOOLS_SUBDIR, search)
    if existing:
        print(f"[Llvm] Using existing LLVM kit: {existing}")
        if is_windows_host() and Path(existing).resolve() == tools.resolve():
            _ensure_debugger(tools, search)
        return _record(Path(existing))

    # Stale Tools/LLVM (e.g. Clang 19 vs VS18 STL needing 20): explain before replace.
    if tools.is_dir() and (tools / "bin" / "clang-cl.exe").is_file():
        major = llvm_resource_major(str(tools))
        need = required_llvm_major()
        if need > 0 and major > 0 and major < need:
            print(
                f"[Llvm] Tools/LLVM is Clang {major}; "
                f"VS STL requires Clang {need}+ - re-bootstrapping"
            )

    if not auto_bootstrap_enabled(
        allow_bootstrap,
        KEY_LLVM_AUTO_BOOTSTRAP,
        ENV_SW_LLVM_AUTO_BOOTSTRAP,
        search=search,
    ):
        sys.stderr.write(
            "[Llvm] clang-cl/libclang not found (or too old for this STL). "
            "Re-run with --install (or llvm_auto_bootstrap / SW_LLVM_AUTO_BOOTSTRAP).\n"
        )
        return ""

    url = search[KEY_LLVM_DOWNLOAD_URLS][platform_key()]   # search_paths.defaults 의 필수 키 — 없으면 KeyError

    filename = url.rsplit("/", 1)[-1]
    if filename.lower().endswith(".exe") or not filename.endswith(
        (".tar.xz", ".tar.gz", ".tgz", ".tar.bz2", ".tar")
    ):
        sys.stderr.write(
            "[Llvm] Use GitHub clang+llvm-*-x86_64-pc-windows-msvc.tar.xz "
            "(NSIS .exe not supported).\n"
        )
        return ""

    # 부트스트랩을 허락받은 뒤의 받기 · 풀기 실패는 "LLVM 없음" 이 아니라 오류다 — 예외 그대로 멈춘다
    # (빈 문자열로 돌려주면 configure 가 LLVM 없이 다음 단계로 가서 엉뚱한 자리에서 깨진다).
    print(f"[Llvm] Bootstrapping minimal kit into {tools}")
    tools.parent.mkdir(parents=True, exist_ok=True)
    archive = ensure_cached_download(
        url, tools_cache_dir() / filename, min_size=50_000_000, label="Llvm"
    )
    _extract_tar_minimal(archive, tools)
    print(f"[Llvm] Pruning to clang-cl/libclang kit under {tools}")
    _prune_bin(tools)
    _prune_lib_include(tools)
    if not is_minimal_llvm_root(str(tools)):
        raise RuntimeError(f"[Llvm] Minimal kit check failed under {tools}")
    print(f"[Llvm] Minimal kit ready: {tools}")
    if is_windows_host():
        _ensure_debugger(tools, search)

    # 헤더가 바뀌었으니 이 툴체인으로 만든 PCH 는 전부 못 쓴다 (헬퍼 독스트링 참고).
    stale_count = _invalidate_precompiled_headers(get_project_root())
    if stale_count > 0:
        print(f"[Llvm] Invalidated {stale_count} stale precompiled-header file(s) under build/")

    return _record(tools)

def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Locate or bootstrap a minimal clang-cl + libclang kit under Tools/LLVM."
    )
    parser.add_argument("--install", action="store_true", help="Bootstrap when missing.")
    args = parser.parse_args(list(argv) if argv is not None else None)
    if path := setup_llvm(allow_bootstrap=args.install):
        print(path)
        return ExitCode.PASSED
    sys.stderr.write("[Llvm Error] LLVM kit not available\n")
    return ExitCode.VIOLATION


if __name__ == "__main__":
    sys.exit(main())
