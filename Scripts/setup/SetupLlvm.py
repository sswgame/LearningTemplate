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
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path
from typing import Iterable, Optional, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    autoBootstrapEnabled,
    ensureCachedDownload,
    findFirstExistingFile,
    findFirstExistingFileRecursive,
    ToolSpec,
    findToolRoot,
    kEnvSwLlvmAutoBootstrap,
    kKeyLibclangDllPath,
    kKeyLlvmAutoBootstrap,
    kKeyLlvmDownloadUrls,
    kKeyLlvmPath,
    kKeyClangFormatVersion,
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

def clangFormatFileNameInternal() -> str:
    return _kClangFormatWin if platform.system() == "Windows" else _kClangFormatPosix

def pinnedClangFormatVersionInternal() -> str:
    """search_paths.json 이 고정한 clang-format 버전입니다."""
    return str(loadSearchPaths().get(kKeyClangFormatVersion, "")).strip()


def clangFormatVersionOfInternal(path: Path | str) -> str:
    """`clang-format --version` 이 찍는 버전 문자열을 뽑습니다. 못 뜨면 빈 문자열."""
    try:
        completed = subprocess.run([str(path), "--version"], capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return ""
    if completed.returncode != 0:
        return ""
    for token in (completed.stdout or "").split():
        if token and token[0].isdigit():
            return token
    return ""


def findClangFormatPath(llvmPath: str = "") -> str:
    """
    고정 버전으로 설치해 둔 clang-format 을 찾습니다 (Tools/LLVM/bin 우선).

    **PATH 의 clang-format 은 마지막 수단이다.** 그 버전은 PC 마다 다르고, clang-format 은 버전이
    다르면 결과가 달라진다 — 실제로 18 과 20 은 이 저장소 400 파일 중 2 개를 다르게 포맷한다
    (동아시아 문자 폭 계산이 달라져 한글 주석 정렬이 갈린다). 포매터만은 "설치된 것" 이 아니라
    "정해진 것" 을 써야 두 PC 의 커밋이 서로를 되돌리지 않는다.
    """
    name = clangFormatFileNameInternal()
    want = pinnedClangFormatVersionInternal()

    listCandidate: list[Path] = []
    if llvmPath:
        listCandidate.append(Path(llvmPath) / "bin" / name)
    listCandidate.append(resolveToolsSubdir(kKeyLlvmToolsSubdir, loadSearchPaths()) / "bin" / name)

    for candidate in listCandidate:
        if candidate.is_file() and clangFormatVersionOfInternal(candidate) == want:
            return normalizePath(candidate)
    return ""


def findAnyRunnableClangFormatInternal() -> str:
    """버전을 가리지 않고 실행만 되는 clang-format 을 찾습니다 (고정본을 못 구했을 때의 마지막 수단)."""
    name = clangFormatFileNameInternal()
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, loadSearchPaths())
    for candidate in (tools / "bin" / name, shutil.which("clang-format")):
        if candidate and Path(candidate).is_file() and clangFormatVersionOfInternal(candidate):
            return normalizePath(candidate)
    return ""


def resolveClangFormatWheelUrlInternal(version: str) -> str:
    """
    PyPI 에서 이 플랫폼용 clang-format 휠 URL 을 찾습니다.

    LLVM 공식 릴리스는 전체 배포판만 올린다 — clang-format 하나를 얻자고 335 MB(리눅스 18) 나
    2 GB(20.1.8 의 `LLVM-*-Linux-X64.tar.xz`) 를 받아야 했고, 게다가 그 바이너리는 빌드된 배포판의
    `libtinfo.so.5` 를 요구해 요즘 리눅스에서는 실행조차 안 됐다. PyPI 의 `clang-format` 패키지는
    **바이너리 하나만** 담은 휠(약 1.4~1.7 MB)을 플랫폼별로 내고 버전이 LLVM 릴리스를 그대로 따른다.

    URL 에 해시가 들어 있어 손으로 적어 둘 수 없으므로 버전만 고정하고 URL 은 여기서 받아 온다.
    """
    if not version:
        return ""

    system = platform.system()
    machine = platform.machine().lower()
    bArm = machine in ("arm64", "aarch64")
    if system == "Windows":
        listTag = ["win_amd64"] if machine.endswith("64") else ["win32"]
    elif system == "Darwin":
        listTag = ["macosx", "arm64"] if bArm else ["macosx", "x86_64"]
    else:
        listTag = ["manylinux", "aarch64"] if bArm else ["manylinux", "x86_64"]

    url = f"https://pypi.org/pypi/clang-format/{version}/json"
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (OSError, ValueError) as error:
        sys.stderr.write(f"[SetupLlvm] PyPI 조회 실패 ({url}): {error}\n")
        return ""

    for entry in payload.get("urls", []):
        fileName = str(entry.get("filename", ""))
        if not fileName.endswith(".whl"):
            continue
        if all(tag in fileName for tag in listTag):
            return str(entry.get("url", ""))
    return ""


def installClangFormatFromWheelInternal(wheel: Path, destBin: Path) -> bool:
    """휠(zip)에서 clang-format 실행 파일만 destBin 으로 꺼냅니다."""
    wantName = clangFormatFileNameInternal()
    destBin.mkdir(parents=True, exist_ok=True)
    try:
        with zipfile.ZipFile(wheel) as archive:
            for member in archive.namelist():
                if member.endswith("/"):
                    continue
                if Path(member).name != wantName:
                    continue
                outPath = destBin / wantName
                with archive.open(member) as source, open(outPath, "wb") as outFile:
                    shutil.copyfileobj(source, outFile)
                if platform.system() != "Windows":
                    outPath.chmod(outPath.stat().st_mode | 0o111)
                print(f"[SetupLlvm] Installed {outPath}", file=sys.stderr)
                return True
    except (OSError, zipfile.BadZipFile) as error:
        sys.stderr.write(f"[SetupLlvm] clang-format 휠을 열 수 없습니다: {error}\n")
    return False


def ensureClangFormat(llvmPath: str = "", *, allowDownload: bool = True) -> str:
    """
    `clang_format_version` 이 고정한 clang-format 을 확보합니다 (없으면 PyPI 휠로 설치).

    Returns:
        clang-format 실행 파일의 절대 경로 (실패 시 빈 문자열 "")
    """
    if found := findClangFormatPath(llvmPath):
        return found

    version = pinnedClangFormatVersionInternal()
    if not version:
        sys.stderr.write(f"[SetupLlvm] search_paths.json 에 {kKeyClangFormatVersion} 이 없습니다.\n")
        return findAnyRunnableClangFormatInternal()

    if allowDownload:
        destBin = resolveToolsSubdir(kKeyLlvmToolsSubdir, loadSearchPaths()) / "bin"
        if url := resolveClangFormatWheelUrlInternal(version):
            wheel = ensureCachedDownload(url, toolsCacheDir() / Path(url).name, label="clang-format")
            if installClangFormatFromWheelInternal(wheel, destBin):
                if found := findClangFormatPath(llvmPath):
                    return found
                sys.stderr.write(f"[SetupLlvm] 설치한 clang-format 이 {version} 이 아닙니다.\n")
        else:
            sys.stderr.write(f"[SetupLlvm] clang-format {version} 휠을 이 플랫폼에서 찾지 못했습니다.\n")

    # 고정본을 못 구했다 — 있는 것으로라도 돌리되, 결과가 달라질 수 있음을 분명히 알린다.
    if fallback := findAnyRunnableClangFormatInternal():
        sys.stderr.write(
            f"[SetupLlvm] 고정 버전({version})을 구하지 못해 {clangFormatVersionOfInternal(fallback)} "
            f"을 씁니다 — 포맷 결과가 다른 PC 와 달라질 수 있습니다.\n"
        )
        return fallback
    return ""


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
