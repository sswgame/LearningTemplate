"""
Scripts/common/project_constants.py

프로젝트 전체에서 공유하는 고정 경로, 파일명, JSON 스키마 키 및 스크립트 진입점 상수 (SSOT).
"""

from __future__ import annotations

# =============================================================================
# --- 1. Config Directories & Files (설정 디렉터리 및 JSON/XML 파일 경로) -------
# =============================================================================

DIR_CONFIG = "Config"
DIR_CONFIG_EDITOR = "Config/Editor"
DIR_CONFIG_ENV = "Config/Environment"
FILE_TOOLCHAIN_CONFIG = "toolchain_config.json"
FILE_PARSER_CONFIG = "parser_config.json"
FILE_PARSER_DEFAULTS = "parser_config.defaults.json"
FILE_SEARCH_PATHS = "search_paths.json"
FILE_SEARCH_PATHS_DEFAULTS = "search_paths.defaults.json"

FILE_RUNTIME_ENGINE_CONFIG = "Config/Engine/EngineConfig.json"
# 에디터가 쓰는 상태(도킹 · 레이아웃 · 캔버스 · 테마 · gv 프리셋) — 사람이 쓰는 설정(Config/)과 폴더를 나눈다. git 무시(/Saved/).
DIR_SAVED_EDITOR = "Saved/Editor"
# 게임 프리셋 폴더 — 게임마다 `<게임>.json` 하나(팩 루트 — 시작 씬은 팩의 gamesettings). Dev 는 실행할 때 고른 게임(`-game=`), Shipping 은 구운 게임의 것을 쓴다.
DIR_RUNTIME_GAME_PRESET = "Config/Game"
# 전용 서버 운영 설정 폴더 — `<게임>.json`(ServerConfig). Shipping 도 굽지 않고 디스크에서 읽는다.
DIR_RUNTIME_SERVER_PRESET = "Config/Server"
# 빌드의 활성 게임 이름을 담는 파일(Dev, `Bin/Modules/<이 이름>`) — `-game=` 이 없을 때 `GameSelection` 이 읽는다. 엔진 TU 에 게임 이름을 굽지 않으려고 파일로 둔다.
FILE_ACTIVE_GAME = "ActiveGame.txt"
# 모듈 빌드 성공 스탬프(Dev, `Bin/Modules/<이 이름>`) — 빌드가 동적 모듈을 모두 링크한 뒤에만 다시 쓴다(`sw_addModuleBuildStamp`).
# 바깥 빌드(터미널 · IDE)의 모듈 변경은 `LiveReloadManager` 가 이 파일이 바뀔 때만 올린다(반쯤 링크된 집합을 올리지 않는다).
FILE_MODULE_BUILD_STAMP = "ModuleBuild.stamp"
# 함께 지은 게임 팩 폴더(Dev, `Bin/<이 이름>/<게임>/SWGame` · `Bin/Symbols/<이 이름>/<게임>/`) — `SW_GAMES`.
DIR_BIN_GAMES = "Games"
# 사람이 쓰는 에디터 도구 값 — 기본값과 다른 값이 있을 때만 만든다(없으면 기본값).
FILE_RUNTIME_EDITOR_TOOL_DEFAULTS = "Config/Editor/editortooldefaults.json"

# =============================================================================
# --- 2. Project Source & Reflection Architecture (소스 레이아웃 & 리플렉션 SSOT) -
# =============================================================================

DIR_SOURCE_EDITOR = "Source/Editor"

FILE_REFLECT_BUILTINS = "Source/Engine/Reflection/ReflectBuiltins.xxx"
FILE_ANNOTATION_META = "Source/Core/Predefined/AnnotationMeta.txt"
FILE_TPL_BUILTIN_HEADER = "BuiltinFileHeader.tpl"
FILE_TPL_BUILTIN_REGISTRAR = "BuiltinTypeRegistrar.tpl"
FILE_TPL_BUILTIN_FOOTER = "BuiltinFileFooter.tpl"
# 리플렉션 대상마다 파서가 쓰는 타입 목록(`<FQN>\t<모듈>` 줄) — 콘텐츠 모듈 검사가 읽는다
FILE_REFLECTED_TYPES_LIST = "ReflectedTypes.list"
# 리플렉션 대상마다 파서가 쓰는 경로 칸 목록(`<이름>\t<대상>` 줄) — Scripts/generate/asset_reference_field_list.py 가 모은다
FILE_ASSET_REFERENCE_FIELDS_LIST = "AssetReferenceFields.list"
# 리플렉션 생성 계약 — 파서(`sw::config`) · CMake(`SW_*`) · 템플릿(`$Banner`)이 이 표 하나를 쓴다(예전에는 JSON `emit` · 파서 기본값 · CMake 글자 · 템플릿 네 곳).
# 자리 표시자는 "스탬프 없음" 하나로 알아본다 — CMake 가 자리 표시자를 심을 때 스탬프(`<.gen.cpp><kExtReflectionStamp>`)를 지운다.
EXT_REFLECTION_GEN_CPP = ".gen.cpp"
# 헤더마다의 개발 전용 단위 — 에디터 메타데이터(표시 · 편집 필드)를 채워 올린다. 배포 구성은 짓지 않는다(`sw_addReflectionStep`, 언리얼 WITH_METADATA 자리)
EXT_REFLECTION_GEN_META_CPP = ".gen.meta.cpp"
EXT_REFLECTION_GEN_HEADER = ".gen.h"
EXT_REFLECTION_TEMPLATE = ".tpl"
EXT_REFLECTION_STAMP = ".stamp"
FILE_REFLECTION_FLAG_OPS = "FlagOps.gen.h"
TEXT_REFLECTION_BANNER = "// AUTO-GENERATED -- DO NOT EDIT"

TARGET_GAME_MODULE = "SWGame"
TARGET_EDITOR_MODULE = "EditorModule"

# C++ 파일 확장자 집합 (소스 / 전체)
CPP_SOURCE_EXTENSIONS: set[str] = {".c", ".cpp", ".cc", ".cxx"}
CPP_ALL_EXTENSIONS: set[str] = {".h", ".hpp", ".inl", ".c", ".cpp", ".cc", ".cxx"}
#: 셰이더 소스 확장자 — 셰이더를 고르는 스크립트는 모두 이것을 쓴다(파일마다 튜플을 따로 두면 갈라진다).
#: clang-format 은 C++ 와 함께 이것도 건다(셰이더만의 설정은 `Content/.clang-format`).
SHADER_SOURCE_EXTENSIONS: set[str] = {".hlsl", ".hlsli"}

# =============================================================================
# --- 3. Tool Directories & Defaults (내부 도구 디렉터리 및 캐시 경로) ----------
# =============================================================================

#: 이 저장소의 파이썬 스크립트가 돌아야 하는 가장 낮은 버전입니다. 정본은 여기 한 곳입니다.
#: CI 러너(ubuntu-22.04)의 `python3` 가 3.10 이라 이 값이 됩니다. `common` 을 import 하는 순간 검사해 낮으면 멈추고,
#: 이 값 미만의 구문 · 표준 라이브러리 API(예: 3.13 의 `read_text(newline=)` · 3.11 의 `hashlib.file_digest`)는 쓰지 않는다.
MINIMUM_PYTHON_VERSION: tuple[int, int] = (3, 10)

DIR_TOOLS_CACHE = "Tools/_cache"
DIR_TOOLS_REFLECTION_TEMPLATES = "Source/Programs/ReflectionParser/Templates"

# =============================================================================
# --- 4. Script Entry Points (스크립트 실행 진입점 경로) ------------------------
# =============================================================================

SCRIPT_SETUP_VCPKG = "Scripts/setup/vcpkg.py"
SCRIPT_SETUP_ENVIRONMENT = "Scripts/setup/environment.py"

# =============================================================================
# --- 5. JSON Schema Keys & Environment Variables (JSON 필드명 및 환경변수 심볼) -
# =============================================================================

# Reflection Parser (parser_config.json)
KEY_PARSER_ARGS_SECTION = "parser_args"
KEY_PARSER_ARGS_DEFAULT = "default"
KEY_PARSER_ARGS_PLATFORM = "platform"
KEY_PARSER_ARGS_EXTRA = "extra"
KEY_PARSER_ARGS_FORCE_INCLUDE = "force_include"
KEY_PATHS = "paths"
KEY_CLANG_FLAGS = "clang_flags"
KEY_EMIT = "emit"
KEY_TUNING = "tuning"

# Toolchain Paths (toolchain_config.json)
KEY_LLVM_PATH = "llvm_path"
KEY_LIBCLANG_DLL_PATH = "libclang_dll_path"
KEY_WINDOWS_SDK_DIR = "windows_sdk_dir"
KEY_WINDOWS_SDK_VERSION = "windows_sdk_version"
KEY_MSVC_TOOLS_DIR = "msvc_tools_dir"
KEY_VCPKG_ROOT = "vcpkg_root"
KEY_NINJA_PATH = "ninja_path"

# Search Roots & Downloads (search_paths.json)
KEY_NINJA_TOOLS_SUBDIR = "ninja_tools_subdir"
KEY_NINJA_SEARCH_ROOTS = "ninja_search_roots"
KEY_NINJA_DOWNLOAD_URLS = "ninja_download_urls"


KEY_LLVM_TOOLS_SUBDIR = "llvm_tools_subdir"
KEY_LLVM_SEARCH_ROOTS = "llvm_search_roots"
KEY_LLVM_DOWNLOAD_URLS = "llvm_download_urls"
KEY_CLANG_FORMAT_VERSION = "clang_format_version"
KEY_LLVM_AUTO_BOOTSTRAP = "llvm_auto_bootstrap"
# Windows lldb 의 liblldb.dll 이 import 하는 python3XX.dll 과 같은 판(임베드 패키지)이어야 한다 — `llvm.py` 의 `_ensure_debugger`.
KEY_LLDB_PYTHON_VERSION = "lldb_python_version"
KEY_LLDB_PYTHON_EMBED_URL = "lldb_python_embed_url"

KEY_VCPKG_TOOLS_SUBDIR = "vcpkg_tools_subdir"
KEY_VCPKG_SEARCH_ROOTS = "vcpkg_search_roots"
KEY_VCPKG_GIT_URL = "vcpkg_git_url"
KEY_VCPKG_GIT_COMMIT = "vcpkg_git_commit"
KEY_VCPKG_AUTO_BOOTSTRAP = "vcpkg_auto_bootstrap"

# Environment Variables
ENV_SW_LLVM_AUTO_BOOTSTRAP = "SW_LLVM_AUTO_BOOTSTRAP"
ENV_SW_VCPKG_AUTO_BOOTSTRAP = "SW_VCPKG_AUTO_BOOTSTRAP"

# =============================================================================
# --- 6. Lint & Formatting Targets (린트 및 포맷팅 대상 디렉터리 SSOT) --------
# =============================================================================

#: 우리가 쓴 C++ 의 뿌리 — 이름 · include 순서 · 픽서 · clang-format · 코드 게이트가 모두 이것을 본다. 게이트마다 따로 적지 않는다
#:. 프로그램(리플렉션 파서 · 부하 봇)도 `Source/Programs` 아래라 따로 적지 않는다.
LINT_TARGET_REL_DIRS: tuple[str, ...] = ("Source",)

#: `LINT_TARGET_REL_DIRS` 의 맨 앞 폴더 이름(`Source`) — include 경로의 기준 폴더를 찾는 게이트가 쓴다.
LINT_TARGET_BASE_DIR_NAMES: tuple[str, ...] = tuple(dict.fromkeys(root.split("/")[0] for root in LINT_TARGET_REL_DIRS))

#: 저장소 안에 있어도 **우리 코드가 아닌** 폴더 이름입니다. 저장소를 훑는 린트는 이 이름의 폴더로 내려가지 않습니다
#: (`collect_repository_files`). 빌드 산출물과, 부트스트랩이 `Tools/` 아래로 내려받는 외부 도구(vcpkg · LLVM ·
#: ninja · 캐시)입니다. 게이트마다 제외 목록을 따로 들지 말 것 — 목록이 서로 갈리고, 저장소 전체를 다 걸은 뒤에 거르는
#: 게이트는 `Tools/vcpkg` 의 외부 헤더 수천 개까지 훑느라 한 번에 십수 초를 씁니다.
#: `ThirdParty/` 는 여기 없습니다 — 저장소에 들어 있고 우리 CMake 연결 파일도 있어, 빼는 것은 게이트가 정합니다.
NOT_OUR_DIR_NAMES: frozenset[str] = frozenset({
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
    "worktrees",  # 에이전트 격리 워크트리(`.claude/worktrees/<이름>/`) — 저장소 전체의 사본이라 훑으면 같은 파일이 여러 번 잡힌다
})

#: 저장소 기준 경로로 못 박은 생성물 폴더 — .gitignore 대상이라 저장소에 들어 있지 않고, 테스트와 실행이 셰이더 · 텍스처 캐시 수만 개를 쌓는다.
#: 이름만으로 빼면 같은 이름의 추적 폴더(`Source/Engine/Asset/Cache/`)까지 빠지므로 경로로 뺀다. 지금은 없다(셰이더 캐시는 `Bin/Saved` 로 옮겼다). 파일 걷기(`collect_repository_files` · `resolve_file_arguments`)가 건너뛴다.
GENERATED_REL_DIRS: frozenset[str] = frozenset()

