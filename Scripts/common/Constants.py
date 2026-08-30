"""
Scripts/common/Constants.py

프로젝트 전체에서 공유하는 고정 경로, 파일명, JSON 스키마 키 및 스크립트 진입점 상수 (SSOT).
"""

from __future__ import annotations

# =============================================================================
# --- 1. Config Directories & Files (설정 디렉터리 및 JSON/XML 파일 경로) -------
# =============================================================================

kDirConfigEnv = "Config/Environment"
kFileToolchainConfig = "toolchain_config.json"
kFileParserConfig = "parser_config.json"
kFileParserDefaults = "parser_config.defaults.json"
kFileSearchPaths = "search_paths.json"
kFileSearchPathsDefaults = "search_paths.defaults.json"

kFileRuntimeEngineConfig = "Config/Engine/EngineConfig.json"
kFileRuntimeAppConfig = "Config/App/AppConfig.json"
kFileRuntimeEditorConfig = "Config/Editor/EditorConfig.json"
kFileRuntimeGameConfig = "Config/Game/GameConfig.json"
kFileRuntimeEditorData = "Config/Editor/editordata.xml"
kFileShippingHostDefaultsHeader = "ShippingHostDefaults.h"

# =============================================================================
# --- 2. Project Source & Reflection Architecture (소스 레이아웃 & 리플렉션 SSOT) -
# =============================================================================

kDirSourceEngine = "Source/Engine"
kDirSourceGames = "Source/Games"
kDirSourceGameFramework = "Source/GameFramework"
kDirSourceApp = "Source/App"
kDirSourceEditor = "Source/Editor"
kDirSourceCore = "Source/Core"

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
kDirToolsNinja = "Tools/Ninja"
kDirToolsSccache = "Tools/Sccache"
kDirToolsVcpkg = "Tools/vcpkg"
kDirToolsLlvm = "Tools/LLVM"
kDirToolsReflectionTemplates = "Tools/ReflectionParser/Templates"
kDirVcpkgInstalledRelDefault = "build/vcpkg_installed"

# =============================================================================
# --- 4. Script Entry Points (스크립트 실행 진입점 경로) ------------------------
# =============================================================================

kScriptSetupVcpkg = "Scripts/setup/SetupVcpkg.py"
kScriptSetupEnvironment = "Scripts/setup/SetupEnvironment.py"
kScriptLintCheckEngineLayers = "Scripts/lint/CheckEngineLayers.py"
kScriptLintCheckIncludeOrder = "Scripts/lint/CheckIncludeOrder.py"
kScriptLintCheckResourceCasing = "Scripts/lint/CheckResourceCasing.py"
kScriptLintCheckCodeConventions = "Scripts/lint/CheckCodeConventions.py"
kScriptLintCheckSourceGlob = "Scripts/lint/CheckSourceGlob.py"
kScriptLintRunClangFormat = "Scripts/lint/RunClangFormat.py"
kScriptGenerateBakeShippingHostDefaults = "Scripts/generate/BakeShippingHostDefaults.py"
kScriptGenerateCookPrefabs = "Scripts/generate/CookPrefabs.py"
kScriptGenerateCookScenes = "Scripts/generate/CookScenes.py"
kScriptCookResourcePacks = "Scripts/cook/CookResourcePacks.py"
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
kKeySystemIncludeDirs = "system_include_dirs"

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
kKeyLlvmAutoBootstrap = "llvm_auto_bootstrap"

kKeyVcpkgToolsSubdir = "vcpkg_tools_subdir"
kKeyVcpkgSearchRoots = "vcpkg_search_roots"
kKeyVcpkgGitUrl = "vcpkg_git_url"
kKeyVcpkgGitCommit = "vcpkg_git_commit"
kKeyVcpkgAutoBootstrap = "vcpkg_auto_bootstrap"
kKeyVcpkgInstalledRel = "vcpkg_installed_rel"

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

