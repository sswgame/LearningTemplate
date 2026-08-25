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
from typing import List, Tuple

from ConfigHelper import (
    ensureCachedDownload,
    extractTarSafe,
    extractZipSafe,
    findFirstExistingFileInBinDirs,
    findFirstExistingFileRecursive,
    findFirstValidRoot,
    kDirToolsNinja,
    kDirToolsSccache,
    kDirVcpkgInstalledRelDefault,
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
    platformSearchRoots,
    recordEnginePath,
    resolveToolsSubdir,
    toolsCacheDir,
)

def findVcpkgInstalledDirsInternal(projectRoot: Path) -> List[Path]:
    """
    프로젝트에서 사용 중인 vcpkg installed 디렉터리 목록을 탐색하여 반환합니다.

    Args:
        projectRoot: 프로젝트 최상위 경로

    Returns:
        vcpkg installed 경로(Path) 리스트
    """
    result: List[Path] = []
    search = loadSearchPaths()
    rel = search.get(kKeyVcpkgInstalledRel, kDirVcpkgInstalledRelDefault)
    preferred = projectRoot / rel
    if preferred.is_dir():
        result.append(preferred)
    buildDir = projectRoot / "build"
    if buildDir.is_dir():
        direct = buildDir / "vcpkg_installed"
        if direct.is_dir() and direct not in result:
            result.append(direct)
        try:
            for child in buildDir.iterdir():
                if not child.is_dir():
                    continue
                candidate = child / "vcpkg_installed"
                if candidate.is_dir() and candidate not in result:
                    result.append(candidate)
        except (OSError, PermissionError):
            pass
    return result

def findDxcDlls(sdkDir: str, sdkVer: str, projectRoot: Path) -> Tuple[str, str]:
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
    sysName = platform.system()
    if sysName == "Windows":
        dxcNames, dxilNames = ["dxcompiler.dll"], ["dxil.dll"]
    elif sysName == "Darwin":
        dxcNames, dxilNames = ["libdxcompiler.dylib"], ["libdxil.dylib"]
    else:
        dxcNames = ["libdxcompiler.so", "libdxcompiler.so.1"]
        dxilNames = ["libdxil.so", "libdxil.so.1"]

    dxcDll, dxilDll = "", ""
    vcpkgDirs = findVcpkgInstalledDirsInternal(projectRoot)
    if vcpkgDirs:
        dxcDll = findFirstExistingFileInBinDirs(vcpkgDirs, dxcNames)
        dxilDll = findFirstExistingFileInBinDirs(vcpkgDirs, dxilNames)

    if not dxcDll or not dxilDll:
        vulkanSdk = os.environ.get("VULKAN_SDK")
        if vulkanSdk:
            vulkanRoot = Path(vulkanSdk)
            if not dxcDll:
                dxcDll = findFirstExistingFileRecursive([vulkanRoot], dxcNames)
            if not dxilDll:
                dxilDll = findFirstExistingFileRecursive([vulkanRoot], dxilNames)

    if sysName == "Windows" and sdkDir and sdkVer:
        sdkRoot = Path(sdkDir)
        if not dxcDll:
            dxcDll = findFirstExistingFileRecursive([sdkRoot], dxcNames)
        if not dxilDll:
            dxilDll = findFirstExistingFileRecursive([sdkRoot], dxilNames)

    if not dxcDll:
        for name in dxcNames:
            found = shutil.which(name)
            if found:
                dxcDll = normalizePath(found)
                break
    if not dxilDll:
        for name in dxilNames:
            found = shutil.which(name)
            if found:
                dxilDll = normalizePath(found)
                break
    return normalizePath(dxcDll), normalizePath(dxilDll)

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
    envVc = os.environ.get("VCToolsInstallDir")
    if envVc and os.path.exists(envVc):
        return normalizePath(envVc)

    pf = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    if not pf:
        return ""
    vswhere = os.path.join(pf, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return ""

    try:
        cmd = [
            vswhere, "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools",
            "-property", "installationPath",
        ]
        vsPath = subprocess.check_output(cmd, text=True, encoding="utf-8", errors="replace").strip()
        if not vsPath:
            vsPath = subprocess.check_output(
                [vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                text=True, encoding="utf-8", errors="replace",
            ).strip()
        if vsPath:
            msvcBase = os.path.join(vsPath, "VC", "Tools", "MSVC")
            if os.path.exists(msvcBase):
                vers = [d for d in os.listdir(msvcBase) if os.path.isdir(os.path.join(msvcBase, d))]
                if vers:
                    vers.sort(reverse=True)
                    return normalizePath(os.path.join(msvcBase, vers[0]))
    except Exception:
        pass
    return ""

@lru_cache(maxsize=1)
def findWindowsSdkPath() -> Tuple[str, str]:
    """
    시스템에 설치된 최신 Windows 10/11 SDK 경로와 버전을 레지스트리에서 탐색하여 반환합니다.

    Returns:
        (SDK 설치 경로, SDK 버전) 튜플. (Windows 환경이 아니거나 찾지 못하면 빈 문자열 튜플)
    """
    if platform.system() != "Windows":
        return "", ""
    envSdk = os.environ.get("WindowsSdkDir")
    if envSdk and os.path.exists(envSdk):
        version = os.environ.get("WindowsSDKVersion", "").strip("\\")
        return normalizePath(envSdk), version
    try:
        import winreg

        key = winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots"
        )
        kitsRoot, _ = winreg.QueryValueEx(key, "KitsRoot10")
        winreg.CloseKey(key)
        if kitsRoot and os.path.exists(kitsRoot):
            incDir = os.path.join(kitsRoot, "Include")
            if os.path.exists(incDir):
                vers = [d for d in os.listdir(incDir) if d.startswith("10.")]
                if vers:
                    vers.sort(reverse=True)
                    return normalizePath(kitsRoot), vers[0]
    except Exception:
        pass
    return "", ""

def findSystemIncludeDirs() -> List[str]:
    """
    시스템 컴파일러(GCC/Clang) 또는 MSVC의 기본 시스템 Include 디렉터리 목록을 반환합니다.
    (IntelliSense 및 ReflectionParser 참고용)

    Returns:
        시스템 Include 경로 문자열 리스트
    """
    includeDirs: List[str] = []
    if platform.system() != "Darwin":
        return includeDirs
    try:
        sdkPath = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
        if sdkPath and os.path.exists(sdkPath):
            usrInc = os.path.join(sdkPath, "usr", "include")
            if os.path.exists(usrInc):
                includeDirs.append(normalizePath(usrInc))
    except Exception:
        pass
    return includeDirs

# ==============================================================================
# Build Tool Fetching (Ninja / Sccache)
# ==============================================================================

def setupBuildToolInternal(
    toolName: str,
    exeName: str,
    subdirKey: str,
    defaultSubdir: str,
    searchRootsKey: str,
    downloadUrlsKey: str,
    extractFunc,
) -> str:
    """
    공통 도구(Ninja, Sccache 등) 탐색 및 다운로드 추상화 함수.
    """
    import logging
    logger = logging.getLogger("SetupEnvironment")
    logger.info(f"[{toolName}] Checking {toolName}...")
    
    search = loadSearchPaths()
    toolsDir = resolveToolsSubdir(subdirKey, defaultSubdir, search)
    local = toolsDir / exeName
    extras = {subdirKey: search.get(subdirKey, defaultSubdir)}

    system = shutil.which(exeName.replace(".exe", "") if platform.system() == "Windows" else exeName)
    if system and Path(system).is_file():
        logger.info(f"[{toolName}] Using PATH: {system}")
        return recordEnginePath(f"{toolName.lower()}_path", system)

    def isToolInternal(root: Path) -> bool:
        return root.is_file() or (root / exeName).is_file()

    found = findFirstValidRoot(
        platformSearchRoots(search, searchRootsKey),
        isToolInternal,
        extras=extras,
        skip=toolsDir,
    )
    if found:
        exe = found if found.is_file() else found / exeName
        logger.info(f"[{toolName}] Using search root: {exe}")
        return recordEnginePath(f"{toolName.lower()}_path", exe)

    if local.is_file():
        logger.info(f"[{toolName}] Using project kit: {local}")
        return recordEnginePath(f"{toolName.lower()}_path", local)

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
        extractFunc(archivePath, toolsDir, local, exeName)
        if local.is_file():
            if platform.system() != "Windows":
                os.chmod(local, 0o755)
            logger.info(f"[{toolName}] Installed: {local}")
            return recordEnginePath(f"{toolName.lower()}_path", local)
    except Exception as exc:
        logger.error(f"[{toolName} Error] {exc}")
    return ""

def setupNinja() -> str:
    """시스템 또는 프로젝트 내에서 Ninja 빌드 시스템을 탐색합니다."""
    exeName = "ninja.exe" if platform.system() == "Windows" else "ninja"
    
    def extractNinjaInternal(archive: Path, destDir: Path, localExe: Path, exe: str):
        extractZipSafe(archive, destDir)
        
    return setupBuildToolInternal(
        "SetupNinja", exeName, kKeyNinjaToolsSubdir, kDirToolsNinja,
        kKeyNinjaSearchRoots, kKeyNinjaDownloadUrls, extractNinjaInternal
    )

def setupSccache() -> str:
    """시스템 또는 프로젝트 내에서 sccache를 탐색합니다."""
    exeName = "sccache.exe" if platform.system() == "Windows" else "sccache"
    
    def extractSccacheInternal(archive: Path, destDir: Path, localExe: Path, exe: str):
        tempExtDir = toolsCacheDir() / "sccache_extracted"
        if tempExtDir.exists():
            shutil.rmtree(tempExtDir, ignore_errors=True)
        extractTarSafe(archive, tempExtDir, mode="r:gz")

        extractedExe = None
        for p in tempExtDir.rglob(exe):
            if p.is_file():
                extractedExe = p
                break
        if extractedExe:
            shutil.copy2(extractedExe, localExe)
        shutil.rmtree(tempExtDir, ignore_errors=True)
        
    return setupBuildToolInternal(
        "SetupSccache", exeName, kKeySccacheToolsSubdir, kDirToolsSccache,
        kKeySccacheSearchRoots, kKeySccacheDownloadUrls, extractSccacheInternal
    )
