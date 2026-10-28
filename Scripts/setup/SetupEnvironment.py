#!/usr/bin/env python3
"""
Scripts/setup/SetupEnvironment.py

개발 PC 도구 경로 → Config/Environment/toolchain_config.json / parser_config.json.
캐시가 유효하면 재사용, 비었거나 깨졌을 때만 탐색/다운로드.

  python3 Scripts/setup/SetupEnvironment.py
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

from common.ClangFormat import ensureClangFormat
from common.Config import autoBootstrapEnabled, loadSearchPaths, loadToolchainConfig, readOptionalConfig, requireConfig
from common.Constants import (
    kDirConfigEnv,
    kEnvSwVcpkgAutoBootstrap,
    kFileParserConfig,
    kFileParserDefaults,
    kFileToolchainConfig,
    kKeyClangFlags,
    kKeyEmit,
    kKeyLibclangDllPath,
    kKeyLlvmPath,
    kKeyMsvcToolsDir,
    kKeyNinjaPath,
    kKeyParserArgsDefault,
    kKeyParserArgsExtra,
    kKeyParserArgsForceInclude,
    kKeyParserArgsPlatform,
    kKeyParserArgsSection,
    kKeyPaths,
    kKeyTuning,
    kKeyVcpkgAutoBootstrap,
    kKeyVcpkgRoot,
    kKeyWindowsSdkDir,
    kKeyWindowsSdkVersion,
)
from common.Paths import getProjectRoot, normalizePath
from common.Search import getOrFindCached, isVcpkgRoot
from common.ExitCode import ExitCode  # noqa: E402
from common.HostTools import (
    findDxcDlls,
    findMsvcPath,
    findWindowsSdkPath,
    setupNinja,
)
from setup.SetupLlvm import findLibClangDllPath, isMinimalLlvmRoot


def asDictInternal(value: Any) -> dict:
    return value if isinstance(value, dict) else {}


def unionStrListInternal(base: Any, extra: Any) -> list[str]:
    combined: list[str] = []
    if isinstance(base, list):
        combined.extend(str(item) for item in base)
    if isinstance(extra, list):
        combined.extend(str(item) for item in extra)
    return list(dict.fromkeys(combined))


def normalizeParserConfigInternal(raw: dict) -> dict:
    """parser_args, paths, emit, tuning 등 중첩 스키마 구조를 정규화합니다."""
    out = dict(raw)
    argsSection = asDictInternal(out.get(kKeyParserArgsSection))
    out[kKeyParserArgsSection] = {
        kKeyParserArgsDefault: list(argsSection.get(kKeyParserArgsDefault, [])),
        kKeyParserArgsPlatform: asDictInternal(argsSection.get(kKeyParserArgsPlatform)),
        kKeyParserArgsExtra: list(argsSection.get(kKeyParserArgsExtra, [])),
        kKeyParserArgsForceInclude: list(argsSection.get(kKeyParserArgsForceInclude, [])),
    }
    out[kKeyPaths] = asDictInternal(out.get(kKeyPaths))
    out[kKeyClangFlags] = asDictInternal(out.get(kKeyClangFlags))
    out[kKeyEmit] = asDictInternal(out.get(kKeyEmit))
    out[kKeyTuning] = asDictInternal(out.get(kKeyTuning))
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
    dxc_dll_path: str
    dxil_dll_path: str
    msvc_tools_dir: str
    vcpkg_root: str
    ninja_path: str


# ==============================================================================
# 2) 통합 환경 구성 매니저 (Class)
# ==============================================================================
class EnvironmentSetupManager:
    def __init__(self, force_refresh: bool = False) -> None:
        self.force_refresh = force_refresh
        self.project_root = getProjectRoot()
        self.config_dir = self.project_root / kDirConfigEnv
        self.toolchain_config_file = self.config_dir / kFileToolchainConfig
        self.parser_config_file = self.config_dir / kFileParserConfig

        self.logger = logging.getLogger("SetupEnvironment")
        self.existing_config: dict[str, Any] = {}
        if not self.force_refresh:
            self.existing_config = loadToolchainConfig() or {}  # 처음 실행은 캐시가 없다

    # 도구 탐색 함수는 "없음" 을 빈 값으로 돌려준다. 예외는 탐색 중 실패(받기 · 풀기 · 잘못된 설정)이므로 삼키지 않고 멈춘다 —
    # 경고 한 줄로 넘기면 빈 경로가 toolchain_config 에 적혀 다음 configure 가 엉뚱한 자리에서 깨진다.
    def resolveLlvmInternal(self) -> tuple[str, str]:
        cachedLlvm = self.existing_config.get(kKeyLlvmPath, "")
        if cachedLlvm and isMinimalLlvmRoot(cachedLlvm):
            llvmPath = normalizePath(str(cachedLlvm))
        else:
            from setup.SetupLlvm import setupLlvm

            path = setupLlvm(allowBootstrap=False)
            llvmPath = normalizePath(path or "")

        libclangPath = getOrFindCached(
            self.existing_config, kKeyLibclangDllPath, findLibClangDllPath, llvmPath
        )
        if llvmPath:
            ensureClangFormat(llvmPath, allowDownload=True)
        return llvmPath, libclangPath

    def resolveWindowsSdkInternal(self) -> tuple[str, str, str, str, str]:
        sdkDirectory = self.existing_config.get(kKeyWindowsSdkDir, "")
        sdkVersion = self.existing_config.get(kKeyWindowsSdkVersion, "")

        if platform.system() == "Windows" and (not sdkDirectory or not Path(sdkDirectory).exists()):
            sdkDirectory, sdkVersion = findWindowsSdkPath()
        elif platform.system() != "Windows":
            sdkDirectory, sdkVersion = "", ""

        foundDxc, foundDxil = findDxcDlls(sdkDirectory, sdkVersion, self.project_root)
        msvcPath = getOrFindCached(self.existing_config, kKeyMsvcToolsDir, findMsvcPath)
        return sdkDirectory, sdkVersion, foundDxc, foundDxil, msvcPath

    def resolveToolsInternal(self) -> tuple[str, str]:
        cachedVcpkg = self.existing_config.get(kKeyVcpkgRoot, "")
        if cachedVcpkg and isVcpkgRoot(cachedVcpkg):
            vcpkgPath = normalizePath(str(cachedVcpkg))
        else:
            from setup.SetupVcpkg import setupVcpkg

            allowBootstrap = autoBootstrapEnabled(
                False,
                kKeyVcpkgAutoBootstrap,
                kEnvSwVcpkgAutoBootstrap,
                search=loadSearchPaths(),
            )
            path = setupVcpkg(allowBootstrap=allowBootstrap)
            vcpkgPath = normalizePath(str(path)) if path else ""

        ninjaFound = getOrFindCached(self.existing_config, kKeyNinjaPath, lambda: normalizePath(setupNinja() or ""))
        ninjaPath = normalizePath(str(ninjaFound)) if ninjaFound else ""

        return vcpkgPath, ninjaPath

    def installPreCommitHookInternal(self) -> None:
        # 훅 설치 실패를 경고로 넘기면 커밋 검사 없이 커밋하게 된다 — 멈춘다.
        from setup.InstallGitHooks import installPreCommitHook

        if installPreCommitHook(self.project_root):
            self.logger.info("[SetupEnvironment] Installed git pre-commit hook (Scripts/lint/PreCommitLint.py).")

    def run(self) -> EngineConfig:
        llvmPath, libclangPath = self.resolveLlvmInternal()
        sdkDirectory, sdkVersion, dxcPath, dxilPath, msvcPath = self.resolveWindowsSdkInternal()
        vcpkgPath, ninjaPath = self.resolveToolsInternal()

        config = EngineConfig(
            target_platform=platform.system().lower(),
            target_arch=platform.machine().lower(),
            llvm_path=llvmPath,
            libclang_dll_path=libclangPath,
            windows_sdk_dir=sdkDirectory,
            windows_sdk_version=sdkVersion,
            dxc_dll_path=dxcPath,
            dxil_dll_path=dxilPath,
            msvc_tools_dir=msvcPath,
            vcpkg_root=vcpkgPath,
            ninja_path=ninjaPath,
        )

        newJson = json.dumps(asdict(config), indent=4)
        shouldWrite = True

        if self.toolchain_config_file.is_file():
            try:
                if self.toolchain_config_file.read_text(encoding="utf-8") == newJson:
                    shouldWrite = False
            except OSError:
                pass

        if shouldWrite:
            self.toolchain_config_file.write_text(newJson, encoding="utf-8")

        self.updateParserConfigInternal(config.target_platform)
        self.installPreCommitHookInternal()

        self.logger.info(
            f"[SetupEnvironment] Resolved {kDirConfigEnv}/{kFileToolchainConfig} for OS '{platform.system()}':"
        )
        for key, value in asdict(config).items():
            self.logger.info(f"  - {key:22}: {value}")
        self.logger.info(
            f"  - config_file          : {normalizePath(str(self.toolchain_config_file))} "
            f"({'updated' if shouldWrite else 'unchanged'})"
        )
        return config

    def updateParserConfigInternal(self, targetOs: str) -> None:
        """
        로컬 parser_config.json 에 **이 기계에 딸린 값만** 씁니다 — paths.* 와 parser_args.extra · force_include 중 기본값과 다른 것.

        주의: 기본값 전체를 로컬에 복사하면 로컬이 키마다 이기므로, 나중에 커밋된 기본값을 고쳐도 이 기계는 옛 값을 계속 쓴다
        (예: `flag_ops_marker` 가 바뀌면 옛 로컬 값 때문에 FlagOps 우산이 비어 Engine 빌드가 깨진다). 파서도 같은 규칙으로 읽는다
        (ParserConfig::load — 다른 키는 경고하고 버린다). 나머지는 모두 parser_config.defaults.json 에서 온다.
        """
        seedRaw = requireConfig(self.project_root / kDirConfigEnv / kFileParserDefaults, kFileParserDefaults)
        localRaw = readOptionalConfig(self.parser_config_file, kFileParserConfig)

        seed = normalizeParserConfigInternal(seedRaw)
        local = normalizeParserConfigInternal({} if localRaw is None else localRaw)   # 로컬 오버라이드는 처음엔 없다

        seedArgs = asDictInternal(seed.get(kKeyParserArgsSection))
        localArgs = asDictInternal(local.get(kKeyParserArgsSection))
        argsData: dict[str, list[str]] = {}
        for listKey in (kKeyParserArgsExtra, kKeyParserArgsForceInclude):
            seedItems = set(unionStrListInternal(seedArgs.get(listKey), []))
            localOnly = [item for item in unionStrListInternal(localArgs.get(listKey), []) if item not in seedItems]
            if localOnly:
                argsData[listKey] = localOnly

        seedPaths = asDictInternal(seed.get(kKeyPaths))
        localPaths = {key: value for key, value in asDictInternal(local.get(kKeyPaths)).items() if seedPaths.get(key) != value}

        parserData: dict[str, Any] = {}
        if argsData:
            parserData[kKeyParserArgsSection] = argsData
        if localPaths:
            parserData[kKeyPaths] = localPaths

        self.parser_config_file.write_text(json.dumps(parserData, indent=4), encoding="utf-8")
        self.logger.info(
            f"[SetupEnvironment] Wrote {kDirConfigEnv}/{kFileParserConfig} "
            f"(platform '{targetOs}', machine-local: {len(localPaths)} path(s), {len(argsData)} argument list(s); "
            f"everything else comes from {kFileParserDefaults})."
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SW Engine Setup Environment Script")
    parser.add_argument(
        "--force",
        action="store_true",
        help="기존 캐시를 무시하고 설정을 처음부터 다시 탐색합니다.",
    )
    parser.add_argument("--verbose", action="store_true", help="상세 로그를 출력합니다.")
    args = parser.parse_args(argv)

    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=log_level, format="%(message)s")

    manager = EnvironmentSetupManager(force_refresh=args.force)
    manager.run()
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
