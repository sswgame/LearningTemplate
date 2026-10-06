"""
Scripts/setup/HostTools.py

호스트 SDK/컴파일러 경로 탐색 (MSVC, Windows SDK, DXC, system includes).
"""

from __future__ import annotations

import os
import platform
import shutil
import sys
from functools import lru_cache
from pathlib import Path
from typing import Callable

from common import (
    ToolSpec,
    runProcess,
    ensureCachedDownload,
    extractZipSafe,
    findFirstExistingFileInBinDirs,
    findFirstExistingFileRecursive,
    findToolRoot,
    kKeyNinjaDownloadUrls,
    kKeyNinjaSearchRoots,
    kKeyNinjaToolsSubdir,
    kKeyVcpkgInstalledRel,
    loadSearchPaths,
    normalizePath,
    platformKey,
    recordEnginePath,
    resolveToolsSubdir,
    selectLatestVersion,
    sharedLibraryNames,
    toolsCacheDir,
)


def findVcpkgInstalledDirsInternal(projectRoot: Path) -> list[Path]:
    """
    프로젝트에서 사용 중인 vcpkg installed 디렉터리 목록을 탐색하여 반환합니다.

    Args:
        projectRoot: 프로젝트 최상위 경로

    Returns:
        vcpkg installed 경로(Path) 리스트
    """
    result: list[Path] = []
    search = loadSearchPaths()
    relativeInstalledPath = search.get(kKeyVcpkgInstalledRel)
    if not relativeInstalledPath:
        raise KeyError(f"[HostTools] Missing required key '{kKeyVcpkgInstalledRel}' in search_paths config")
    if (preferred := projectRoot / relativeInstalledPath).is_dir():
        result.append(preferred)
    if (buildDir := projectRoot / "build").is_dir():
        if (directInstalledPath := buildDir / "vcpkg_installed").is_dir() and directInstalledPath not in result:
            result.append(directInstalledPath)
        try:
            for child in buildDir.iterdir():
                if child.is_dir() and (candidate := child / "vcpkg_installed").is_dir() and candidate not in result:
                    result.append(candidate)
        except (OSError, PermissionError):
            pass
    return result


def findHostLibraryInternal(names: list[str], vcpkgDirs: list[Path], searchRoots: list[Path]) -> str:
    """
    vcpkg 설치 디렉터리, 지정된 루트 폴더 및 시스템 PATH에서 동적 라이브러리를 탐색합니다.
    """
    if vcpkgDirs and (found := findFirstExistingFileInBinDirs(vcpkgDirs, names)):
        return normalizePath(found)
    if searchRoots and (found := findFirstExistingFileRecursive(searchRoots, names)):
        return normalizePath(found)
    if found := next((foundName for name in names if (foundName := shutil.which(name))), None):
        return normalizePath(found)
    return ""


def findDxcDlls(sdkDir: str, sdkVer: str, projectRoot: Path) -> tuple[str, str]:
    """
    DirectX Shader Compiler (dxcompiler)와 dxil 라이브러리 경로를 찾습니다.
    (우선순위: vcpkg_installed -> VULKAN_SDK -> Windows SDK -> PATH 순)

    Args:
        sdkDir: Windows SDK 디렉터리 경로
        sdkVer: Windows SDK 버전
        projectRoot: 프로젝트 최상위 경로

    Returns:
        (dxcompiler 경로, dxil 경로) 튜플
    """
    dxcNames = sharedLibraryNames("dxcompiler")
    dxilNames = sharedLibraryNames("dxil")

    vcpkgDirs = findVcpkgInstalledDirsInternal(projectRoot)
    searchRoots: list[Path] = []

    if vulkanSdk := os.environ.get("VULKAN_SDK"):
        searchRoots.append(Path(vulkanSdk))
    if platform.system() == "Windows" and sdkDir and sdkVer:
        searchRoots.append(Path(sdkDir))

    dxcPath = findHostLibraryInternal(dxcNames, vcpkgDirs, searchRoots)
    dxilPath = findHostLibraryInternal(dxilNames, vcpkgDirs, searchRoots)
    return dxcPath, dxilPath


@lru_cache(maxsize=1)
def findMsvcPath() -> str:
    """
    시스템에 설치된 최신 MSVC(Microsoft Visual C++) 툴체인 경로를 탐색하여 반환합니다.
    (Windows 환경에서 vswhere를 사용)

    Returns:
        MSVC VC/Tools/MSVC 하위의 최신 버전 디렉터리 경로. 찾지 못하면 빈 문자열.
    """
    if platform.system() != "Windows":
        return ""
    if (envVcTools := os.environ.get("VCToolsInstallDir")) and Path(envVcTools).exists():
        return normalizePath(envVcTools)

    programFiles = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if not programFiles:
        return ""
    vswhere = Path(programFiles) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return ""

    command = [
        str(vswhere), "-latest", "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools",
        "-property", "installationPath",
    ]
    result = runProcess(command)
    if not result.bSucceeded:
        # "설치 안 됨" 과 "찾다가 실패" 를 가른다 — 돌려주는 값은 둘 다 빈 문자열이지만 configure 로그에 이유가 남는다.
        print(f"[HostTools] MSVC 위치를 vswhere 로 찾지 못했습니다(종료 코드 {result.returnCode}): {result.stderr.strip()}", file=sys.stderr)
        return ""
    vsPath = result.stdout.strip()
    if not vsPath:
        return ""
    msvcBase = Path(vsPath) / "VC" / "Tools" / "MSVC"
    try:
        latestVersion = selectLatestVersion([entry.name for entry in msvcBase.iterdir() if entry.is_dir()]) if msvcBase.is_dir() else ""
    except OSError as error:
        print(f"[HostTools] MSVC 버전 폴더를 읽지 못했습니다: {msvcBase}: {error}", file=sys.stderr)
        return ""
    return normalizePath(msvcBase / latestVersion) if latestVersion else ""


@lru_cache(maxsize=1)
def findWindowsSdkPath() -> tuple[str, str]:
    """
    시스템에 설치된 최신 Windows 10/11 SDK 경로와 버전을 레지스트리에서 탐색하여 반환합니다.

    Returns:
        (SDK 설치 경로, SDK 버전) 튜플. (Windows 환경이 아니거나 찾지 못하면 빈 문자열 튜플)
    """
    if platform.system() != "Windows":
        return "", ""
    if (envSdkDir := os.environ.get("WindowsSdkDir")) and Path(envSdkDir).exists():
        version = os.environ.get("WindowsSDKVersion", "").strip("\\")
        return normalizePath(envSdkDir), version
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
            kitsRoot, _ = winreg.QueryValueEx(key, "KitsRoot10")
        includeDir = Path(kitsRoot) / "Include"
        latestVersion = selectLatestVersion([entry.name for entry in includeDir.iterdir() if entry.name.startswith("10.")]) if kitsRoot and includeDir.is_dir() else ""
    except OSError as error:
        # 레지스트리 키가 없거나 폴더를 못 읽음 — "설치 안 됨" 과 구별되도록 이유를 남긴다.
        print(f"[HostTools] Windows SDK 위치를 레지스트리에서 찾지 못했습니다: {error}", file=sys.stderr)
        return "", ""
    return (normalizePath(kitsRoot), latestVersion) if latestVersion else ("", "")


# ==============================================================================
# 빌드 도구 탐색 및 다운로드 (Ninja / Sccache)
# ==============================================================================

def setupBuildToolInternal(toolName: str,
                           exeName: str,
                           subdirKey: str,
                           searchRootsKey: str,
                           downloadUrlsKey: str,
                           extractFunc: Callable[[Path, Path, Path, str], None],
                           *,
                           acceptExe: Callable[[Path], bool] | None = None,
                           sha256Key: str = "") -> str:
    """
    공통 도구(Ninja, Sccache 등) 탐색 및 다운로드 추상화 함수.

    - `acceptExe` : 찾은 실행 파일을 쓸지 고른다(버전 핀) — 거절하면 찾지 못한 것으로 보고 받는다.
    - `sha256Key` : search_paths 의 `{플랫폼: 해시}` 키 — 받은 압축 파일을 그 해시로 검사한다.
    """
    import logging
    logger = logging.getLogger("SetupEnvironment")
    logger.info(f"[{toolName}] Checking {toolName}...")

    search = loadSearchPaths()
    toolsDir = resolveToolsSubdir(subdirKey, search)
    localExePath = toolsDir / exeName

    def isUsableInternal(root: Path) -> bool:
        exePath = root if root.is_file() else root / exeName
        return exePath.is_file() and (acceptExe is None or acceptExe(exePath))

    spec = ToolSpec(
        name=toolName,
        tools_subdir_key=subdirKey,
        search_roots_key=searchRootsKey,
        bin_names=(exeName, exeName.replace(".exe", "") if platform.system() == "Windows" else exeName),
        validate_func=isUsableInternal,
    )
    if found := findToolRoot(spec, search, logger=logger):
        resolvedExe = found if found.is_file() else (found / exeName if (found / exeName).is_file() else found)
        return recordEnginePath(f"{toolName.lower()}_path", resolvedExe)

    url = (search.get(downloadUrlsKey) or {}).get(platformKey())
    if not url:
        logger.warning(f"[{toolName} Error] No {downloadUrlsKey}.{platformKey()}")
        return ""

    logger.info(f"[{toolName}] Downloading into {toolsDir}")
    toolsDir.mkdir(parents=True, exist_ok=True)
    archiveName = url.rsplit("/", 1)[-1]
    expectedHash = (search.get(sha256Key) or {}).get(platformKey()) if sha256Key else None
    archivePath = ensureCachedDownload(
        url, toolsCacheDir() / archiveName, minSize=50_000, label=toolName, sha256=expectedHash
    )

    try:
        extractFunc(archivePath, toolsDir, localExePath, exeName)
        if localExePath.is_file():
            if platform.system() != "Windows":
                os.chmod(localExePath, 0o755)
            logger.info(f"[{toolName}] Installed: {localExePath}")
            return recordEnginePath(f"{toolName.lower()}_path", localExePath)
    except Exception as exception:
        logger.error(f"[{toolName} Error] {exception}")
    return ""


def setupNinja() -> str:
    """시스템 PATH 또는 search_paths.json에서 Ninja 빌드 도구를 탐색하며, 없을 경우 다운로드하여 Tools/Ninja에 설치합니다."""
    exeName = "ninja.exe" if platform.system() == "Windows" else "ninja"

    def extractNinjaInternal(archive: Path, destDir: Path, localExe: Path, exe: str) -> None:
        extractZipSafe(archive, destDir)

    return setupBuildToolInternal(
        "SetupNinja", exeName, kKeyNinjaToolsSubdir,
        kKeyNinjaSearchRoots, kKeyNinjaDownloadUrls, extractNinjaInternal
    )
