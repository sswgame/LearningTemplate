"""
Scripts/common/Constants.py

프로젝트 전체에서 공유하는 고정 경로, 파일명, JSON 스키마 키 및 스크립트 진입점 상수 (SSOT).
"""

from __future__ import annotations

# =============================================================================
# --- 1. Config Directories & Files (설정 디렉터리 및 JSON/XML 파일 경로) -------
# =============================================================================

kDirConfig = "Config"
kDirConfigEditor = "Config/Editor"
kDirConfigEnv = "Config/Environment"
kFileToolchainConfig = "toolchain_config.json"
kFileParserConfig = "parser_config.json"
kFileParserDefaults = "parser_config.defaults.json"
kFileSearchPaths = "search_paths.json"
kFileSearchPathsDefaults = "search_paths.defaults.json"

kFileRuntimeEngineConfig = "Config/Engine/EngineConfig.json"
kFileRuntimeEditorConfig = "Config/Editor/EditorConfig.json"
# 게임 프리셋 폴더 — 게임마다 `<SW_ACTIVE_GAME>.json` 하나(팩 루트 · 시작 씬). 활성 게임이 어느 것을 쓸지 고른다.
kDirRuntimeGamePreset = "Config/Game"
# 전용 서버 운영 설정 폴더 — `<SW_ACTIVE_GAME>.json`(ServerConfig). Shipping 도 굽지 않고 디스크에서 읽는다.
kDirRuntimeServerPreset = "Config/Server"
# 사람이 쓰는 에디터 도구 값 — 기본값과 다른 값이 있을 때만 만든다(없으면 기본값).
kFileRuntimeEditorToolDefaults = "Config/Editor/editortooldefaults.json"
kFileShippingHostDefaultsHeader = "ShippingHostDefaults.h"
kFilePackConfig = "Config/Engine/PackConfig.json"

# =============================================================================
# --- 2. Project Source & Reflection Architecture (소스 레이아웃 & 리플렉션 SSOT) -
# =============================================================================

kDirSourceEngine = "Source/Engine"
kDirSourceGames = "Source/Games"
kDirSourceGameFramework = "Source/GameFramework"
kDirSourceApp = "Source/App"
kDirSourceEditor = "Source/Editor"
kDirSourceCore = "Source/Core"
kDirSourceRuntimeAPI = "Source/RuntimeAPI"

kFileReflectBuiltins = "Source/Engine/Reflection/ReflectBuiltins.xxx"
kFileEngineServices = "Engine/Common/EngineServices.h"
kFileAnnotationMeta = "Source/Core/Predefined/AnnotationMeta.txt"
kFileTplBuiltinHeader = "BuiltinFileHeader.tpl"
kFileTplBuiltinRegistrar = "BuiltinTypeRegistrar.tpl"
kFileTplBuiltinFooter = "BuiltinFileFooter.tpl"

kTargetGameModule = "SWGame"
kTargetEditorModule = "EditorModule"

# C++ 파일 확장자 집합 (헤더 / 소스 / 전체)
kCppHeaderExtensions: set[str] = {".h", ".hpp", ".inl"}
kCppSourceExtensions: set[str] = {".c", ".cpp", ".cc", ".cxx"}
kCppAllExtensions: set[str] = {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}

# =============================================================================
# --- 3. Tool Directories & Defaults (내부 도구 디렉터리 및 캐시 경로) ----------
# =============================================================================

kDirToolsCache = "Tools/_cache"
kDirToolsLlvm = "Tools/LLVM"
kDirToolsReflectionTemplates = "Tools/ReflectionParser/Templates"

# =============================================================================
# --- 4. Script Entry Points (스크립트 실행 진입점 경로) ------------------------
# =============================================================================

kScriptSetupVcpkg = "Scripts/setup/SetupVcpkg.py"
kScriptSetupEnvironment = "Scripts/setup/SetupEnvironment.py"
kScriptGenerateShippingHostDefaults = "Scripts/generate/GenerateShippingHostDefaults.py"
kScriptCookAssets = "Scripts/generate/CookAssets.py"
kScriptGenerateDocs = "Scripts/generate/GenerateDocs.py"

# =============================================================================
# --- 5. JSON Schema Keys & Environment Variables (JSON 필드명 및 환경변수 심볼) -
# =============================================================================

# Target / Platform
kKeyTargetPlatform = "target_platform"
kKeyTargetArch = "target_arch"

# Reflection Parser (parser_config.json)
kKeyParserArgsSection = "parser_args"
kKeyParserArgsDefault = "default"
kKeyParserArgsPlatform = "platform"
kKeyParserArgsExtra = "extra"
kKeyParserArgsForceInclude = "force_include"
kKeyPaths = "paths"
kKeyClangFlags = "clang_flags"
kKeyEmit = "emit"
kKeyTuning = "tuning"

# Toolchain Paths (toolchain_config.json)
kKeyLlvmPath = "llvm_path"
kKeyLibclangDllPath = "libclang_dll_path"
kKeyWindowsSdkDir = "windows_sdk_dir"
kKeyWindowsSdkVersion = "windows_sdk_version"
kKeyDxcDllPath = "dxc_dll_path"
kKeyDxilDllPath = "dxil_dll_path"
kKeyMsvcToolsDir = "msvc_tools_dir"
kKeyVcpkgRoot = "vcpkg_root"
kKeyNinjaPath = "ninja_path"
kKeySccachePath = "sccache_path"

# Search Roots & Downloads (search_paths.json)
kKeyNinjaToolsSubdir = "ninja_tools_subdir"
kKeyNinjaSearchRoots = "ninja_search_roots"
kKeyNinjaDownloadUrls = "ninja_download_urls"

kKeySccacheToolsSubdir = "sccache_tools_subdir"
kKeySccacheSearchRoots = "sccache_search_roots"
kKeySccacheDownloadUrls = "sccache_download_urls"

kKeyLlvmToolsSubdir = "llvm_tools_subdir"
kKeyLlvmSearchRoots = "llvm_search_roots"
kKeyLlvmDownloadUrls = "llvm_download_urls"
kKeyClangFormatVersion = "clang_format_version"
kKeyLlvmAutoBootstrap = "llvm_auto_bootstrap"

kKeyVcpkgToolsSubdir = "vcpkg_tools_subdir"
kKeyVcpkgSearchRoots = "vcpkg_search_roots"
kKeyVcpkgGitUrl = "vcpkg_git_url"
kKeyVcpkgGitCommit = "vcpkg_git_commit"
kKeyVcpkgAutoBootstrap = "vcpkg_auto_bootstrap"
kKeyVcpkgInstalledRel = "vcpkg_installed_rel"

# Resource Pack Config (PackConfig.json)
kKeyGlobalExcludeDirs = "global_exclude_directories"
kKeyGlobalExcludePatterns = "global_exclude_patterns"
kKeyRules = "rules"
kKeyExcludeDirs = "exclude_directories"
kKeyExcludePatterns = "exclude_patterns"
kKeyRecursive = "recursive"

# Environment Variables
kEnvSwLlvmAutoBootstrap = "SW_LLVM_AUTO_BOOTSTRAP"
kEnvSwVcpkgAutoBootstrap = "SW_VCPKG_AUTO_BOOTSTRAP"

# =============================================================================
# --- 6. Lint & Formatting Targets (린트 및 포맷팅 대상 디렉터리 SSOT) --------
# =============================================================================

kLintTargetRelDirs: tuple[str, ...] = (
    "Source",
    "Test",
    "Tools/ReflectionParser",
)

#: 저장소 안에 있어도 **우리 코드가 아닌** 폴더 이름입니다. 저장소를 훑는 린트는 이 이름의 폴더로 내려가지 않습니다
#: (`collectRepositoryFiles`). 빌드 산출물과, 부트스트랩이 `Tools/` 아래로 내려받는 외부 도구(vcpkg · LLVM · sccache ·
#: ninja · 캐시)입니다. 게이트마다 제외 목록을 따로 들지 말 것 — 목록이 서로 갈리고, 저장소 전체를 다 걸은 뒤에 거르는
#: 게이트는 `Tools/vcpkg` 의 외부 헤더 수천 개까지 훑느라 한 번에 십수 초를 씁니다.
#: `ThirdParty/` 는 여기 없습니다 — 저장소에 들어 있고 우리 CMake 연결 파일도 있어, 빼는 것은 게이트가 정합니다.
kNotOurDirNames: frozenset[str] = frozenset({
    "build",
    "generated",
    ".git",
    "__pycache__",
    ".venv",
    "vcpkg",
    "LLVM",
    "Sccache",
    "Ninja",
    "_cache",
    "_deps",
})

