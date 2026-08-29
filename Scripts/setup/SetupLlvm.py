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
import shutil
import sys
import tarfile
from pathlib import Path
from typing import Iterable, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    autoBootstrapEnabled,
    ensureCachedDownload,
    findFirstExistingFile,
    findFirstExistingFileRecursive,
    findFirstValidRoot,
    kDirToolsLlvm,
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
    platformSearchRoots,
    recordEnginePath,
    resolveToolsSubdir,
    toolsCacheDir,
)

_kWinKeepBinExes: set[str] = {
    "clang-cl.exe",
    "clang.exe",
    "clang++.exe",
    "clang-format.exe",
    "lld-link.exe",
    "lld.exe",
    "llvm-rc.exe",
}
_kPosixKeepBinNames: set[str] = {
    "clang",
    "clang++",
    "clang-cl",
    "clang-format",
    "lld",
    "ld.lld",
    "llvm-rc",
}
_kTarKeepPrefixes = (
    "bin/",
    "lib/clang/",
    "lib/libclang",
    "include/clang-c/",
)
_kClangFormatWin = "clang-format.exe"
_kClangFormatPosix = "clang-format"

# --- 1. LLVM 탐색 -----------------------------------------------------------

def findLibClangDllPath(llvmPath: str) -> str:
    """
    LLVM 설치 경로를 기반으로 libclang(파서) 동적 라이브러리의 경로를 찾습니다.
    """
    if platform.system() == "Windows":
        libNames = ["libclang.dll"]
    elif platform.system() == "Darwin":
        libNames = ["libclang.dylib"]
    else:
        libNames = ["libclang.so", "libclang.so.1"]

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
        if not findLibClangDllPath(str(root)):
            return False
        if not (root / "lib" / "libclang.lib").is_file():
            return False
        if not (root / "include" / "clang-c" / "Index.h").is_file():
            return False
    else:
        if not (binDir / "clang").is_file() and not (binDir / "clang-cl").is_file():
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

def findLlvmPath() -> str:
    """
    환경변수 → 시스템 PATH → search_paths.json 탐색 루트 → Tools/LLVM 순서로 유효한 LLVM 키트를 찾습니다.
    """
    for envKey in ("LLVM_DIR", "LLVM_HOME", "LLVM_ROOT", "LLVM_PATH"):
        if (envLlvm := os.environ.get(envKey)) and isMinimalLlvmRoot(envLlvm):
            return normalizePath(envLlvm)

    if llvmBin := shutil.which("clang-cl") or shutil.which("clang"):
        parent = Path(llvmBin).resolve().parent.parent
        if isMinimalLlvmRoot(str(parent)):
            return normalizePath(str(parent))

    search = loadSearchPaths()
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, kDirToolsLlvm, search)
    extras = {kKeyLlvmToolsSubdir: search.get(kKeyLlvmToolsSubdir, kDirToolsLlvm)}
    found = findFirstValidRoot(
        platformSearchRoots(search, kKeyLlvmSearchRoots),
        lambda p: isMinimalLlvmRoot(str(p)),
        extras=extras,
        skip=tools,
    )
    if found:
        return normalizePath(str(found))
    if isMinimalLlvmRoot(str(tools)):
        return normalizePath(str(tools))
    return ""

# --- 2. LLVM 부트스트랩 및 설치 --------------------------------------------

def recordInternal(root: Path) -> str:
    resolved = recordEnginePath(kKeyLlvmPath, root.resolve())
    libclang = findLibClangDllPath(resolved)
    if libclang:
        recordEnginePath(kKeyLibclangDllPath, libclang)
    ensureClangFormat(resolved, allowDownload=True)
    return resolved

def clangFormatFileNameInternal() -> str:
    return _kClangFormatWin if platform.system() == "Windows" else _kClangFormatPosix

def findClangFormatPath(llvmPath: str = "") -> str:
    """
    Tools/LLVM/bin → PATH 순으로 clang-format을 찾습니다.
    """
    name = clangFormatFileNameInternal()
    if llvmPath and (candidate := Path(llvmPath) / "bin" / name).is_file():
        return normalizePath(candidate)
    if which := shutil.which("clang-format"):
        return normalizePath(which)
    search = loadSearchPaths()
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, kDirToolsLlvm, search)
    if (candidate := tools / "bin" / name).is_file():
        return normalizePath(candidate)
    return ""

def extractClangFormatFromArchiveInternal(archive: Path, destBin: Path) -> bool:
    """
    LLVM 배포 tar에서 bin/clang-format(.exe)만 destBin 으로 추출합니다.
    """
    wantName = clangFormatFileNameInternal()
    destBin.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive, "r:*") as tar:
        for member in tar.getmembers():
            if not member.isfile():
                continue
            parts = Path(member.name.replace("\\", "/")).parts
            if len(parts) < 2:
                continue
            if parts[-1] != wantName:
                continue
            if parts[-2] != "bin":
                continue
            extracted = tar.extractfile(member)
            if extracted is None:
                continue
            outPath = destBin / wantName
            with open(outPath, "wb") as outFile:
                shutil.copyfileobj(extracted, outFile)
            if platform.system() != "Windows":
                outPath.chmod(outPath.stat().st_mode | 0o111)
            print(f"[SetupLlvm] Installed {outPath}", file=sys.stderr)
            return True
    return False

def ensureClangFormat(llvmPath: str = "", *, allowDownload: bool = True) -> str:
    """
    clang-format 실행 파일이 없으면 llvm_download_urls 아카이브에서 추출하여 Tools/LLVM/bin에 설치합니다.

    Returns:
        clang-format 실행 파일의 절대 경로 (실패 시 빈 문자열 "")
    """
    found = findClangFormatPath(llvmPath)
    if found:
        return found
    if not allowDownload:
        return ""

    search = loadSearchPaths()
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, kDirToolsLlvm, search)
    destRoot = Path(llvmPath) if llvmPath else tools
    if not destRoot.is_dir():
        destRoot = tools
    destBin = destRoot / "bin"
    if not destBin.is_dir():
        destBin.mkdir(parents=True, exist_ok=True)

    urls = search.get(kKeyLlvmDownloadUrls, {})
    if not isinstance(urls, dict):
        return ""
    url = str(urls.get(platformKey(), "")).strip()
    if not url:
        sys.stderr.write(
            "[SetupLlvm] clang-format을 찾을 수 없으며 search_paths.json에 다운로드 URL이 없습니다. "
            "LLVM을 설치하거나 PATH에 clang-format을 추가해주세요.\n"
        )
        return ""

    cacheName = Path(url).name or "llvm-clang-format-src.tar.xz"
    archive = ensureCachedDownload(url, toolsCacheDir() / cacheName, label="LLVM(clang-format)")
    if not extractClangFormatFromArchiveInternal(archive, destBin):
        sys.stderr.write("[SetupLlvm] LLVM 아카이브 내에서 clang-format을 찾을 수 없습니다.\n")
        return ""
    return findClangFormatPath(str(destRoot))

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
            if name == _kClangFormatPosix or name.startswith("clang-format"):
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
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, kDirToolsLlvm, search)

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
        default=True,
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
