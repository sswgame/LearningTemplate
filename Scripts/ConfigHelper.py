r"""
Scripts/ConfigHelper.py

Scripts 공통 유틸:
  - 프로젝트 루트 / toolchain_config / search_paths I/O
  - 도구 bootstrap 헬퍼 (다운로드, search_roots, 플래그)

네이밍: 공개 camelCase, 비공개 camelCaseInternal.
JSON 키 문자열은 snake_case 유지.
"""

from __future__ import annotations

import functools
import hashlib
import json
import os
import platform
import re
import shutil
import sys
import tarfile
import zipfile
import urllib.request
from pathlib import Path

# Windows console encoding defense
if sys.platform == "win32":
    try:
        if hasattr( sys.stdout, "reconfigure" ):
            sys.stdout.reconfigure( encoding="utf-8" )
        if hasattr( sys.stderr, "reconfigure" ):
            sys.stderr.reconfigure( encoding="utf-8" )
    except Exception:
        pass
from typing import Any, Callable, Dict, Iterable, Iterator, List, Optional

# --- Constants ---------------------------------------------------------------

kDirConfigEnv = "Config/Environment"
kFileToolchainConfig = "toolchain_config.json"
kFileParserConfig = "parser_config.json"
kFileParserDefaults = "parser_config.defaults.json"
kFileSearchPaths = "search_paths.json"
kFileSearchPathsDefaults = "search_paths.defaults.json"

kDirToolsCache = "Tools/_cache"
kDirToolsNinja = "Tools/Ninja"
kDirToolsSccache = "Tools/Sccache"
kDirToolsVcpkg = "Tools/vcpkg"
kDirToolsLlvm = "Tools/LLVM"
kDirToolsReflectionTemplates = "Tools/ReflectionParser/Templates"
kDirVcpkgInstalledRelDefault = "build/vcpkg_installed"

kDirSourceEngine = "Source/Engine"
kDirSourceGames = "Source/Games"
kDirSourceGameFramework = "Source/GameFramework"
kDirSourceApp = "Source/App"
kDirSourceEditor = "Source/Editor"
kDirSourceCore = "Source/Core"

kFileReflectBuiltins = "Source/Engine/Reflection/ReflectBuiltins.xxx"
kFileGameObjectManagerInternal = "GameObjectManagerInternal.h"
kFileEngineServices = "Engine/Common/EngineServices.h"
kFileAnnotationMeta = "Source/Core/Predefined/AnnotationMeta.txt"
kFileRuntimeEngineConfig = "Config/Engine/EngineConfig.json"
kFileRuntimeAppConfig = "Config/App/AppConfig.json"
kFileRuntimeEditorConfig = "Config/Editor/EditorConfig.json"
kFileRuntimeGameConfig = "Config/Game/GameConfig.json"
kFileRuntimeEditorData = "Config/Editor/editordata.xml"
kFileShippingHostDefaultsHeader = "ShippingHostDefaults.h"
kFileTplBuiltinHeader = "BuiltinFileHeader.tpl"
kFileTplBuiltinRegistrar = "BuiltinTypeRegistrar.tpl"
kFileTplBuiltinFooter = "BuiltinFileFooter.tpl"

kScriptSetupVcpkg = "Scripts/setup/SetupVcpkg.py"
kScriptSetupEnvironment = "Scripts/setup/SetupEnvironment.py"
kScriptLintCheckEngineLayers = "Scripts/lint/CheckEngineLayers.py"
kScriptLintCheckSourceGlob = "Scripts/lint/CheckSourceGlob.py"
kScriptLintRunClangFormat = "Scripts/lint/RunClangFormat.py"

kKeyTargetPlatform = "target_platform"
kKeyTargetArch = "target_arch"
kKeyParserArgs = "parser_args"
# Nested schema (parser_config.defaults.json)
kKeyParserArgsSection = "parser_args"
kKeyParserArgsDefault = "default"
kKeyParserArgsPlatform = "platform"
kKeyParserArgsExtra = "extra"
kKeyPaths = "paths"
kKeyClangFlags = "clang_flags"
kKeyEmit = "emit"
kKeyTuning = "tuning"
kKeyLlvmPath = "llvm_path"
kKeyLibclangDllPath = "libclang_dll_path"
kKeyWindowsSdkDir = "windows_sdk_dir"
kKeyWindowsSdkVersion = "windows_sdk_version"
kKeyDxcDllPath = "dxc_dll_path"
kKeyDxilDllPath = "dxil_dll_path"

kTargetGameModule = "SWGame"
kTargetEditorModule = "EditorModule"

kKeyMsvcToolsDir = "msvc_tools_dir"
kKeyVcpkgRoot = "vcpkg_root"
kKeyNinjaPath = "ninja_path"
kKeySccachePath = "sccache_path"
kKeySystemIncludeDirs = "system_include_dirs"

kKeyNinjaToolsSubdir = "ninja_tools_subdir"
kKeyNinjaSearchRoots = "ninja_search_roots"
kKeyNinjaDownloadUrls = "ninja_download_urls"

kKeySccacheToolsSubdir = "sccache_tools_subdir"
kKeySccacheSearchRoots = "sccache_search_roots"
kKeySccacheDownloadUrls = "sccache_download_urls"

kKeyLlvmToolsSubdir = "llvm_tools_subdir"
kKeyLlvmSearchRoots = "llvm_search_roots"
kKeyLlvmDownloadUrls = "llvm_download_urls"
kKeyLlvmAutoBootstrap = "llvm_auto_bootstrap"

kKeyVcpkgToolsSubdir = "vcpkg_tools_subdir"
kKeyVcpkgSearchRoots = "vcpkg_search_roots"
kKeyVcpkgGitUrl = "vcpkg_git_url"
kKeyVcpkgGitCommit = "vcpkg_git_commit"
kKeyVcpkgAutoBootstrap = "vcpkg_auto_bootstrap"
kKeyVcpkgInstalledRel = "vcpkg_installed_rel"

kEnvSwLlvmAutoBootstrap = "SW_LLVM_AUTO_BOOTSTRAP"
kEnvSwVcpkgAutoBootstrap = "SW_VCPKG_AUTO_BOOTSTRAP"

# --- path / config I/O -------------------------------------------------------

def ensureScriptsOnPath() -> Path:
    """
    Scripts/ 를 sys.path에 넣는다 (멱등).

    하위 스크립트 표준 진입 (ConfigHelper import 전)::

        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from ConfigHelper import getProjectRoot, ...
    """
    scriptsDir = Path(__file__).resolve().parent
    scriptsStr = str(scriptsDir)
    if scriptsStr not in sys.path:
        sys.path.insert(0, scriptsStr)
    return scriptsDir

@functools.lru_cache(maxsize=1)
def getProjectRoot() -> Path:
    """
    CMakeLists.txt 파일이 존재하는 디렉터리를 찾아 프로젝트 최상위 루트 경로를 반환합니다.
    """
    start = Path(__file__).resolve().parent
    for candidate in [start, *start.parents]:
        if (candidate / "CMakeLists.txt").is_file():
            return candidate
    return start.parent

def normalizePath(pathStr: str) -> str:
    """
    경로 문자열을 정규화하여 POSIX 스타일(슬래시 사용) 문자열로 반환합니다.
    """
    if not pathStr:
        return ""
    return str(Path(pathStr).as_posix())

def expandPathTemplate(template: str, extras: Optional[Dict[str, str]] = None) -> str:
    """
    경로 템플릿 내의 예약된 변수(${sourceDir}, ${ProjectRoot})와
    추가 변수(extras), 환경 변수를 실제 값으로 확장하여 반환합니다.
    """
    if not template:
        return ""
    root = str(getProjectRoot())
    expanded = template.replace("${sourceDir}", root).replace("${ProjectRoot}", root)
    if extras:
        for key, value in extras.items():
            expanded = expanded.replace(f"${{{key}}}", str(value))

    def replaceBraceEnvInternal(match: re.Match[str]) -> str:
        key = match.group(1)
        val = os.environ.get(key)
        return val if val is not None else match.group(0)

    expanded = re.sub(r"\$\{([A-Za-z0-9_()]+)\}", replaceBraceEnvInternal, expanded)
    return os.path.expandvars(expanded)

def loadToolchainConfig() -> Dict[str, Any]:
    """
    Config/Environment/toolchain_config.json (개발 PC 툴체인 경로 캐시)을 읽습니다.
    """
    root = getProjectRoot()
    configFile = root / kDirConfigEnv / kFileToolchainConfig
    if configFile.exists():
        try:
            with open(configFile, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                return data
        except Exception as exc:
            sys.stderr.write(f"[ConfigHelper] Failed to read {kFileToolchainConfig}: {exc}\n")
    return {}


def updateToolchainConfig(key: str, value: Any) -> None:
    """
    Config/Environment/toolchain_config.json 에 키-값을 갱신합니다.
    (동일 값이면 쓰기를 생략해 CMake 재구성을 막습니다.)
    """
    projectRoot = getProjectRoot()
    configDir = projectRoot / kDirConfigEnv
    configFile = configDir / kFileToolchainConfig
    configDir.mkdir(parents=True, exist_ok=True)
    configData = loadToolchainConfig()
    normalizedVal = normalizePath(str(value)) if isinstance(value, (str, Path)) else value
    if configData.get(key) == normalizedVal:
        return
    configData[key] = normalizedVal
    with open(configFile, "w", encoding="utf-8") as f:
        json.dump(configData, f, indent=4)

def readJsonDictInternal(path: Path, label: str) -> Dict[str, Any]:
    """
    JSON 객체를 읽습니다. 없거나 깨져 있으면 빈 dict입니다.
    """
    if not path.exists():
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        if isinstance(data, dict):
            return data
    except Exception as exc:
        sys.stderr.write(f"[ConfigHelper] Failed to read {label}: {exc}\n")
    return {}

def mergeJsonDictInternal(base: Dict[str, Any], override: Dict[str, Any]) -> Dict[str, Any]:
    """
    defaults 위에 로컬 값을 올립니다.

    중첩 dict만 재귀 병합하고, 리스트·스칼라는 로컬이 있으면 통째로 교체합니다.
    로컬에 없는 키(새 도구 URL 등)는 defaults를 그대로 씁니다.
    """
    merged = dict(base)
    for key, value in override.items():
        existing = merged.get(key)
        if isinstance(existing, dict) and isinstance(value, dict):
            merged[key] = mergeJsonDictInternal(existing, value)
        else:
            merged[key] = value
    return merged

def loadSearchPaths() -> Dict[str, Any]:
    """
    경로 탐색 설정을 읽습니다.

    search_paths.json이 없으면 defaults를 복사해 시드합니다.
    이미 있어도 defaults와 합쳐서, 예전에 복사된 파일에 없는 키를 채웁니다.
    """
    root = getProjectRoot()
    configDir = root / kDirConfigEnv
    jsonPath = configDir / kFileSearchPaths
    defaultsPath = configDir / kFileSearchPathsDefaults
    if not jsonPath.exists() and defaultsPath.exists():
        try:
            configDir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(defaultsPath, jsonPath)
        except Exception as e:
            sys.stderr.write(f"[ConfigHelper] Failed to copy {kFileSearchPathsDefaults}: {e}\n")

    defaults = readJsonDictInternal(defaultsPath, kFileSearchPathsDefaults)
    local = readJsonDictInternal(jsonPath, kFileSearchPaths)
    if not defaults and not local:
        return {}
    return mergeJsonDictInternal(defaults, local)

def getOrFindCached(existing: Dict[str, Any],
                    key: str,
                    findFunc,
                    *args,
                    validate: Optional[Any] = None) -> Any:
    """
    기존 설정 딕셔너리에서 값을 찾고 유효하면 캐시된 값을 반환하며,
    그렇지 않으면 탐색 함수(findFunc)를 호출하여 새 값을 찾습니다.
    """
    val = existing.get(key)
    if isinstance(val, list) and val:
        return val
    if val and isinstance(val, str):
        ok = validate(val) if callable(validate) else os.path.exists(val)
        if ok:
            return val
    elif val and not isinstance(val, str):
        return val
    return findFunc(*args)

def findFirstExistingFile(searchDirs: List[Path], fileNames: List[str]) -> str:
    """
    주어진 디렉터리 목록에서 지정된 파일명 중 가장 먼저 존재하는 파일의 경로를 반환합니다.
    """
    for directory in searchDirs:
        if not directory.exists():
            continue
        for fileName in fileNames:
            candidate = directory / fileName
            if candidate.is_file():
                return str(candidate)
    return ""

_kRglobSkipDirNames = {"buildtrees", "downloads", "packages"}

def findFirstExistingFileRecursive(searchDirs: List[Path], fileNames: List[str]) -> str:
    """
    주어진 디렉터리 목록을 재귀적으로 탐색하여 가장 먼저 발견된 파일의 경로를 반환합니다.
    vcpkg buildtrees/downloads/packages 는 건너뜁니다.
    """
    for directory in searchDirs:
        if not directory.exists() or not directory.is_dir():
            continue
        for fileName in fileNames:
            try:
                for candidate in directory.rglob(fileName):
                    if not candidate.is_file():
                        continue
                    if any(part in _kRglobSkipDirNames for part in candidate.parts):
                        continue
                    return str(candidate)
            except (OSError, PermissionError):
                pass
    return ""

def findFirstExistingFileInBinDirs(searchDirs: List[Path], fileNames: List[str]) -> str:
    """
    vcpkg_installed/<triplet>/bin 과 debug/bin 을 rglob보다 먼저 찾습니다.
    """
    for directory in searchDirs:
        if not directory.exists() or not directory.is_dir():
            continue
        try:
            tripletRoots = [p for p in directory.iterdir() if p.is_dir()]
        except (OSError, PermissionError):
            tripletRoots = []
        for root in [directory, *tripletRoots]:
            for sub in ("bin", "debug/bin"):
                folder = root / sub
                if not folder.is_dir():
                    continue
                for fileName in fileNames:
                    candidate = folder / fileName
                    if candidate.is_file():
                        return str(candidate)
    return findFirstExistingFileRecursive(searchDirs, fileNames)

# --- tooling bootstrap helpers -----------------------------------------------

def platformKey() -> str:
    """
    현재 시스템에 대한 플랫폼 키('windows', 'darwin', 'linux')를 반환합니다.
    """
    sysName = platform.system().lower()
    if sysName.startswith("win"):
        return "windows"
    if sysName == "darwin":
        return "darwin"
    return "linux"

def toolsCacheDir() -> Path:
    """
    도구 다운로드 등을 위한 캐시 디렉터리 경로(Tools/_cache)를 반환합니다.
    """
    d = getProjectRoot() / kDirToolsCache
    d.mkdir(parents=True, exist_ok=True)
    return d

def resolveToolsSubdir(subdirKey: str, default: str, search: Optional[Dict] = None) -> Path:
    """
    탐색 설정(search_paths)에서 도구의 하위 디렉터리 경로를 찾아 프로젝트 루트 기준의 절대 경로로 반환합니다.
    """
    cfg = search if search is not None else loadSearchPaths()
    subdir = cfg.get(subdirKey, default)
    return (getProjectRoot() / str(subdir)).resolve()

def resolveGitExecutable() -> Optional[str]:
    """
    PATH 및 Windows 기본 설치 경로에서 git 실행 파일을 찾습니다.
    CMake Tools 등 PATH가 얇은 환경에서도 Git for Windows를 잡기 위함입니다.
    """
    found = shutil.which("git")
    if found:
        return found

    if sys.platform == "win32":
        programFiles = os.environ.get("ProgramFiles", r"C:\Program Files")
        programFilesX86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        localAppData = os.environ.get("LocalAppData", "")
        candidateList = [
            Path(programFiles) / "Git" / "cmd" / "git.exe",
            Path(programFiles) / "Git" / "bin" / "git.exe",
            Path(programFilesX86) / "Git" / "cmd" / "git.exe",
            Path(programFilesX86) / "Git" / "bin" / "git.exe",
        ]
        if localAppData:
            candidateList.append(Path(localAppData) / "Programs" / "Git" / "cmd" / "git.exe")
            candidateList.append(Path(localAppData) / "Programs" / "Git" / "bin" / "git.exe")
        for candidate in candidateList:
            if candidate.is_file():
                return str(candidate.resolve())
    return None

def ensureGitOnPath() -> Optional[str]:
    """
    git을 찾아 PATH 앞에 넣고, 실행 파일 경로를 반환합니다.
    없으면 None.
    """
    gitPath = resolveGitExecutable()
    if gitPath is None:
        return None

    gitDir = str(Path(gitPath).parent)
    pathParts = os.environ.get("PATH", "").split(os.pathsep)
    pathLower = { part.lower() for part in pathParts if part }
    if gitDir.lower() not in pathLower:
        os.environ["PATH"] = gitDir + os.pathsep + os.environ.get("PATH", "")
    return gitPath

def autoBootstrapEnabled(force: bool,
                         configKey: str,
                         envKey: str,
                         *,
                         default: bool = False,
                         search: Optional[Dict] = None) -> bool:
    """
    환경 변수나 설정 파일, 강제 옵션 등을 확인하여 자동 부트스트랩(다운로드 등)이 활성화되어 있는지 여부를 반환합니다.
    """
    if force:
        return True
    cfg = search if search is not None else loadSearchPaths()
    if bool(cfg.get(configKey, default)):
        return True
    flag = os.environ.get(envKey, "").strip().lower()
    return flag in ("1", "true", "on", "yes")

def recordEnginePath(key: str, path: str | Path) -> str:
    """
    주어진 엔진 관련 경로를 정규화하여 설정 파일에 기록한 후 반환합니다.
    """
    resolved = normalizePath(str(path))
    updateToolchainConfig(key, resolved)
    return resolved

def downloadUrl(url: str, dest: Path, *, label: str = "Download") -> None:
    """
    주어진 URL에서 파일을 다운로드하여 지정된 대상(dest) 경로에 저장합니다.
    """
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"[{label}] Downloading {url}")
    lastPct = [-1]

    def hookInternal(blockNum: int, blockSize: int, totalSize: int) -> None:
        if totalSize <= 0:
            return
        downloaded = blockNum * blockSize
        pct = min(100, int(downloaded * 100 / totalSize))
        if pct >= lastPct[0] + 5 or pct == 100:
            lastPct[0] = pct
            print(
                f"[{label}]   {pct}% "
                f"({downloaded // (1024 * 1024)} / {totalSize // (1024 * 1024)} MiB)"
            )

    urllib.request.urlretrieve(url, dest, reporthook=hookInternal)

def isSafeArchiveMember(name: str, destRoot: Path) -> bool:
    """
    Zip Slip 보안 취약점을 방지하기 위해 아카이브 내 파일 경로가 대상 디렉터리를 벗어나는지(상위 탐색) 검사합니다.

    Args:
        name: 아카이브 내 항목 이름
        destRoot: 압축 해제 대상 루트 디렉터리 경로

    Returns:
        안전한 경로인 경우 True, 경로 탈출 시도시 False
    """
    norm = name.replace("\\", "/")
    if norm.startswith("/") or ".." in Path(norm).parts:
        return False
    try:
        target = (destRoot / norm).resolve()
        destRoot = destRoot.resolve()
        return destRoot == target or destRoot in target.parents
    except OSError:
        return False

def extractZipSafe(archive: Path, dest: Path) -> None:
    """
    Zip 아카이브를 대상 디렉터리에 안전하게 압축 해제합니다 (경로 순회 공격 방지).

    Args:
        archive: 해제할 Zip 파일 경로
        dest: 대상 디렉터리 경로
    """
    dest.mkdir(parents=True, exist_ok=True)
    destResolved = dest.resolve()
    with zipfile.ZipFile(archive, "r") as zf:
        for info in zf.infolist():
            if not isSafeArchiveMember(info.filename, destResolved):
                continue
            zf.extract(info, destResolved)

def extractTarSafe(archive: Path, dest: Path, *, mode: str = "r:*") -> None:
    """
    Tar 아카이브(.tar.gz, .tar.xz 등)를 대상 디렉터리에 안전하게 압축 해제합니다.

    Args:
        archive: 해제할 Tar 파일 경로
        dest: 대상 디렉터리 경로
        mode: tarfile 열기 모드 (기본 "r:*")
    """
    dest.mkdir(parents=True, exist_ok=True)
    destResolved = dest.resolve()
    with tarfile.open(archive, mode) as tar:
        members = [m for m in tar.getmembers() if isSafeArchiveMember(m.name, destResolved)]
        tar.extractall(path=destResolved, members=members)

def fileSha256(path: Path) -> str:
    """
    주어진 파일의 SHA-256 해시 문자열(소문자 16진수)을 계산하여 반환합니다.

    Args:
        path: 해시를 계산할 파일 경로

    Returns:
        64자리 16진수 SHA-256 해시 문자열
    """
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def ensureCachedDownload(url: str,
                         dest: Path,
                         *,
                         minSize: int = 10_000,
                         label: str = "Download",
                         sha256: Optional[str] = None) -> Path:
    """
    도구 아카이브를 캐시 디렉터리에 다운로드하고, 사이드카 .sha256 해시를 검증하여 캐시를 재사용합니다.

    Args:
        url: 다운로드할 URL
        dest: 저장할 대상 파일 경로
        minSize: 정상적인 파일로 간주할 최소 바이트 크기
        label: 콘솔 출력에 사용할 식별자
        sha256: 기대되는 SHA-256 해시값 (옵션)

    Returns:
        다운로드/캐시된 파일 경로
    """
    sidecar = dest.with_name(dest.name + ".sha256")
    expected = (sha256 or "").strip().lower()

    def writeSidecarInternal(digest: str) -> None:
        sidecar.write_text(digest + "\n", encoding="utf-8")

    def cacheValidInternal() -> bool:
        if not dest.exists() or dest.stat().st_size < minSize:
            return False
        digest = fileSha256(dest)
        if expected:
            if digest == expected:
                return True
            print(f"[{label}] SHA256 mismatch for cache {dest}, re-downloading")
            return False
        if sidecar.exists():
            recorded = sidecar.read_text(encoding="utf-8").strip().split()[0].lower()
            if digest == recorded:
                return True
            print(f"[{label}] SHA256 sidecar mismatch for {dest}, re-downloading")
            return False
        writeSidecarInternal(digest)
        return True

    if cacheValidInternal():
        print(f"[{label}] Reusing cached: {dest}")
        return dest

    downloadUrl(url, dest, label=label)
    digest = fileSha256(dest)
    if dest.stat().st_size < minSize:
        dest.unlink(missing_ok=True)
        raise RuntimeError(f"[{label}] Download too small: {dest}")
    if expected and digest != expected:
        dest.unlink(missing_ok=True)
        raise RuntimeError(f"[{label}] SHA256 mismatch for {dest}")
    writeSidecarInternal(expected or digest)
    return dest

def iterSearchRootTemplates(templates: Iterable[str],
                            *,
                            extras: Optional[Dict[str, str]] = None,
                            skip: Optional[Path] = None) -> Iterator[Path]:
    """
    경로 템플릿 목록을 순회하며 확장하고, 유효한 디렉터리(스킵 조건 제외)들을 생성(yield)합니다.
    """
    skipResolved: Optional[Path] = None
    if skip is not None:
        try:
            skipResolved = skip.resolve()
        except OSError:
            skipResolved = None

    for template in templates:
        root = Path(expandPathTemplate(str(template), extras=extras))
        if not root.exists():
            continue
        if skipResolved is not None:
            try:
                if root.resolve() == skipResolved:
                    continue
            except OSError:
                pass
        yield root

def findFirstValidRoot(templates: Iterable[str],
                       isValid: Callable[[Path], bool],
                       *,
                       extras: Optional[Dict[str, str]] = None,
                       skip: Optional[Path] = None) -> Optional[Path]:
    """
    주어진 경로 템플릿 목록에서 isValid 함수를 만족하는 첫 번째 유효한 루트 경로를 반환합니다.
    """
    for root in iterSearchRootTemplates(templates, extras=extras, skip=skip):
        if isValid(root):
            return root.resolve()
    return None

def platformSearchRoots(search: Dict, rootsKey: str) -> List[str]:
    """
    설정 파일 딕셔너리에서 현재 플랫폼에 맞는 탐색 루트 목록을 반환합니다.
    """
    raw = search.get(rootsKey, [])
    if isinstance(raw, dict):
        return list(raw.get(platformKey(), []) or [])
    if isinstance(raw, list):
        return [str(x) for x in raw]
    return []

def wipeDirContents(path: Path) -> None:
    """
    주어진 디렉터리 내의 모든 파일 및 하위 디렉터리를 삭제합니다.
    """
    if not path.exists():
        return
    for child in path.iterdir():
        if child.is_dir():
            shutil.rmtree(child, ignore_errors=True)
        else:
            child.unlink(missing_ok=True)

def isVcpkgRoot(path: Path | str) -> bool:
    """
    주어진 경로가 vcpkg 루트 디렉터리인지 확인합니다 (내부 구조를 통해 검사).
    """
    try:
        return (Path(path) / "scripts" / "buildsystems" / "vcpkg.cmake").is_file()
    except OSError:
        return False
