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
import os
import platform
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Callable, Dict, Tuple

# Scripts/ → sys.path (ConfigHelper 문서의 표준 부트스트랩)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ConfigHelper import (
    autoBootstrapEnabled,
    kDirConfigEnv,
    kEnvSwVcpkgAutoBootstrap,
    kFileToolchainConfig,
    kFileParserConfig,
    kFileParserDefaults,
    kKeyClangFlags,
    kKeyEmit,
    kKeyLibclangDllPath,
    kKeyLlvmPath,
    kKeyMsvcToolsDir,
    kKeyNinjaPath,
    kKeyParserArgsDefault,
    kKeyParserArgsExtra,
    kKeyParserArgsPlatform,
    kKeyParserArgsSection,
    kKeyPaths,
    kKeySccachePath,
    kKeySystemIncludeDirs,
    kKeyTuning,
    kKeyVcpkgAutoBootstrap,
    kKeyVcpkgRoot,
    kKeyWindowsSdkDir,
    kKeyWindowsSdkVersion,
    getOrFindCached,
    getProjectRoot,
    isVcpkgRoot,
    loadToolchainConfig,
    loadSearchPaths,
    mergeJsonDictInternal,
    normalizePath,
    readJsonDictInternal,
)
from setup.HostTools import (
    findDxcDlls,
    findMsvcPath,
    findSystemIncludeDirs,
    findWindowsSdkPath,
    setupNinja,
    setupSccache,
)
from setup.SetupLlvm import findLibClangDllPath, isMinimalLlvmRoot


def asDictInternal(value: Any) -> dict:
    return value if isinstance(value, dict) else {}


def unionStrListInternal(base: Any, extra: Any) -> list[str]:
    outList: list[str] = []
    if isinstance(base, list):
        outList.extend(str(item) for item in base)
    if isinstance(extra, list):
        outList.extend(str(item) for item in extra)
    uniqueSet: set[str] = set()
    resultList: list[str] = []
    for item in outList:
        if item in uniqueSet:
            continue
        uniqueSet.add(item)
        resultList.append(item)
    return resultList


def rewriteLegacyClangArgsInternal(argList: list) -> list:
    """구버전/잘못된 clang 인자 철자를 교정합니다."""
    if not isinstance(argList, list):
        return []
    rewritten = []
    for arg in argList:
        if not isinstance(arg, str):
            continue
        if arg == "-fno-spellchecking":
            rewritten.append("-fno-spell-checking")
            continue
        rewritten.append(arg)
    return rewritten

def normalizeParserConfigInternal(raw: dict) -> dict:
    """Ensure nested parser_args/paths/emit/tuning schema."""
    out = dict(raw)
    rawArgs = out.get(kKeyParserArgsSection)
    if isinstance(rawArgs, list):
        argsSection = {
            kKeyParserArgsDefault: rawArgs,
            kKeyParserArgsPlatform: {},
            kKeyParserArgsExtra: [],
        }
    else:
        argsSection = asDictInternal(rawArgs)
    argsSection[kKeyParserArgsDefault] = rewriteLegacyClangArgsInternal(
        argsSection.get(kKeyParserArgsDefault, [])
    )
    argsSection[kKeyParserArgsExtra] = rewriteLegacyClangArgsInternal(
        argsSection.get(kKeyParserArgsExtra, [])
    )
    platformArgs = asDictInternal(argsSection.get(kKeyParserArgsPlatform))
    for osKey, osArgs in list(platformArgs.items()):
        platformArgs[osKey] = rewriteLegacyClangArgsInternal(osArgs)
    argsSection[kKeyParserArgsPlatform] = platformArgs
    out[kKeyParserArgsSection] = argsSection
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
    system_include_dirs: list[str]

# ==============================================================================
# 2) EnvironmentSetupManager 클래스
# ==============================================================================
class EnvironmentSetupManager:
    def __init__(self, force_refresh: bool = False):
        self.project_root = getProjectRoot()
        self.config_dir = self.project_root / kDirConfigEnv
        self.config_dir.mkdir(parents=True, exist_ok=True)
        self.toolchain_config_file = self.config_dir / kFileToolchainConfig
        self.parser_config_file = self.config_dir / kFileParserConfig
        
        # force_refresh가 참이면 기존 설정을 무시합니다.
        self.existing_config = {} if force_refresh else loadToolchainConfig()
        self.logger = logging.getLogger("SetupEnvironment")

    def safeCallInternal(self, label: str, fn: Callable[..., Any], *args: Any, **kwargs: Any) -> Any:
        """안전하게 콜러블을 실행하고 예외 발생 시 Fast-Fail 처리합니다."""
        try:
            return fn(*args, **kwargs)
        except Exception as exc:
            self.logger.fatal(f"[{label}] failed: {exc}")
            sys.exit(1)

    def resolveLlvmInternal(self) -> Tuple[str, str]:
        cached_llvm = self.existing_config.get(kKeyLlvmPath, "")
        if cached_llvm and isMinimalLlvmRoot(str(cached_llvm)):
            llvm_path = normalizePath(str(cached_llvm))
        else:
            from setup.SetupLlvm import setupLlvm
            path = self.safeCallInternal("SetupLlvm", setupLlvm, allowBootstrap=False)
            llvm_path = normalizePath(path or "")
            
        libclang_path = getOrFindCached(
            self.existing_config, kKeyLibclangDllPath, findLibClangDllPath, llvm_path
        )
        if llvm_path:
            from setup.SetupLlvm import ensureClangFormat
            self.safeCallInternal("EnsureClangFormat", ensureClangFormat, llvm_path, allowDownload=True)
        return llvm_path, libclang_path

    def resolveWindowsSdkInternal(self) -> Tuple[str, str, str, str, str]:
        sdk_dir = self.existing_config.get(kKeyWindowsSdkDir, "")
        sdk_ver = self.existing_config.get(kKeyWindowsSdkVersion, "")
        
        if platform.system() == "Windows" and (not sdk_dir or not os.path.exists(sdk_dir)):
            sdk_dir, sdk_ver = findWindowsSdkPath()
        elif platform.system() != "Windows":
            sdk_dir, sdk_ver = "", ""
            
        found_dxc, found_dxil = findDxcDlls(sdk_dir, sdk_ver, self.project_root)
        msvc_path = getOrFindCached(self.existing_config, kKeyMsvcToolsDir, findMsvcPath)
        return sdk_dir, sdk_ver, found_dxc, found_dxil, msvc_path

    def resolveToolsInternal(self) -> Tuple[str, str, str, list[str]]:
        cached_vcpkg = self.existing_config.get(kKeyVcpkgRoot, "")
        if cached_vcpkg and isVcpkgRoot(cached_vcpkg):
            vcpkg_path = normalizePath(str(cached_vcpkg))
        else:
            from setup.SetupVcpkg import setupVcpkg
            # search_paths / SW_VCPKG_AUTO_BOOTSTRAP 이 켜져 있으면 SetupEnvironment에서도 clone 허용
            allowBootstrap = autoBootstrapEnabled(
                False,
                kKeyVcpkgAutoBootstrap,
                kEnvSwVcpkgAutoBootstrap,
                search=loadSearchPaths(),
            )
            path = self.safeCallInternal("SetupVcpkg", setupVcpkg, allowBootstrap=allowBootstrap)
            vcpkg_path = normalizePath(str(path)) if path else ""

        # lambda closure to delay execution for getOrFindCached
        ninja_path = getOrFindCached(self.existing_config, kKeyNinjaPath, lambda: normalizePath(setupNinja() or ""))
        sccache_path = getOrFindCached(self.existing_config, kKeySccachePath, lambda: normalizePath(setupSccache() or ""))
        sys_includes = getOrFindCached(self.existing_config, kKeySystemIncludeDirs, findSystemIncludeDirs)
        
        return vcpkg_path, ninja_path, sccache_path, sys_includes

    def runLinuxSetupInternal(self):
        if platform.system() == "Linux":
            try:
                from setup.SetupLinuxDevEnvironment import setupLinuxDevEnvironment
                setupLinuxDevEnvironment()
            except Exception as exc:
                self.logger.warning(f"SetupLinuxDevEnvironment skipped: {exc}")

    def installPreCommitHookInternal(self):
        hook_path = self.project_root / ".git" / "hooks" / "pre-commit"
        if not hook_path.parent.exists():
            return
            
        # Write the bash script that calls PreCommitLint.py
        hook_content = (
            "#!/bin/sh\n"
            "# Auto-generated by SetupEnvironment.py\n\n"
            "python Scripts/lint/PreCommitLint.py\n"
            "if [ $? -ne 0 ]; then\n"
            "  exit 1\n"
            "fi\n"
        )
        
        try:
            hook_path.write_text(hook_content, encoding="utf-8")
            if platform.system() != "Windows":
                os.chmod(hook_path, 0o755)
            self.logger.info("[SetupEnvironment] Installed git pre-commit hook (Scripts/lint/PreCommitLint.py).")
        except Exception as exc:
            self.logger.warning(f"[SetupEnvironment] Failed to install pre-commit hook: {exc}")

    def run(self) -> EngineConfig:
        llvm_path, libclang_path = self.resolveLlvmInternal()
        sdk_dir, sdk_ver, dxc, dxil, msvc = self.resolveWindowsSdkInternal()
        vcpkg, ninja, sccache, sys_incs = self.resolveToolsInternal()

        config = EngineConfig(
            target_platform=platform.system().lower(),
            target_arch=platform.machine().lower(),
            llvm_path=llvm_path,
            libclang_dll_path=libclang_path,
            windows_sdk_dir=sdk_dir,
            windows_sdk_version=sdk_ver,
            dxc_dll_path=dxc,
            dxil_dll_path=dxil,
            msvc_tools_dir=msvc,
            vcpkg_root=vcpkg,
            ninja_path=ninja,
            sccache_path=sccache,
            system_include_dirs=sys_incs,
        )

        new_json = json.dumps(asdict(config), indent=4)
        should_write = True
        
        if self.toolchain_config_file.exists():
            try:
                if self.toolchain_config_file.read_text(encoding="utf-8") == new_json:
                    should_write = False
            except OSError:
                pass
                
        if should_write:
            self.toolchain_config_file.write_text(new_json, encoding="utf-8")

        self.updateParserConfigInternal(config.target_platform)
        self.runLinuxSetupInternal()
        self.installPreCommitHookInternal()

        self.logger.info(f"[SetupEnvironment] Resolved {kDirConfigEnv}/{kFileToolchainConfig} for OS '{platform.system()}':")
        for key, val in asdict(config).items():
            self.logger.info(f"  - {key:22}: {val}")
        self.logger.info(
            f"  - config_file          : {normalizePath(str(self.toolchain_config_file))} "
            f"({'updated' if should_write else 'unchanged'})"
        )
        return config

    def updateParserConfigInternal(self, targetOs: str) -> None:
        """parser_config.defaults.json 과 로컬 parser_config.json을 병합하여 갱신합니다."""
        defaultsFile = self.project_root / kDirConfigEnv / kFileParserDefaults
        seedRaw = readJsonDictInternal(defaultsFile, kFileParserDefaults)
        localRaw = readJsonDictInternal(self.parser_config_file, kFileParserConfig)

        seed = normalizeParserConfigInternal(seedRaw)
        local = normalizeParserConfigInternal(localRaw)

        managedKeys = (
            kKeyParserArgsSection,
            kKeyPaths,
            kKeyClangFlags,
            kKeyEmit,
            kKeyTuning,
        )
        localPassthrough = {key: value for key, value in local.items() if key not in managedKeys}
        parserData = mergeJsonDictInternal(seed, localPassthrough)

        seedArgs = asDictInternal(seed.get(kKeyParserArgsSection))
        localArgs = asDictInternal(local.get(kKeyParserArgsSection))
        seedPlatform = asDictInternal(seedArgs.get(kKeyParserArgsPlatform))
        localPlatform = asDictInternal(localArgs.get(kKeyParserArgsPlatform))

        mergedArgs = {
            kKeyParserArgsDefault: unionStrListInternal(
                seedArgs.get(kKeyParserArgsDefault), localArgs.get(kKeyParserArgsDefault)
            ),
            kKeyParserArgsPlatform: {
                osKey: unionStrListInternal(seedPlatform.get(osKey), localPlatform.get(osKey))
                for osKey in set(seedPlatform.keys()) | set(localPlatform.keys())
            },
            kKeyParserArgsExtra: unionStrListInternal(
                seedArgs.get(kKeyParserArgsExtra), localArgs.get(kKeyParserArgsExtra)
            ),
        }
        parserData[kKeyParserArgsSection] = mergedArgs
        parserData[kKeyPaths] = mergeJsonDictInternal(
            asDictInternal(seed.get(kKeyPaths)), asDictInternal(local.get(kKeyPaths))
        )
        parserData[kKeyClangFlags] = mergeJsonDictInternal(
            asDictInternal(seed.get(kKeyClangFlags)), asDictInternal(local.get(kKeyClangFlags))
        )
        parserData[kKeyEmit] = mergeJsonDictInternal(
            asDictInternal(seed.get(kKeyEmit)), asDictInternal(local.get(kKeyEmit))
        )
        parserData[kKeyTuning] = mergeJsonDictInternal(
            asDictInternal(seed.get(kKeyTuning)), asDictInternal(local.get(kKeyTuning))
        )

        self.parser_config_file.write_text(json.dumps(parserData, indent=4), encoding="utf-8")
        self.logger.info(
            f"[SetupEnvironment] Wrote {kDirConfigEnv}/{kFileParserConfig} "
            f"(platform '{targetOs}', default args={len(mergedArgs[kKeyParserArgsDefault])})."
        )

def setupEnvironment(force_refresh: bool = False) -> Dict[str, Any]:
    """레거시 모듈 임포트 호환성을 위한 래퍼 함수입니다."""
    # cmake에서 subprocess로 호출되거나 python 내에서 import로 불릴 때를 위해 남겨둠
    manager = EnvironmentSetupManager(force_refresh=force_refresh)
    # 기존 코드들이 딕셔너리로 받으므로 asdict() 변환해서 리턴
    return asdict(manager.run())

def main():
    parser = argparse.ArgumentParser(description="SW Engine Setup Environment Script")
    parser.add_argument("--force", action="store_true", help="기존 캐시를 무시하고 설정을 처음부터 다시 탐색합니다.")
    parser.add_argument("--verbose", action="store_true", help="상세 로그를 출력합니다.")
    args = parser.parse_args()

    # 기본 레벨은 INFO, --verbose 시 DEBUG (모던 CLI 스탠다드)
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,
        format="%(message)s"  # 기존의 심플한 로그 포맷 유지 (print 대체)
    )

    manager = EnvironmentSetupManager(force_refresh=args.force)
    manager.run()

if __name__ == "__main__":
    main()
