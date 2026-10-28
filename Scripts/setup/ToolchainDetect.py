"""
Scripts/setup/ToolchainDetect.py

도구 · SDK 경로 탐색. 두 층이다.

1. 선언적 도구 탐색(`ToolSpec` · `findToolRoot` — Ninja · LLVM · vcpkg 가 같이 쓴다): OS 환경 변수 → 시스템 PATH →
   search_paths.json 탐색 루트 → 프로젝트 로컬 `Tools/<subdir>` 키트. 없으면 부르는 쪽이 부트스트랩 · 다운로드한다.
2. 호스트 SDK/컴파일러 경로 탐색(MSVC, Windows SDK, DXC, vcpkg installed, Ninja 설치).
"""

from __future__ import annotations

import logging
import os
import platform
import shutil
import sys
from collections.abc import Callable
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

from common.FileCollect import findFirstExistingFileInBinDirs, findFirstExistingFileRecursive, findFirstValidRoot, platformSearchRoots, selectLatestVersion
from common.Paths import normalizePath, platformKey, sharedLibraryNames
from common.ProjectConstants import kKeyNinjaDownloadUrls, kKeyNinjaSearchRoots, kKeyNinjaToolsSubdir, kKeyVcpkgInstalledRel
from common.RunProcess import runProcess
from common.ToolDownload import ensureCachedDownload, extractZipSafe, resolveToolsSubdir, toolsCacheDir
from common.ToolchainConfig import loadSearchPaths, recordEnginePath


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


def findToolRoot(
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
        search: search_paths 딕셔너리 (None일 경우 loadSearchPaths() 호출).
        logger: 로깅에 사용할 Logger 객체 (선택 사항).

    Returns:
        유효한 도구 루트 Path 객체. 찾지 못한 경우 None.
    """
    searchConfig = search if search is not None else loadSearchPaths()
    log = logger or logging.getLogger(spec.name)
    toolsDir = resolveToolsSubdir(spec.tools_subdir_key, searchConfig)
    extras = {spec.tools_subdir_key: searchConfig[spec.tools_subdir_key]}

    def isValid(candidate: Path) -> bool:
        if spec.validate_func is not None:
            return spec.validate_func(candidate)
        return candidate.is_dir()

    # 1. 환경변수 확인
    for envVar in spec.env_vars:
        if (envVal := os.environ.get(envVar)) and isValid(Path(envVal)):
            log.info(f"[{spec.name}] Using {envVar}: {envVal}")
            return Path(normalizePath(envVal))

    # 2. 시스템 PATH 상의 바이너리 확인
    for binName in spec.bin_names:
        if foundBin := shutil.which(binName):
            binPath = Path(foundBin).resolve()
            for parentCandidate in (binPath.parent, binPath.parent.parent):
                if isValid(parentCandidate):
                    log.info(f"[{spec.name}] Using PATH: {parentCandidate}")
                    return Path(normalizePath(str(parentCandidate)))

    # 3. search_paths.json 탐색 루트 확인
    found = findFirstValidRoot(
        platformSearchRoots(searchConfig, spec.search_roots_key),
        isValid,
        extras=extras,
        skip=toolsDir,
    )
    if found:
        log.info(f"[{spec.name}] Using search root: {found}")
        return Path(normalizePath(str(found)))

    # 4. 로컬 Tools/<subdir> 키트 확인
    if isValid(toolsDir):
        log.info(f"[{spec.name}] Using project kit: {toolsDir}")
        return toolsDir

    return None


# ==============================================================================
# 호스트 SDK / 컴파일러 / 빌드 도구 경로
# ==============================================================================

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
        raise KeyError(f"[ToolchainDetect] Missing required key '{kKeyVcpkgInstalledRel}' in search_paths config")
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
        print(f"[ToolchainDetect] MSVC 위치를 vswhere 로 찾지 못했습니다(종료 코드 {result.returnCode}): {result.stderr.strip()}", file=sys.stderr)
        return ""
    vsPath = result.stdout.strip()
    if not vsPath:
        return ""
    msvcBase = Path(vsPath) / "VC" / "Tools" / "MSVC"
    try:
        latestVersion = selectLatestVersion([entry.name for entry in msvcBase.iterdir() if entry.is_dir()]) if msvcBase.is_dir() else ""
    except OSError as error:
        print(f"[ToolchainDetect] MSVC 버전 폴더를 읽지 못했습니다: {msvcBase}: {error}", file=sys.stderr)
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
    # 개발자 셸의 두 변수가 함께 있을 때만 쓴다 — 버전 없이 폴더만 돌려주면 쓰는 쪽이 판을 모른다. 아니면 레지스트리가 정본이다.
    envSdkDir, envSdkVersion = os.environ.get("WindowsSdkDir"), os.environ.get("WindowsSDKVersion")
    if envSdkDir and envSdkVersion and Path(envSdkDir).exists():
        return normalizePath(envSdkDir), envSdkVersion.strip("\\")
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
            kitsRoot, _ = winreg.QueryValueEx(key, "KitsRoot10")
        includeDir = Path(kitsRoot) / "Include"
        latestVersion = selectLatestVersion([entry.name for entry in includeDir.iterdir() if entry.name.startswith("10.")]) if kitsRoot and includeDir.is_dir() else ""
    except OSError as error:
        # 레지스트리 키가 없거나 폴더를 못 읽음 — "설치 안 됨" 과 구별되도록 이유를 남긴다.
        print(f"[ToolchainDetect] Windows SDK 위치를 레지스트리에서 찾지 못했습니다: {error}", file=sys.stderr)
        return "", ""
    return (normalizePath(kitsRoot), latestVersion) if latestVersion else ("", "")


# ==============================================================================
# 빌드 도구 탐색 및 다운로드 (Ninja)
# ==============================================================================

def setupBuildToolInternal(toolName: str,
                           exeName: str,
                           subdirKey: str,
                           searchRootsKey: str,
                           downloadUrlsKey: str) -> str:
    """도구 탐색 및 다운로드(zip 으로 받아 `Tools/<이름>` 에 푼다)."""
    import logging
    logger = logging.getLogger("Environment")
    logger.info(f"[{toolName}] Checking {toolName}...")

    search = loadSearchPaths()
    toolsDir = resolveToolsSubdir(subdirKey, search)
    localExePath = toolsDir / exeName

    def isUsableInternal(root: Path) -> bool:
        exePath = root if root.is_file() else root / exeName
        return exePath.is_file()

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

    url = search[downloadUrlsKey][platformKey()]   # search_paths.defaults 의 필수 키 — 없으면 KeyError

    logger.info(f"[{toolName}] Downloading into {toolsDir}")
    toolsDir.mkdir(parents=True, exist_ok=True)
    archiveName = url.rsplit("/", 1)[-1]
    archivePath = ensureCachedDownload(url, toolsCacheDir() / archiveName, minSize=50_000, label=toolName)

    # 받은 압축을 풀다 실패하면 "도구 없음" 이 아니라 오류다 — 예외 그대로 멈춘다.
    extractZipSafe(archivePath, toolsDir)
    if not localExePath.is_file():
        raise RuntimeError(f"[{toolName}] 압축을 풀었지만 {localExePath} 가 없습니다: {archivePath}")
    if platform.system() != "Windows":
        os.chmod(localExePath, 0o755)
    logger.info(f"[{toolName}] Installed: {localExePath}")
    return recordEnginePath(f"{toolName.lower()}_path", localExePath)


def setupNinja() -> str:
    """시스템 PATH 또는 search_paths.json에서 Ninja 빌드 도구를 탐색하며, 없을 경우 다운로드하여 Tools/Ninja에 설치합니다."""
    exeName = "ninja.exe" if platform.system() == "Windows" else "ninja"

    return setupBuildToolInternal("SetupNinja", exeName, kKeyNinjaToolsSubdir, kKeyNinjaSearchRoots, kKeyNinjaDownloadUrls)
