"""
Scripts/common/ProjectConstants.py

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
# 에디터가 쓰는 상태(도킹 · 레이아웃 · 캔버스 · 테마 · gv 프리셋) — 사람이 쓰는 설정(Config/)과 폴더를 나눈다. git 무시(/Saved/).
kDirSavedEditor = "Saved/Editor"
# 게임 프리셋 폴더 — 게임마다 `<게임>.json` 하나(팩 루트 — 시작 씬은 팩의 gamesettings). Dev 는 실행할 때 고른 게임(`-game=`), Shipping 은 구운 게임의 것을 쓴다.
kDirRuntimeGamePreset = "Config/Game"
# 전용 서버 운영 설정 폴더 — `<게임>.json`(ServerConfig). Shipping 도 굽지 않고 디스크에서 읽는다.
kDirRuntimeServerPreset = "Config/Server"
# 빌드의 활성 게임 이름을 담는 파일(Dev, `Bin/Modules/<이 이름>`) — `-game=` 이 없을 때 `GameSelection` 이 읽는다. 엔진 TU 에 게임 이름을 굽지 않으려고 파일로 둔다.
kFileActiveGame = "ActiveGame.txt"
# 모듈 빌드 성공 스탬프(Dev, `Bin/Modules/<이 이름>`) — 빌드가 동적 모듈을 모두 링크한 뒤에만 다시 쓴다(`sw_addModuleBuildStamp`).
# 바깥 빌드(터미널 · IDE)의 모듈 변경은 `LiveReloadManager` 가 이 파일이 바뀔 때만 올린다(반쯤 링크된 집합을 올리지 않는다).
kFileModuleBuildStamp = "ModuleBuild.stamp"
# 함께 지은 게임 팩 폴더(Dev, `Bin/<이 이름>/<게임>/SWGame` · `Bin/Symbols/<이 이름>/<게임>/`) — `SW_GAMES`.
kDirBinGames = "Games"
# 사람이 쓰는 에디터 도구 값 — 기본값과 다른 값이 있을 때만 만든다(없으면 기본값).
kFileRuntimeEditorToolDefaults = "Config/Editor/editortooldefaults.json"

# =============================================================================
# --- 2. Project Source & Reflection Architecture (소스 레이아웃 & 리플렉션 SSOT) -
# =============================================================================

kDirSourceEditor = "Source/Editor"

kFileReflectBuiltins = "Source/Engine/Reflection/ReflectBuiltins.xxx"
kFileAnnotationMeta = "Source/Core/Predefined/AnnotationMeta.txt"
kFileTplBuiltinHeader = "BuiltinFileHeader.tpl"
kFileTplBuiltinRegistrar = "BuiltinTypeRegistrar.tpl"
kFileTplBuiltinFooter = "BuiltinFileFooter.tpl"

kTargetGameModule = "SWGame"
kTargetEditorModule = "EditorModule"

# C++ 파일 확장자 집합 (소스 / 전체)
kCppSourceExtensions: set[str] = {".c", ".cpp", ".cc", ".cxx"}
kCppAllExtensions: set[str] = {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}
#: 셰이더 소스 확장자 — 셰이더를 고르는 스크립트는 모두 이것을 쓴다(파일마다 튜플을 따로 두면 갈라진다).
#: clang-format 은 C++ 와 함께 이것도 건다(셰이더만의 설정은 `Content/.clang-format`).
kShaderSourceExtensions: set[str] = {".hlsl", ".hlsli"}

# =============================================================================
# --- 3. Tool Directories & Defaults (내부 도구 디렉터리 및 캐시 경로) ----------
# =============================================================================

#: 이 저장소의 파이썬 스크립트가 돌아야 하는 가장 낮은 버전입니다. 정본은 여기 한 곳입니다.
#: CI 러너(ubuntu-22.04)의 `python3` 가 3.10 이라 이 값이 됩니다. `common` 을 import 하는 순간 검사해 낮으면 멈추고,
#: 이 값 미만의 구문 · 표준 라이브러리 API(예: 3.13 의 `read_text(newline=)` · 3.11 의 `hashlib.file_digest`)는 쓰지 않는다.
kMinimumPythonVersion: tuple[int, int] = (3, 10)

kDirToolsCache = "Tools/_cache"
kDirToolsReflectionTemplates = "Source/Programs/ReflectionParser/Templates"

# =============================================================================
# --- 4. Script Entry Points (스크립트 실행 진입점 경로) ------------------------
# =============================================================================

kScriptSetupVcpkg = "Scripts/setup/Vcpkg.py"
kScriptSetupEnvironment = "Scripts/setup/Environment.py"

# =============================================================================
# --- 5. JSON Schema Keys & Environment Variables (JSON 필드명 및 환경변수 심볼) -
# =============================================================================

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
kKeyMsvcToolsDir = "msvc_tools_dir"
kKeyVcpkgRoot = "vcpkg_root"
kKeyNinjaPath = "ninja_path"

# Search Roots & Downloads (search_paths.json)
kKeyNinjaToolsSubdir = "ninja_tools_subdir"
kKeyNinjaSearchRoots = "ninja_search_roots"
kKeyNinjaDownloadUrls = "ninja_download_urls"


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

# Environment Variables
kEnvSwLlvmAutoBootstrap = "SW_LLVM_AUTO_BOOTSTRAP"
kEnvSwVcpkgAutoBootstrap = "SW_VCPKG_AUTO_BOOTSTRAP"

# =============================================================================
# --- 6. Lint & Formatting Targets (린트 및 포맷팅 대상 디렉터리 SSOT) --------
# =============================================================================

#: 우리가 쓴 C++ 의 뿌리 — 이름 · include 순서 · 픽서 · clang-format · 코드 게이트가 모두 이것을 본다. 게이트마다 따로 적지 않는다
#:. 프로그램(리플렉션 파서 · 부하 봇)도 `Source/Programs` 아래라 따로 적지 않는다.
kLintTargetRelDirs: tuple[str, ...] = ("Source",)

#: `kLintTargetRelDirs` 의 맨 앞 폴더 이름(`Source`) — include 경로의 기준 폴더를 찾는 게이트가 쓴다.
kLintTargetBaseDirNames: tuple[str, ...] = tuple(dict.fromkeys(root.split("/")[0] for root in kLintTargetRelDirs))

#: 저장소 안에 있어도 **우리 코드가 아닌** 폴더 이름입니다. 저장소를 훑는 린트는 이 이름의 폴더로 내려가지 않습니다
#: (`collectRepositoryFiles`). 빌드 산출물과, 부트스트랩이 `Tools/` 아래로 내려받는 외부 도구(vcpkg · LLVM ·
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
    "Ninja",
    "_cache",
    "_deps",
    "graphify-out",  # graphify 가 코드 폴더(`Source/`) 안에 남기는 그래프 산출물(.gitignore 대상) — 폴더마다 빌드 소속을 묻는 게이트가 이 폴더를 소속 없는 코드로 본다
    "worktrees",  # 에이전트 격리 워크트리(`.claude/worktrees/<이름>/`) — 저장소 전체의 사본이라 훑으면 같은 파일이 여러 번 잡힌다
})

#: 저장소 기준 경로로 못 박은 생성물 폴더 — .gitignore 대상이라 저장소에 들어 있지 않고, 테스트와 실행이 셰이더 · 텍스처 캐시 수만 개를 쌓는다.
#: 이름만으로 빼면 같은 이름의 추적 폴더(`Source/Engine/Asset/Cache/`)까지 빠지므로 경로로 뺀다. 지금은 없다(셰이더 캐시는 `Bin/Saved` 로 옮겼다). 파일 걷기(`collectRepositoryFiles` · `resolveFileArguments`)가 건너뛴다.
kGeneratedRelDirs: frozenset[str] = frozenset()

