"""
Scripts/common/ClangFormat.py

clang-format 을 찾고, 고정 버전이 없으면 설치합니다. **포맷을 돌리는 쪽과 설치하는 쪽이 같은 규칙을 쓰도록 한 곳에 둡니다.**

예전에는 둘로 나뉘어 있었습니다. 설치하는 쪽(`setup/SetupLlvm.py`)은 "고정 버전만 쓴다" 는 규칙으로 버전을 확인했지만,
포맷을 돌리는 쪽(`common/Host.py` 의 `resolveClangFormat` — `FormatModified` · pre-commit 훅 · `RunClangFormat` 이 부른다)은
toolchain 의 LLVM 경로를 **버전 확인 없이** 먼저 쓰고, 그다음 PATH 를 보고, 고정본(`Tools/LLVM/bin`)은 그 뒤에야 봤습니다.
그래서 toolchain 이 시스템 LLVM(21.1.1)을 가리키는 PC 는 고정 버전(20.1.8)이 설치돼 있어도 21 로 포맷했습니다
(2026-09-30 에 찾음). 게다가 `common` 이 `setup` 을 함수 안에서 import 하는 역방향 의존이었습니다.

  resolveClangFormat()   포맷을 돌릴 clang-format — 고정본 우선, 없으면 설치, 그래도 없으면 경고와 함께 아무 것
  ensureClangFormat()    설치까지 하는 같은 규칙(부트스트랩이 부른다)
"""

from __future__ import annotations

import json
import platform
import shutil
import subprocess
import sys
from pathlib import Path

from .Archive import ensureCachedDownload, resolveToolsSubdir, toolsCacheDir
from .Config import loadSearchPaths, loadToolchainConfig
from .Constants import kKeyClangFormatVersion, kKeyLlvmPath, kKeyLlvmToolsSubdir
from .Paths import normalizePath

_kClangFormatWin = "clang-format.exe"
_kClangFormatPosix = "clang-format"


def clangFormatFileNameInternal() -> str:
    return _kClangFormatWin if platform.system() == "Windows" else _kClangFormatPosix


def pinnedClangFormatVersion() -> str:
    """search_paths.json 이 고정한 clang-format 버전입니다."""
    return str(loadSearchPaths().get(kKeyClangFormatVersion, "")).strip()


def clangFormatVersionOf(path: Path | str) -> str:
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
    고정 버전으로 설치해 둔 clang-format 을 찾습니다 (주어진 LLVM 경로, 그다음 Tools/LLVM/bin).

    **PATH 의 clang-format 은 마지막 수단이다.** 그 버전은 PC 마다 다르고, clang-format 은 버전이
    다르면 결과가 달라진다 — 실제로 18 과 20 은 이 저장소 400 파일 중 2 개를 다르게 포맷한다
    (동아시아 문자 폭 계산이 달라져 한글 주석 정렬이 갈린다). 포매터만은 "설치된 것" 이 아니라
    "정해진 것" 을 써야 두 PC 의 커밋이 서로를 되돌리지 않는다.
    """
    name = clangFormatFileNameInternal()
    want = pinnedClangFormatVersion()

    listCandidate: list[Path] = []
    if llvmPath:
        listCandidate.append(Path(llvmPath) / "bin" / name)
    listCandidate.append(resolveToolsSubdir(kKeyLlvmToolsSubdir, loadSearchPaths()) / "bin" / name)

    for candidate in listCandidate:
        if candidate.is_file() and clangFormatVersionOf(candidate) == want:
            return normalizePath(candidate)
    return ""


def findAnyRunnableClangFormatInternal() -> str:
    """버전을 가리지 않고 실행만 되는 clang-format 을 찾습니다 (고정본을 못 구했을 때의 마지막 수단)."""
    name = clangFormatFileNameInternal()
    tools = resolveToolsSubdir(kKeyLlvmToolsSubdir, loadSearchPaths())
    for candidate in (tools / "bin" / name, shutil.which("clang-format")):
        if candidate and Path(candidate).is_file() and clangFormatVersionOf(candidate):
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
    import urllib.request  # PyPI 를 물을 때만(Archive.downloadUrl 과 같은 이유)
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
        sys.stderr.write(f"[ClangFormat] PyPI 조회 실패 ({url}): {error}\n")
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
    import zipfile  # 휠을 풀 때만
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
                print(f"[ClangFormat] Installed {outPath}", file=sys.stderr)
                return True
    except (OSError, zipfile.BadZipFile) as error:
        sys.stderr.write(f"[ClangFormat] clang-format 휠을 열 수 없습니다: {error}\n")
    return False


def ensureClangFormat(llvmPath: str = "", *, allowDownload: bool = True) -> str:
    """
    `clang_format_version` 이 고정한 clang-format 을 확보합니다 (없으면 PyPI 휠로 설치).

    Returns:
        clang-format 실행 파일의 절대 경로 (실패 시 빈 문자열 "")
    """
    if found := findClangFormatPath(llvmPath):
        return found

    version = pinnedClangFormatVersion()
    if not version:
        sys.stderr.write(f"[ClangFormat] search_paths.json 에 {kKeyClangFormatVersion} 이 없습니다.\n")
        return findAnyRunnableClangFormatInternal()

    if allowDownload:
        destBin = resolveToolsSubdir(kKeyLlvmToolsSubdir, loadSearchPaths()) / "bin"
        if url := resolveClangFormatWheelUrlInternal(version):
            wheel = ensureCachedDownload(url, toolsCacheDir() / Path(url).name, label="clang-format")
            if installClangFormatFromWheelInternal(wheel, destBin):
                if found := findClangFormatPath(llvmPath):
                    return found
                sys.stderr.write(f"[ClangFormat] 설치한 clang-format 이 {version} 이 아닙니다.\n")
        else:
            sys.stderr.write(f"[ClangFormat] clang-format {version} 휠을 이 플랫폼에서 찾지 못했습니다.\n")

    # 고정본을 못 구했다 — 있는 것으로라도 돌리되, 결과가 달라질 수 있음을 분명히 알린다.
    if fallback := findAnyRunnableClangFormatInternal():
        sys.stderr.write(
            f"[ClangFormat] 고정 버전({version})을 구하지 못해 {clangFormatVersionOf(fallback)} "
            f"을 씁니다 — 포맷 결과가 다른 PC 와 달라질 수 있습니다.\n"
        )
        return fallback
    return ""


def resolveClangFormat(llvmPath: str = "", *, allowDownload: bool = True) -> str:
    """
    포맷을 돌릴 clang-format 실행 파일 경로입니다. `ensureClangFormat` 과 같은 규칙이고, LLVM 경로를 주지 않으면
    toolchain_config.json 의 것을 씁니다.

    `.clang-format` 은 규칙만 고정하지 **도구 버전은 고정하지 못합니다.** 한쪽에서 커밋한 줄을 다른 쪽 pre-commit 훅이
    거부하고, 고쳐서 올리면 원래 PC 가 다시 되돌리는 왕복을 실제로 겪었습니다(이어붙인 줄의 정렬 칸 수가 달랐습니다).
    """
    if not llvmPath:
        llvmPath = str(loadToolchainConfig().get(kKeyLlvmPath, "") or "")
    return ensureClangFormat(llvmPath, allowDownload=allowDownload)
