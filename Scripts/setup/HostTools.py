"""
Scripts/setup/HostTools.py

호스트 SDK/컴파일러 경로 탐색 (MSVC, Windows SDK, DXC, system includes).
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
from functools import lru_cache
from pathlib import Path
from typing import Callable

from common import (
    ToolSpec,
    ensureCachedDownload,
    extractTarSafe,
    extractZipSafe,
    findFirstExistingFileInBinDirs,
    findFirstExistingFileRecursive,
    findToolRoot,
    kKeyNinjaDownloadUrls,
    kKeyNinjaSearchRoots,
    kKeyNinjaToolsSubdir,
    kKeySccacheDownloadUrls,
    kKeySccacheSearchRoots,
    kKeySccacheToolsSubdir,
    kKeyVcpkgInstalledRel,
    loadSearchPaths,
    normalizePath,
    platformKey,
    recordEnginePath,
    resolveToolsSubdir,
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

    try:
        command = [
            str(vswhere), "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools",
            "-property", "installationPath",
        ]
        vsPath = subprocess.check_output(command, text=True, encoding="utf-8", errors="replace").strip()
        if vsPath:
            msvcBase = Path(vsPath) / "VC" / "Tools" / "MSVC"
            if msvcBase.is_dir():
                versionList = [dirName for dirName in os.listdir(msvcBase) if (msvcBase / dirName).is_dir()]
                if versionList:
                    versionList.sort(reverse=True)
                    return normalizePath(msvcBase / versionList[0])
    except Exception:
        pass
    return ""


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
    try:
        import winreg

        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots"
        )
        kitsRoot, _ = winreg.QueryValueEx(key, "KitsRoot10")
        winreg.CloseKey(key)
        if kitsRoot and Path(kitsRoot).exists():
            includeDir = Path(kitsRoot) / "Include"
            if includeDir.is_dir():
                versionList = [dirName for dirName in os.listdir(includeDir) if dirName.startswith("10.")]
                if versionList:
                    versionList.sort(reverse=True)
                    return normalizePath(kitsRoot), versionList[0]
    except Exception:
        pass
    return "", ""


def findSystemIncludeDirs() -> list[str]:
    """
    시스템 컴파일러(GCC/Clang) 또는 MSVC의 기본 시스템 Include 디렉터리 목록을 반환합니다.
    (IntelliSense 및 ReflectionParser 참고용)

    Returns:
        시스템 Include 경로 문자열 리스트
    """
    includeDirs: list[str] = []
    if platform.system() != "Darwin":
        return includeDirs
    try:
        sdkPath = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
        if sdkPath and Path(sdkPath).exists():
            usrIncludeDir = Path(sdkPath) / "usr" / "include"
            if usrIncludeDir.is_dir():
                includeDirs.append(normalizePath(usrIncludeDir))
    except Exception:
        pass
    return includeDirs


# ==============================================================================
# 빌드 도구 탐색 및 다운로드 (Ninja / Sccache)
# ==============================================================================

def setupBuildToolInternal(toolName: str,
                           exeName: str,
                           subdirKey: str,
                           searchRootsKey: str,
                           downloadUrlsKey: str,
                           extractFunc: Callable[[Path, Path, Path, str], None]) -> str:
    """
    공통 도구(Ninja, Sccache 등) 탐색 및 다운로드 추상화 함수.
    """
    import logging
    logger = logging.getLogger("SetupEnvironment")
    logger.info(f"[{toolName}] Checking {toolName}...")
    
    search = loadSearchPaths()
    toolsDir = resolveToolsSubdir(subdirKey, search)
    localExePath = toolsDir / exeName
    spec = ToolSpec(
        name=toolName,
        tools_subdir_key=subdirKey,
        search_roots_key=searchRootsKey,
        bin_names=(exeName, exeName.replace(".exe", "") if platform.system() == "Windows" else exeName),
        validate_func=lambda root: root.is_file() or (root / exeName).is_file(),
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
    archivePath = ensureCachedDownload(
        url, toolsCacheDir() / archiveName, minSize=50_000, label=toolName
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


def setupSccache() -> str:
    """시스템 PATH 또는 search_paths.json에서 sccache(컴파일 캐시)를 탐색하며, 없을 경우 다운로드하여 Tools/Sccache에 설치합니다."""
    exeName = "sccache.exe" if platform.system() == "Windows" else "sccache"
    
    def extractSccacheInternal(archive: Path, destDir: Path, localExe: Path, exe: str) -> None:
        tempExtDir = toolsCacheDir() / "sccache_extracted"
        if tempExtDir.is_dir():
            shutil.rmtree(tempExtDir, ignore_errors=True)
        extractTarSafe(archive, tempExtDir, mode="r:gz")

        if extractedExe := next((cand for cand in tempExtDir.rglob(exe) if cand.is_file()), None):
            shutil.copy2(extractedExe, localExe)
        shutil.rmtree(tempExtDir, ignore_errors=True)
        
    return setupBuildToolInternal(
        "SetupSccache", exeName, kKeySccacheToolsSubdir,
        kKeySccacheSearchRoots, kKeySccacheDownloadUrls, extractSccacheInternal
    )
