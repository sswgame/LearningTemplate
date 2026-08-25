"""
Scripts/common/Config.py

JSON 설정 파일 읽기/쓰기, 툴체인 및 검색 경로 설정 관리.
"""

from __future__ import annotations

import json
import os
import shutil
import sys
from pathlib import Path
from typing import Any

from .Constants import (
    kDirConfigEnv,
    kFileSearchPaths,
    kFileSearchPathsDefaults,
    kFileToolchainConfig,
)
from .Paths import getProjectRoot, normalizePath


def readJsonDictInternal(path: Path, label: str) -> dict[str, Any]:
    """
    JSON 객체를 읽어 딕셔너리로 반환합니다. 파일이 없거나 파싱 실패 시 빈 딕셔너리를 반환합니다.
    """
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            return data
    except Exception as exception:
        sys.stderr.write(f"[Config] Failed to read {label}: {exception}\n")
    return {}


def mergeJsonDictInternal(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    """
    기본 설정값(base) 위에 로컬 사용자 설정값(override)을 덮어써서 병합합니다.

    - 중첩 딕셔너리(dict)는 재귀적으로 병합합니다.
    - 리스트나 일반 값은 로컬에 값이 있으면 로컬 값으로 완전히 교체합니다.
    - 로컬에 없는 새 키는 기본값(base)을 그대로 유지합니다.
    """
    merged = dict(base)
    for key, value in override.items():
        existing = merged.get(key)
        if isinstance(existing, dict) and isinstance(value, dict):
            merged[key] = mergeJsonDictInternal(existing, value)
        else:
            merged[key] = value
    return merged


def loadToolchainConfig() -> dict[str, Any]:
    """
    개발 환경 툴체인 경로 캐시 파일(Config/Environment/toolchain_config.json)을 읽어 반환합니다.
    """
    configFile = getProjectRoot() / kDirConfigEnv / kFileToolchainConfig
    return readJsonDictInternal(configFile, kFileToolchainConfig)


def updateToolchainConfig(key: str, value: Any) -> None:
    """
    toolchain_config.json에 특정 키-값을 기록합니다.
    (기존 값과 동일하면 디스크 쓰기를 생략하여 불필요한 CMake 재구성을 방지합니다.)
    """
    configDir = getProjectRoot() / kDirConfigEnv
    configFile = configDir / kFileToolchainConfig
    configDir.mkdir(parents=True, exist_ok=True)
    configData = loadToolchainConfig()
    normalizedValue = normalizePath(str(value)) if isinstance(value, (str, Path)) else value
    if configData.get(key) == normalizedValue:
        return
    configData[key] = normalizedValue
    configFile.write_text(json.dumps(configData, indent=4), encoding="utf-8")


def loadSearchPaths() -> dict[str, Any]:
    """
    도구 탐색 경로 설정(search_paths.json)을 읽어 반환합니다.

    - search_paths.json 파일이 없으면 defaults 기본 설정 파일을 복사하여 생성합니다.
    - 로컬 파일이 이미 존재하더라도 기본값과 병합하여 새로 추가된 키를 자동으로 보완합니다.
    """
    configDir = getProjectRoot() / kDirConfigEnv
    jsonPath = configDir / kFileSearchPaths
    defaultsPath = configDir / kFileSearchPathsDefaults
    if not jsonPath.is_file() and defaultsPath.is_file():
        try:
            configDir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(defaultsPath, jsonPath)
        except Exception as exception:
            sys.stderr.write(f"[Config] Failed to copy {kFileSearchPathsDefaults}: {exception}\n")

    defaults = readJsonDictInternal(defaultsPath, kFileSearchPathsDefaults)
    local = readJsonDictInternal(jsonPath, kFileSearchPaths)
    if not defaults and not local:
        return {}
    return mergeJsonDictInternal(defaults, local)


def recordEnginePath(key: str, path: str | Path) -> str:
    """
    주어진 엔진 관련 경로를 정규화하여 설정 파일에 기록한 후 반환합니다.
    """
    resolved = normalizePath(str(path))
    updateToolchainConfig(key, resolved)
    return resolved


def autoBootstrapEnabled(force: bool,
                         configKey: str,
                         envKey: str,
                         *,
                         default: bool = False,
                         search: dict[str, Any] | None = None) -> bool:
    """
    환경 변수나 설정 파일, 강제 옵션 등을 확인하여 자동 부트스트랩(다운로드 등)이 활성화되어 있는지 여부를 반환합니다.
    """
    if force:
        return True
    searchConfig = search if search is not None else loadSearchPaths()
    if bool(searchConfig.get(configKey, default)):
        return True
    flag = os.environ.get(envKey, "").strip().lower()
    return flag in ("1", "true", "on", "yes")
