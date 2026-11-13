#!/usr/bin/env python3
"""
Scripts/setup/environment.py

개발 PC 도구 경로 → Config/Environment/toolchain_config.json / parser_config.json.
캐시가 유효하면 재사용, 비었거나 깨졌을 때만 탐색/다운로드.

  python3 Scripts/setup/environment.py
"""

from __future__ import annotations

import argparse
import json
import logging
import platform
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

# Scripts/ → sys.path (common 패키지 부트스트랩)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.clang_format import ensure_clang_format
from common.toolchain_config import auto_bootstrap_enabled, load_search_paths, load_toolchain_config, read_optional_config, require_config
from common.project_constants import (
    DIR_CONFIG_ENV,
    ENV_SW_VCPKG_AUTO_BOOTSTRAP,
    FILE_PARSER_CONFIG,
    FILE_PARSER_DEFAULTS,
    FILE_TOOLCHAIN_CONFIG,
    KEY_CLANG_FLAGS,
    KEY_EMIT,
    KEY_LIBCLANG_DLL_PATH,
    KEY_LLVM_PATH,
    KEY_MSVC_TOOLS_DIR,
    KEY_NINJA_PATH,
    KEY_PARSER_ARGS_DEFAULT,
    KEY_PARSER_ARGS_EXTRA,
    KEY_PARSER_ARGS_FORCE_INCLUDE,
    KEY_PARSER_ARGS_PLATFORM,
    KEY_PARSER_ARGS_SECTION,
    KEY_PATHS,
    KEY_TUNING,
    KEY_VCPKG_AUTO_BOOTSTRAP,
    KEY_VCPKG_ROOT,
    KEY_WINDOWS_SDK_DIR,
    KEY_WINDOWS_SDK_VERSION,
)
from common.paths import get_project_root, normalize_path
from common.host_platform import is_windows_host, HOST
from common.file_collect import get_or_find_cached, is_vcpkg_root
from common.exit_code import ExitCode  # noqa: E402
from generate.generator_output import GeneratorError, write_generated_file
from generate.toolchain_cmake import generate_toolchain_cmake
from setup.toolchain_detect import (
    find_msvc_path,
    find_windows_sdk_path,
    setup_ninja,
)
from setup.llvm import find_lib_clang_dll_path, is_minimal_llvm_root


def _as_dict(value: Any) -> dict:
    return value if isinstance(value, dict) else {}


def _union_str_list(base: Any, extra: Any) -> list[str]:
    combined: list[str] = []
    if isinstance(base, list):
        combined.extend(str(item) for item in base)
    if isinstance(extra, list):
        combined.extend(str(item) for item in extra)
    return list(dict.fromkeys(combined))


def _normalize_parser_config(raw: dict) -> dict:
    """parser_args, paths, emit, tuning 등 중첩 스키마 구조를 정규화합니다."""
    out = dict(raw)
    args_section = _as_dict(out.get(KEY_PARSER_ARGS_SECTION))
    out[KEY_PARSER_ARGS_SECTION] = {
        KEY_PARSER_ARGS_DEFAULT: list(args_section.get(KEY_PARSER_ARGS_DEFAULT, [])),
        KEY_PARSER_ARGS_PLATFORM: _as_dict(args_section.get(KEY_PARSER_ARGS_PLATFORM)),
        KEY_PARSER_ARGS_EXTRA: list(args_section.get(KEY_PARSER_ARGS_EXTRA, [])),
        KEY_PARSER_ARGS_FORCE_INCLUDE: list(args_section.get(KEY_PARSER_ARGS_FORCE_INCLUDE, [])),
    }
    out[KEY_PATHS] = _as_dict(out.get(KEY_PATHS))
    out[KEY_CLANG_FLAGS] = _as_dict(out.get(KEY_CLANG_FLAGS))
    out[KEY_EMIT] = _as_dict(out.get(KEY_EMIT))
    out[KEY_TUNING] = _as_dict(out.get(KEY_TUNING))
    return out


# ==============================================================================
# 1) 설정용 Dataclass
# ==============================================================================
@dataclass
class EngineConfig:
    target_platform: str
    target_arch: str
    llvm_path: str
    libclang_dll_path: str
    windows_sdk_dir: str
    windows_sdk_version: str
    msvc_tools_dir: str
    vcpkg_root: str
    ninja_path: str


# ==============================================================================
# 2) 통합 환경 구성 매니저 (Class)
# ==============================================================================
class EnvironmentSetupManager:
    def __init__(self, force_refresh: bool = False) -> None:
        self.force_refresh = force_refresh
        self.project_root = get_project_root()
        self.config_dir = self.project_root / DIR_CONFIG_ENV
        self.toolchain_config_file = self.config_dir / FILE_TOOLCHAIN_CONFIG
        self.parser_config_file = self.config_dir / FILE_PARSER_CONFIG

        self.logger = logging.getLogger("Environment")
        self.existing_config: dict[str, Any] = {}
        if not self.force_refresh:
            self.existing_config = load_toolchain_config() or {}  # 처음 실행은 캐시가 없다

    # 도구 탐색 함수는 "없음" 을 빈 값으로 돌려준다. 예외는 탐색 중 실패(받기 · 풀기 · 잘못된 설정)이므로 삼키지 않고 멈춘다 —
    # 경고 한 줄로 넘기면 빈 경로가 toolchain_config 에 적혀 다음 configure 가 엉뚱한 자리에서 깨진다.
    def _resolve_llvm(self) -> tuple[str, str]:
        cached_llvm = self.existing_config.get(KEY_LLVM_PATH, "")
        if cached_llvm and is_minimal_llvm_root(cached_llvm):
            llvm_path = normalize_path(str(cached_llvm))
        else:
            from setup.llvm import setup_llvm

            path = setup_llvm(allow_bootstrap=False)
            llvm_path = normalize_path(path or "")

        libclang_path = get_or_find_cached(
            self.existing_config, KEY_LIBCLANG_DLL_PATH, find_lib_clang_dll_path, llvm_path
        )
        if llvm_path:
            ensure_clang_format(llvm_path, allow_download=True)
        return llvm_path, libclang_path

    def _resolve_windows_sdk(self) -> tuple[str, str, str]:
        sdk_directory = self.existing_config.get(KEY_WINDOWS_SDK_DIR, "")
        sdk_version = self.existing_config.get(KEY_WINDOWS_SDK_VERSION, "")

        if is_windows_host() and (not sdk_directory or not Path(sdk_directory).exists()):
            sdk_directory, sdk_version = find_windows_sdk_path()
        elif not is_windows_host():
            sdk_directory, sdk_version = "", ""

        msvc_path = get_or_find_cached(self.existing_config, KEY_MSVC_TOOLS_DIR, find_msvc_path)
        return sdk_directory, sdk_version, msvc_path

    def _resolve_tools(self) -> tuple[str, str]:
        cached_vcpkg = self.existing_config.get(KEY_VCPKG_ROOT, "")
        if cached_vcpkg and is_vcpkg_root(cached_vcpkg):
            vcpkg_path = normalize_path(str(cached_vcpkg))
        else:
            from setup.vcpkg import setup_vcpkg

            allow_bootstrap = auto_bootstrap_enabled(
                False,
                KEY_VCPKG_AUTO_BOOTSTRAP,
                ENV_SW_VCPKG_AUTO_BOOTSTRAP,
                search=load_search_paths(),
            )
            path = setup_vcpkg(allow_bootstrap=allow_bootstrap)
            vcpkg_path = normalize_path(str(path)) if path else ""

        ninja_found = get_or_find_cached(self.existing_config, KEY_NINJA_PATH, lambda: normalize_path(setup_ninja() or ""))
        ninja_path = normalize_path(str(ninja_found)) if ninja_found else ""

        return vcpkg_path, ninja_path

    def _install_pre_commit_hook(self) -> None:
        # 훅 설치 실패를 경고로 넘기면 커밋 검사 없이 커밋하게 된다 — 멈춘다.
        from setup.git_hooks import install_pre_commit_hook

        if install_pre_commit_hook(self.project_root):
            self.logger.info("[Environment] Installed git pre-commit hook (Scripts/lint/pre_commit.py).")

    def run(self) -> EngineConfig:
        llvm_path, libclang_path = self._resolve_llvm()
        sdk_directory, sdk_version, msvc_path = self._resolve_windows_sdk()
        vcpkg_path, ninja_path = self._resolve_tools()

        config = EngineConfig(
            target_platform=HOST.key,
            target_arch=platform.machine().lower(),
            llvm_path=llvm_path,
            libclang_dll_path=libclang_path,
            windows_sdk_dir=sdk_directory,
            windows_sdk_version=sdk_version,
            msvc_tools_dir=msvc_path,
            vcpkg_root=vcpkg_path,
            ninja_path=ninja_path,
        )

        should_write = write_generated_file(self.toolchain_config_file, json.dumps(asdict(config), indent=4), tag="Environment", quiet=True)

        self._update_parser_config(config.target_platform)
        self._install_pre_commit_hook()

        self.logger.info(
            f"[Environment] Resolved {DIR_CONFIG_ENV}/{FILE_TOOLCHAIN_CONFIG} for OS '{HOST.name}':"
        )
        for key, value in asdict(config).items():
            self.logger.info(f"  - {key:22}: {value}")
        self.logger.info(
            f"  - config_file          : {normalize_path(str(self.toolchain_config_file))} "
            f"({'updated' if should_write else 'unchanged'})"
        )
        return config

    def _update_parser_config(self, target_os: str) -> None:
        """
        로컬 parser_config.json 에 **이 기계에 딸린 값만** 씁니다 — paths.* 와 parser_args.extra · force_include 중 기본값과 다른 것.

        주의: 기본값 전체를 로컬에 복사하면 로컬이 키마다 이기므로, 나중에 커밋된 기본값을 고쳐도 이 기계는 옛 값을 계속 쓴다
        (예: `flag_ops_marker` 가 바뀌면 옛 로컬 값 때문에 FlagOps 우산이 비어 Engine 빌드가 깨진다). 파서도 같은 규칙으로 읽는다
        (ParserConfig::load — 다른 키는 경고하고 버린다). 나머지는 모두 parser_config.defaults.json 에서 온다.
        """
        seed_raw = require_config(self.project_root / DIR_CONFIG_ENV / FILE_PARSER_DEFAULTS, FILE_PARSER_DEFAULTS)
        local_raw = read_optional_config(self.parser_config_file, FILE_PARSER_CONFIG)

        seed = _normalize_parser_config(seed_raw)
        local = _normalize_parser_config({} if local_raw is None else local_raw)   # 로컬 오버라이드는 처음엔 없다

        seed_args = _as_dict(seed.get(KEY_PARSER_ARGS_SECTION))
        local_args = _as_dict(local.get(KEY_PARSER_ARGS_SECTION))
        args_data: dict[str, list[str]] = {}
        for list_key in (KEY_PARSER_ARGS_EXTRA, KEY_PARSER_ARGS_FORCE_INCLUDE):
            seed_items = set(_union_str_list(seed_args.get(list_key), []))
            local_only = [item for item in _union_str_list(local_args.get(list_key), []) if item not in seed_items]
            if local_only:
                args_data[list_key] = local_only

        seed_paths = _as_dict(seed.get(KEY_PATHS))
        local_paths = {key: value for key, value in _as_dict(local.get(KEY_PATHS)).items() if seed_paths.get(key) != value}

        parser_data: dict[str, Any] = {}
        if args_data:
            parser_data[KEY_PARSER_ARGS_SECTION] = args_data
        if local_paths:
            parser_data[KEY_PATHS] = local_paths

        # 바뀌었을 때만 쓴다 — 모든 리플렉션 단계가 이 파일에 기대므로(ReflectionCodeGen.cmake), 같은 내용을 configure 마다 다시 쓰면
        # 프리셋 하나를 구성할 때마다 모든 빌드 트리의 파서가 전부 다시 돌았다(파일은 소스 트리 하나를 빌드 트리 여럿이 같이 본다).
        written = write_generated_file(self.parser_config_file, json.dumps(parser_data, indent=4), tag="Environment", quiet=True)
        self.logger.info(
            f"[Environment] {'Wrote' if written else 'Unchanged'} {DIR_CONFIG_ENV}/{FILE_PARSER_CONFIG} "
            f"(platform '{target_os}', machine-local: {len(local_paths)} path(s), {len(args_data)} argument list(s); "
            f"everything else comes from {FILE_PARSER_DEFAULTS})."
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SW Engine Setup Environment Script")
    parser.add_argument(
        "--force",
        action="store_true",
        help="기존 캐시를 무시하고 설정을 처음부터 다시 탐색합니다.",
    )
    parser.add_argument("--verbose", action="store_true", help="상세 로그를 출력합니다.")
    # configure 는 이 뒤에 늘 ToolchainVars.cmake 를 만든다 — 같은 프로세스에서 해 파이썬을 한 번 덜 띄운다(DetectToolchain.cmake).
    parser.add_argument("--cmake-vars", type=Path, help="toolchain_config.json 을 CMake set() 목록으로도 쓴다(toolchain_cmake.py 와 같은 출력)")
    args = parser.parse_args(argv)

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format="%(message)s")

    manager = EnvironmentSetupManager(force_refresh=args.force)
    manager.run()
    if args.cmake_vars:
        try:
            generate_toolchain_cmake(args.cmake_vars)
        except GeneratorError as error:
            print(f"[ToolchainCMake] {error}", file=sys.stderr)
            return ExitCode.VIOLATION
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
