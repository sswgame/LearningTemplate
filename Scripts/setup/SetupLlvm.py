#!/usr/bin/env python3
"""
Scripts/setup/SetupLlvm.py

LLVM 탐색 + (없으면) GitHub tar 로 Tools/LLVM 최소 키트 확보.
Ninja/vcpkg 와 같이 find/setup 을 한 파일에 둡니다.

  python3 Scripts/setup/SetupLlvm.py
  python3 Scripts/setup/SetupLlvm.py --install
"""

from __future__ import annotations

import argparse
import os
import platform
import json
import shutil
import sys
import tarfile
from pathlib import Path
from typing import Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    autoBootstrapEnabled,
    ensureCachedDownload,
    ensureClangFormat,
    findFirstExistingFile,
    findFirstExistingFileRecursive,
    ToolSpec,
    findToolRoot,
    getProjectRoot,
    kEnvSwLlvmAutoBootstrap,
    kKeyLibclangDllPath,
    kKeyLlvmAutoBootstrap,
    kKeyLlvmDownloadUrls,
    kKeyLlvmPath,
    kKeyLlvmSearchRoots,
    kKeyLlvmToolsSubdir,
    loadSearchPaths,
    normalizePath,
    platformKey,
    recordEnginePath,
    resolveToolsSubdir,
    sharedLibraryNames,
    toolsCacheDir,
)

# llvm-lib / llvm-ar 는 **LTO 때문에** 필요하다. clang 이 -flto 로 내는 .obj 는 LLVM 비트코드라
# MSVC lib.exe 가 못 읽고(LNK1107), 그러면 CMake 의 `check_ipo_supported` 가 실패해 IPO 가 통째로
# 꺼진다 — `SW_ENABLE_LTO=ON` 인데도 -flto 가 한 TU 에도 안 걸리고, 아무 오류도 나지 않는다.
_kWinKeepBinExes: set[str] = {
    "clang-cl.exe",
    "clang.exe",
    "clang++.exe",
    "clang-format.exe",
    "lld-link.exe",
    "lld.exe",
    "llvm-lib.exe",
    "llvm-rc.exe",
}
_kPosixKeepBinNames: set[str] = {
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
_kTarKeepPrefixes = (
    "bin/",
    "lib/clang/",
    "lib/libclang",
    "include/clang-c/",
)

# --- 1. LLVM 탐색 -----------------------------------------------------------

def findLibClangDllPath(llvmPath: str) -> str:
    """
    LLVM 설치 경로를 기반으로 libclang(파서) 동적 라이브러리의 경로를 찾습니다.
    """
    libNames = sharedLibraryNames("libclang")

    searchDirs: list[Path] = []
    if llvmPath:
        root = Path(llvmPath)
        searchDirs.extend(
            [
                root / "bin",
                root / "lib",
                root / "lib" / "x86_64-linux-gnu",
                root / "lib64",
            ]
        )

    found = findFirstExistingFile(searchDirs, libNames)
    if found:
        return normalizePath(found)
    for name in libNames:
        which = shutil.which(name)
        if which:
            return normalizePath(which)
    if llvmPath:
        found = findFirstExistingFileRecursive([Path(llvmPath)], libNames)
        if found:
            return normalizePath(found)
        root = Path(llvmPath)
        globMatches: list[Path] = []
        for folder in (root / "bin", root / "lib", root / "lib" / "x86_64-linux-gnu", root / "lib64"):
            if folder.is_dir() is False:
                continue
            globMatches.extend(folder.glob("libclang.so*"))
            globMatches.extend(folder.glob("libclang-*.so*"))
        for match in sorted(globMatches):
            if match.is_file():
                return normalizePath(str(match))
    return ""

def llvmResourceMajor(path: str) -> int:
    """lib/clang/<major>/include 디렉터리에서 가장 큰 Major 버전을 읽어 반환합니다."""
    clangRes = Path(path) / "lib" / "clang"
    if not clangRes.is_dir():
        return 0
    best = 0
    for sub in clangRes.iterdir():
        if not sub.is_dir() or not (sub / "include").is_dir():
            continue
        try:
            major = int(sub.name.split(".", 1)[0])
        except ValueError:
            continue
        if major > best:
            best = major
    return best

def requiredLlvmMajor() -> int:
    """
    Windows 환경의 최신 MSVC STL(VS 2022/18)은 Clang 20 이상 버전을 요구합니다 (STL1000 오류 방지).
    다른 플랫폼은 기본 최소 키트만 만족하면 됩니다.
    """
    if platform.system() == "Windows":
        return 20
    return 0

def isMinimalLlvmRoot(path: str) -> bool:
    """
    지정된 디렉터리가 clang-cl/clang 컴파일러, libclang, 리소스 디렉터리 및 헤더를 포함하는
    최소 유효 LLVM 키트인지 검사합니다.
    """
    if not path:
        return False
    root = Path(path)
    if not root.is_dir():
        return False

    binDir = root / "bin"
    if platform.system() == "Windows":
        if not (binDir / "clang-cl.exe").is_file():
            return False
        # llvm-lib 도 "최소 유효" 의 일부다. 없으면 LTO 가 통째로 꺼진다 — 그것도 조용히
        # (자세한 이유는 _kWinKeepBinExes 주석). 이 검사에 넣어야 **이미 설치된 트리도 복구된다**.
        if not (binDir / "llvm-lib.exe").is_file():
            return False
        if not findLibClangDllPath(str(root)):
            return False
        if not (root / "lib" / "libclang.lib").is_file():
            return False
        if not (root / "include" / "clang-c" / "Index.h").is_file():
            return False
    else:
        if not (binDir / "clang").is_file() and not (binDir / "clang-cl").is_file():
            return False
        # Windows 쪽 llvm-lib 과 같은 이유다 (위 주석). 리눅스는 증상만 다르다 —
        # CMake 가 IPO 정적 라이브러리를 CMAKE_<LANG>_COMPILER_AR 로 묶는데 그게 -NOTFOUND 가 된다.
        if not (binDir / "llvm-ar").is_file():
            return False
        if not findLibClangDllPath(str(root)):
            return False
        if not (root / "include" / "clang-c" / "Index.h").is_file():
            return False

    clangRes = root / "lib" / "clang"
    if not clangRes.is_dir():
        return False
    if not any(sub.is_dir() and (sub / "include").is_dir() for sub in clangRes.iterdir()):
        return False

    need = requiredLlvmMajor()
    if need > 0:
        major = llvmResourceMajor(str(root))
        if major < need:
            return False
    return True

kLlvmToolSpec = ToolSpec(
    name="SetupLlvm",
    tools_subdir_key=kKeyLlvmToolsSubdir,
    search_roots_key=kKeyLlvmSearchRoots,
    bin_names=("clang-cl.exe", "clang-cl", "clang.exe", "clang"),
    env_vars=("LLVM_DIR", "LLVM_HOME", "LLVM_ROOT", "LLVM_PATH"),
    validate_func=lambda p: isMinimalLlvmRoot(str(p)),
)


def findLlvmPath() -> str:
    """
    환경변수 → 시스템 PATH → search_paths.json 탐색 루트 → Tools/LLVM 순서로 유효한 LLVM 키트를 찾습니다.
    """
    search = loadSearchPaths()
    if found := findToolRoot(kLlvmToolSpec, search):
        return normalizePath(str(found))
    return ""

# --- 2. LLVM 부트스트랩 및 설치 --------------------------------------------

def recordInternal(root: Path) -> str:
    resolved = recordEnginePath(kKeyLlvmPath, root.resolve())
    libclang = findLibClangDllPath(resolved)
    if libclang:
        recordEnginePath(kKeyLibclangDllPath, libclang)
    ensureClangFormat(resolved, allowDownload=True)
    return resolved


def replaceDirInternal(src: Path, dest: Path) -> None:
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
            f"[SetupLlvm] 경고: '{old}' 임시 폴더를 삭제할 수 없습니다 "
            "(파일이 아직 점유 중입니다. IDE나 clangd 종료 후 수동 삭제 가능합니다)."
        )

def invalidatePrecompiledHeadersInternal(projectRoot: Path) -> int:
    """
    빌드 트리들의 PCH 를 지웁니다 (툴체인 교체 직후에만 부릅니다).

    LLVM 을 새로 깔면 `lib/clang/<major>/include` 의 헤더가 바뀌고, 그 순간 기존 PCH 는 전부
    쓸 수 없게 된다. 그런데 컴파일러 메시지는 "has been modified since the precompiled header
    was built" 뿐이라 원인도 조치도 안 알려 준다. 그래서 **툴체인을 바꾼 쪽이 치운다.**

    `.pch` 만 지우면 안 된다 — ninja 는 짝이 되는 `cmake_pch.cxx.obj` 가 최신이면 PCH 를 다시
    만들지 않고, 그것을 필요로 하는 TU 만 컴파일하다 "PCH file not found" 로 진다. 둘 다 지운다.
    """
    buildRoot = projectRoot / "build"
    if buildRoot.is_dir() is False:
        return 0

    removed = 0
    for pattern in ("cmake_pch*.pch", "cmake_pch*.obj", "cmake_pch*.gch"):
        for stale in buildRoot.rglob(pattern):
            try:
                stale.unlink()
                removed += 1
            except OSError:
                pass
    return removed


def extractTarMinimalInternal(archive: Path, destRootDir: Path) -> None:
    """
    LLVM 아카이브에서 빌드에 필요한 최소 구성 파일들만 임시 스테이징 폴더에 압축 해제한 뒤 destRootDir로 교체합니다.
    """
    parent = destRootDir.parent
    parent.mkdir(parents=True, exist_ok=True)
    stagingParent = parent / f".{destRootDir.name}_extract_{os.getpid()}"
    if stagingParent.exists():
        shutil.rmtree(stagingParent, ignore_errors=True)
    stagingParent.mkdir(parents=True, exist_ok=True)

    try:
        with tarfile.open(archive, "r:*") as tarHandle:
            members = []
            topDirectory: Optional[str] = None
            for member in tarHandle.getmembers():
                parts = Path(member.name).parts
                if not parts:
                    continue
                if topDirectory is None:
                    topDirectory = parts[0]
                relativeMemberPath = "/".join(parts[1:]) if topDirectory and parts[0] == topDirectory else member.name
                if any(relativeMemberPath == p.rstrip("/") or relativeMemberPath.startswith(p) for p in _kTarKeepPrefixes):
                    members.append(member)
            print(f"[SetupLlvm] Extracting {len(members)} archive members (minimal)...")
            safeMembers = [
                member
                for member in members
                if not member.name.replace("\\", "/").startswith("/")
                and ".." not in Path(member.name).parts
            ]
            tarHandle.extractall(path=stagingParent, members=safeMembers)

        extractedDir = stagingParent / (topDirectory or "")
        if not extractedDir.is_dir():
            raise RuntimeError(f"Extracted LLVM root not found under {stagingParent}")

        replaceDirInternal(extractedDir, destRootDir)
    finally:
        shutil.rmtree(stagingParent, ignore_errors=True)

def pruneBinInternal(root: Path) -> None:
    binDir = root / "bin"
    if not binDir.is_dir():
        return
    if platform.system() == "Windows":
        for path in binDir.iterdir():
            lower = path.name.lower()
            if path.is_file() and lower.endswith(".exe") and path.name not in _kWinKeepBinExes:
                path.unlink(missing_ok=True)
            elif path.is_file() and lower.endswith((".pdb", ".txt", ".html")):
                path.unlink(missing_ok=True)
        return
    for path in binDir.iterdir():
        if not path.is_file():
            continue
        name = path.name
        if name in _kPosixKeepBinNames or name.startswith("clang-"):
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

def pruneLibIncludeInternal(root: Path) -> None:
    libDir = root / "lib"
    if libDir.is_dir():
        for path in list(libDir.iterdir()):
            if path.name == "clang" and path.is_dir():
                continue
            if path.is_file() and path.name.lower().startswith("libclang"):
                continue
            if path.is_dir():
                shutil.rmtree(path, ignore_errors=True)
            else:
                path.unlink(missing_ok=True)
    includeDir = root / "include"
    if includeDir.is_dir():
        for path in list(includeDir.iterdir()):
            if path.name == "clang-c" and path.is_dir():
                continue
            if path.is_dir():
                shutil.rmtree(path, ignore_errors=True)
            else:
                path.unlink(missing_ok=True)
    for dropFolder in ("share", "libexec", "msvc", "python", "tools", "local"):
        dropPath = root / dropFolder
        if dropPath.is_dir():
            shutil.rmtree(dropPath, ignore_errors=True)

def setupLlvm(allowBootstrap: bool = False) -> str:
    """
    LLVM/Clang 경로를 탐색하거나, 누락되었을 경우 GitHub에서 다운로드하여 설치(Bootstrap)합니다.
    (Windows 환경에서 clang-cl 버전 검증 포함)
    """
    existing = findLlvmPath()
    if existing:
        print(f"[SetupLlvm] Using existing LLVM kit: {existing}")
        return recordInternal(Path(existing))

    search = loadSearchPaths()
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, search)

    # Stale Tools/LLVM (e.g. Clang 19 vs VS18 STL needing 20): explain before replace.
    if tools.is_dir() and (tools / "bin" / "clang-cl.exe").is_file():
        major = llvmResourceMajor(str(tools))
        need = requiredLlvmMajor()
        if need > 0 and major > 0 and major < need:
            print(
                f"[SetupLlvm] Tools/LLVM is Clang {major}; "
                f"VS STL requires Clang {need}+ - re-bootstrapping"
            )

    if not autoBootstrapEnabled(
        allowBootstrap,
        kKeyLlvmAutoBootstrap,
        kEnvSwLlvmAutoBootstrap,
        search=search,
    ):
        sys.stderr.write(
            "[SetupLlvm] clang-cl/libclang not found (or too old for this STL). "
            "Re-run with --install (or llvm_auto_bootstrap / SW_LLVM_AUTO_BOOTSTRAP).\n"
        )
        return ""

    urls = search.get(kKeyLlvmDownloadUrls) or {}
    url = urls.get(platformKey())
    if not url:
        sys.stderr.write(f"[SetupLlvm Error] No llvm_download_urls.{platformKey()} in search_paths.\n")
        return ""

    filename = url.rsplit("/", 1)[-1]
    if filename.lower().endswith(".exe") or not filename.endswith(
        (".tar.xz", ".tar.gz", ".tgz", ".tar.bz2", ".tar")
    ):
        sys.stderr.write(
            "[SetupLlvm] Use GitHub clang+llvm-*-x86_64-pc-windows-msvc.tar.xz "
            "(NSIS .exe not supported).\n"
        )
        return ""

    print(f"[SetupLlvm] Bootstrapping minimal kit into {tools}")
    tools.parent.mkdir(parents=True, exist_ok=True)
    try:
        archive = ensureCachedDownload(
            url, toolsCacheDir() / filename, minSize=50_000_000, label="SetupLlvm"
        )
        extractTarMinimalInternal(archive, tools)
        print(f"[SetupLlvm] Pruning to clang-cl/libclang kit under {tools}")
        pruneBinInternal(tools)
        pruneLibIncludeInternal(tools)
        if not isMinimalLlvmRoot(str(tools)):
            sys.stderr.write(f"[SetupLlvm] Minimal kit check failed under {tools}.\n")
            return ""
        print(f"[SetupLlvm] Minimal kit ready: {tools}")

        # 헤더가 바뀌었으니 이 툴체인으로 만든 PCH 는 전부 못 쓴다 (헬퍼 독스트링 참고).
        staleCount = invalidatePrecompiledHeadersInternal(getProjectRoot())
        if staleCount > 0:
            print(f"[SetupLlvm] Invalidated {staleCount} stale precompiled-header file(s) under build/")

        return recordInternal(tools)
    except Exception as exc:
        sys.stderr.write(f"[SetupLlvm Error] {exc}\n")
        return ""

def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Locate or bootstrap a minimal clang-cl + libclang kit under Tools/LLVM."
    )
    parser.add_argument("--install", action="store_true", help="Bootstrap when missing.")
    args = parser.parse_args(list(argv) if argv is not None else None)
    if path := setupLlvm(allowBootstrap=args.install):
        print(path)
        return 0
    sys.stderr.write("[SetupLlvm Error] LLVM kit not available\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
