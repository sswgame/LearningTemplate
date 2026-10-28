"""
Scripts/common/ToolchainConfig.py

JSON 설정 파일 읽기/쓰기, 툴체인 및 검색 경로 설정 관리.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any

from .ProjectConstants import (
    kDirConfigEnv,
    kFileSearchPaths,
    kFileSearchPathsDefaults,
    kFileToolchainConfig,
)
from .Paths import getProjectRoot, normalizePath


class ConfigError(RuntimeError):
    """설정 파일이 없거나(필수일 때) 읽을 수 없거나 JSON 객체가 아니다."""


def parseJsonObjectInternal(path: Path, label: str) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError(f"[Config] {label} 를 읽지 못했습니다: {path}: {error}") from error
    if not isinstance(data, dict):
        raise ConfigError(f"[Config] {label} 는 JSON 객체여야 합니다: {path}")
    return data


def requireConfig(path: Path, label: str) -> dict[str, Any]:
    """
    필수 설정을 읽습니다. 없거나 깨졌거나 객체가 아니면 ConfigError 로 멈춥니다.

    설정 읽기는 이 함수와 `readOptionalConfig` 둘뿐이다 — "없으면 빈 사전" 을 돌려주는 읽기가 있으면 필수 설정이 빠져도
    호출부의 `.get(키, 기본값)` 이 조용히 기본값으로 돌아 아무도 모른다. 필수인지 선택인지를 함수 이름이 말하게 한다.
    """
    if not path.is_file():
        raise ConfigError(f"[Config] 필수 설정 {label} 이 없습니다: {path}")
    return parseJsonObjectInternal(path, label)


def readOptionalConfig(path: Path, label: str) -> dict[str, Any] | None:
    """
    선택 설정을 읽습니다. 파일이 없으면 None(호출부가 "없음" 을 직접 다룬다), 있는데 깨졌으면 ConfigError 로 멈춥니다.
    """
    if not path.is_file():
        return None
    return parseJsonObjectInternal(path, label)


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


def loadToolchainConfig() -> dict[str, Any] | None:
    """
    개발 환경 툴체인 경로 캐시 파일(Config/Environment/toolchain_config.json)을 읽어 반환합니다.

    이 파일은 Environment 가 채우는 **캐시**라 처음 실행에는 없다 — 없으면 None 이다. 빈 사전으로 바꾸지 않는다:
    호출부의 `.get(키, 기본값)` 이 "캐시 없음" 을 조용히 삼키지 못하게 호출부가 None 을 직접 다룬다. 깨졌으면 멈춘다.
    """
    configFile = getProjectRoot() / kDirConfigEnv / kFileToolchainConfig
    return readOptionalConfig(configFile, kFileToolchainConfig)


def updateToolchainConfig(key: str, value: Any) -> None:
    """
    toolchain_config.json에 특정 키-값을 기록합니다.
    (기존 값과 동일하면 디스크 쓰기를 생략하여 불필요한 CMake 재구성을 방지합니다.)
    """
    configDir = getProjectRoot() / kDirConfigEnv
    configFile = configDir / kFileToolchainConfig
    configDir.mkdir(parents=True, exist_ok=True)
    cached = loadToolchainConfig()
    configData = {} if cached is None else cached  # 처음 기록이면 새 캐시를 만든다
    normalizedValue = normalizePath(str(value)) if isinstance(value, (str, Path)) else value
    if configData.get(key) == normalizedValue:
        return
    configData[key] = normalizedValue
    configFile.write_text(json.dumps(configData, indent=4), encoding="utf-8")


def expandSelfReferencesInternal(config: dict[str, Any]) -> dict[str, Any]:
    """
    설정 안의 `${key}` 를 **같은 설정의 최상위 스칼라 값**으로 치환합니다.

    도구 버전이 URL 안에 두 번씩 박혀 있으면(`llvmorg-20.1.8/clang+llvm-20.1.8-…`) 올릴 때마다
    여러 줄을 손대야 하고, 플랫폼 하나를 빠뜨리면 그때부터 PC 마다 다른 버전을 쓰게 된다(Windows 20 /
    Linux 18 처럼). 버전을 키 하나로 두고 URL 은 그것을
    참조하게 하면 올릴 자리가 한 곳뿐이다.

    `${sourceDir}`·`${ProgramFiles}` 처럼 여기서 값을 알 수 없는 것은 **건드리지 않는다** —
    경로 확장은 쓰는 자리에서 `expandPathTemplate` 이 맥락을 갖고 한다.
    """
    scalars = {key: str(value) for key, value in config.items() if isinstance(value, (str, int, float))}
    if not scalars:
        return config

    def substituteInternal(value: Any) -> Any:
        if isinstance(value, str):
            for key, replacement in scalars.items():
                value = value.replace(f"${{{key}}}", replacement)
            return value
        if isinstance(value, dict):
            return {k: substituteInternal(v) for k, v in value.items()}
        if isinstance(value, list):
            return [substituteInternal(v) for v in value]
        return value

    return {key: substituteInternal(value) for key, value in config.items()}


def loadSearchPaths() -> dict[str, Any]:
    """
    도구 탐색 경로 설정(search_paths.json)을 읽어 반환합니다.

    - search_paths.json 이 없으면 **빈 오버라이드 파일**로 만듭니다.
    - 로컬 파일의 값이 기본값을 덮어씁니다(리스트는 통째로 교체, 없는 키는 기본값 유지).
    - search_paths.defaults.json 이 존재하지 않거나 유효하지 않으면 ConfigError 로 멈춥니다. 로컬 파일이 깨져도 멈춥니다.

    주의: defaults 를 **통째로 복사**해 두면 로컬 파일이 모든 키를 갖게 되고, 로컬이 우선이므로 defaults 를
    아무리 고쳐도 그 PC 에는 **영원히 닿지 않는다**. 그래서 로컬 파일에는 사용자가 실제로 바꾼 것만 남기고,
    나머지는 defaults 를 따른다. (통째로 복사된 파일이 있다면 지우면 기본값으로 돌아온다 — 도구 캐시일 뿐 잃을 것이 없다.)
    """
    configDir = getProjectRoot() / kDirConfigEnv
    jsonPath = configDir / kFileSearchPaths
    defaultsPath = configDir / kFileSearchPathsDefaults

    defaults = requireConfig(defaultsPath, kFileSearchPathsDefaults)

    if not jsonPath.is_file():
        # 오버라이드 파일은 사용자가 손볼 자리를 보여 주려고 만들 뿐이다 — 못 만들어도 defaults 만으로 맞게 돈다.
        try:
            configDir.mkdir(parents=True, exist_ok=True)
            jsonPath.write_text("{\n}\n", encoding="utf-8")
        except OSError as exception:
            sys.stderr.write(f"[Config] {kFileSearchPaths} 를 만들지 못했습니다(defaults 만 씁니다): {exception}\n")

    local = readOptionalConfig(jsonPath, kFileSearchPaths)
    if local is None:
        return expandSelfReferencesInternal(defaults)  # 오버라이드는 선택이다 — 없으면 defaults 만 쓴다
    return expandSelfReferencesInternal(mergeJsonDictInternal(defaults, local))


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
                         search: dict[str, Any] | None = None) -> bool:
    """
    환경 변수나 설정 파일, 강제 옵션 등을 확인하여 자동 부트스트랩(다운로드 등)이 활성화되어 있는지 여부를 반환합니다.
    """
    if force:
        return True
    searchConfig = search if search is not None else loadSearchPaths()
    if bool(searchConfig[configKey]):   # search_paths.defaults 의 필수 키
        return True
    flag = os.environ.get(envKey, "").strip().lower()
    return flag in ("1", "true", "on", "yes")
