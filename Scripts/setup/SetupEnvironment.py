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
from typing import Any, Callable

# Scripts/ → sys.path (common 패키지 부트스트랩)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    autoBootstrapEnabled,
    ensureClangFormat,
    getOrFindCached,
    getProjectRoot,
    isVcpkgRoot,
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
    kKeySccachePath,
    kKeyTuning,
    kKeyVcpkgAutoBootstrap,
    kKeyVcpkgRoot,
    kKeyWindowsSdkDir,
    kKeyWindowsSdkVersion,
    loadSearchPaths,
    loadToolchainConfig,
    normalizePath,
    readJsonDictInternal,
)
from setup.HostTools import (
    findDxcDlls,
    findMsvcPath,
    findWindowsSdkPath,
    setupNinja,
    setupSccache,
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
    sccache_path: str


# ==============================================================================
# 2) 통합 환경 구성 매니저 (Class)
# ==============================================================================
class EnvironmentSetupManager:
    def __init__(self, force_refresh: bool = False):
        self.force_refresh = force_refresh
        self.project_root = getProjectRoot()
        self.config_dir = self.project_root / kDirConfigEnv
        self.toolchain_config_file = self.config_dir / kFileToolchainConfig
        self.parser_config_file = self.config_dir / kFileParserConfig

        self.logger = logging.getLogger("SetupEnvironment")
        self.existing_config: dict[str, Any] = {}
        if not self.force_refresh:
            self.existing_config = loadToolchainConfig()

    def safeCallInternal(self, name: str, callback: Callable[..., Any], *args: Any, **kwargs: Any) -> Any:
        try:
            return callback(*args, **kwargs)
        except Exception as exception:
            self.logger.warning(f"[{name}] Warning/Skipped: {exception}")
            return None

    def resolveLlvmInternal(self) -> tuple[str, str]:
        cachedLlvm = self.existing_config.get(kKeyLlvmPath, "")
        if cachedLlvm and isMinimalLlvmRoot(cachedLlvm):
            llvmPath = normalizePath(str(cachedLlvm))
        else:
            from setup.SetupLlvm import setupLlvm

            path = self.safeCallInternal("SetupLlvm", setupLlvm, allowBootstrap=False)
            llvmPath = normalizePath(path or "")

        libclangPath = getOrFindCached(
            self.existing_config, kKeyLibclangDllPath, findLibClangDllPath, llvmPath
        )
        if llvmPath:
            self.safeCallInternal("EnsureClangFormat", ensureClangFormat, llvmPath, allowDownload=True)
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

    def resolveToolsInternal(self) -> tuple[str, str, str]:
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
            path = self.safeCallInternal("SetupVcpkg", setupVcpkg, allowBootstrap=allowBootstrap)
            vcpkgPath = normalizePath(str(path)) if path else ""

        ninjaFound = self.safeCallInternal(
            "SetupNinja",
            getOrFindCached,
            self.existing_config,
            kKeyNinjaPath,
            lambda: normalizePath(setupNinja() or ""),
        )
        ninjaPath = normalizePath(str(ninjaFound)) if ninjaFound else ""
        sccacheFound = self.safeCallInternal(
            "SetupSccache",
            getOrFindCached,
            self.existing_config,
            kKeySccachePath,
            lambda: normalizePath(setupSccache() or ""),
        )
        sccachePath = normalizePath(str(sccacheFound)) if sccacheFound else ""

        return vcpkgPath, ninjaPath, sccachePath

    def runLinuxSetupInternal(self) -> None:
        if platform.system() == "Linux":
            try:
                from setup.SetupLinuxDevEnvironment import setupLinuxDevEnvironment

                setupLinuxDevEnvironment()
            except Exception as exception:
                self.logger.warning(f"SetupLinuxDevEnvironment skipped: {exception}")

    def installPreCommitHookInternal(self) -> None:
        try:
            from setup.InstallGitHooks import installPreCommitHook

            if installPreCommitHook(self.project_root):
                self.logger.info("[SetupEnvironment] Installed git pre-commit hook (Scripts/lint/PreCommitLint.py).")
        except Exception as exception:
            self.logger.warning(f"[SetupEnvironment] Failed to install pre-commit hook: {exception}")

    def run(self) -> EngineConfig:
        llvmPath, libclangPath = self.resolveLlvmInternal()
        sdkDirectory, sdkVersion, dxcPath, dxilPath, msvcPath = self.resolveWindowsSdkInternal()
        vcpkgPath, ninjaPath, sccachePath = self.resolveToolsInternal()

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
            sccache_path=sccachePath,
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
        self.runLinuxSetupInternal()
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

        예전에는 기본값 전체에 로컬을 덮은 사본을 썼고, 파서는 로컬을 키마다 이기게 읽었다. 그래서 나중에 커밋된 기본값을 고쳐도
        이 기계는 옛 값을 계속 썼다(flag_ops_marker 가 바뀐 뒤 Engine 빌드가 깨진 일). 파서도 같은 규칙으로 읽는다
        (ParserConfig::load — 다른 키는 경고하고 버린다). 나머지는 모두 parser_config.defaults.json 에서 온다.
        """
        defaultsFile = self.project_root / kDirConfigEnv / kFileParserDefaults
        if not defaultsFile.is_file():
            self.logger.error(f"[SetupEnvironment] Required defaults config not found: {defaultsFile}")
            raise FileNotFoundError(f"Required defaults config not found: {defaultsFile}")

        seedRaw = readJsonDictInternal(defaultsFile, kFileParserDefaults)
        if not seedRaw:
            self.logger.error(f"[SetupEnvironment] Failed to load or parse defaults from {defaultsFile}")
            raise RuntimeError(f"Failed to load or parse defaults from {defaultsFile}")

        localRaw = readJsonDictInternal(self.parser_config_file, kFileParserConfig)

        seed = normalizeParserConfigInternal(seedRaw)
        local = normalizeParserConfigInternal(localRaw)

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


def setupEnvironment(force_refresh: bool = False) -> dict[str, Any]:
    """
    개발 환경 설정을 탐색 및 구성하고 toolchain_config.json / parser_config.json에 저장합니다.
    """
    manager = EnvironmentSetupManager(force_refresh=force_refresh)
    return asdict(manager.run())


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
    return 0


if __name__ == "__main__":
    sys.exit(main())
