"""
Scripts/common/ToolLocator.py

도구(Ninja, Sccache, LLVM, vcpkg 등) 탐색 및 설정을 위한 선언적 프레임워크:
  1. OS 환경 변수 검사
  2. 시스템 PATH (shutil.which) 검사
  3. search_paths.json 탐색 루트 검사
  4. 프로젝트 로컬 Tools/<subdir> 키트 검사
  5. 필요 시 자동 부트스트랩/다운로드 연동
"""

from __future__ import annotations

import logging
import os
import shutil
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .Archive import resolveToolsSubdir
from .Config import loadSearchPaths
from .Paths import normalizePath
from .Search import findFirstValidRoot, platformSearchRoots


@dataclass(frozen=True)
class ToolSpec:
    """
    외부 빌드/호스트 도구의 탐색 및 설정 규격을 정의하는 데이터클래스입니다.

    Attributes:
        name: 도구 표시명 (예: 'Ninja', 'Sccache', 'LLVM', 'vcpkg').
        tools_subdir_key: search_paths 내 로컬 서브디렉터리 키 (예: 'ninja_tools_subdir').
        search_roots_key: search_paths 내 탐색 루트 배열 키 (예: 'ninja_search_roots').
        bin_names: 시스템 PATH 상에서 탐색할 실행 파일 이름 튜플 (예: ('ninja.exe', 'ninja')).
        env_vars: 우선 검사할 환경 변수 이름 튜플 (예: ('VCPKG_ROOT',)).
        download_urls_key: 자동 다운로드 URL 딕셔너리 키 (선택 사항).
        auto_bootstrap_key: search_paths 내 자동 부트스트랩 활성화 키 (선택 사항).
        env_auto_bootstrap_key: 환경 변수 자동 부트스트랩 오버라이드 키 (선택 사항).
        validate_func: 특정 디렉터리가 해당 도구의 유효한 루트인지 검증하는 콜백.
    """

    name: str
    tools_subdir_key: str
    search_roots_key: str
    bin_names: tuple[str, ...] = ()
    env_vars: tuple[str, ...] = ()
    download_urls_key: str | None = None
    auto_bootstrap_key: str | None = None
    env_auto_bootstrap_key: str | None = None
    validate_func: Callable[[Path], bool] | None = None


def findToolRoot(
    spec: ToolSpec,
    search: dict[str, Any] | None = None,
    *,
    logger: logging.Logger | None = None,
) -> Path | None:
    """
    선언된 ToolSpec에 따라 환경변수 -> 시스템 PATH -> search_paths -> 로컬 Tools 디렉터리 순서로
    도구의 설치 루트 디렉터리를 탐색합니다.

    Args:
        spec: 도구 명세 (ToolSpec).
        search: search_paths 딕셔너리 (None일 경우 loadSearchPaths() 호출).
        logger: 로깅에 사용할 Logger 객체 (선택 사항).

    Returns:
        유효한 도구 루트 Path 객체. 찾지 못한 경우 None.
    """
    searchConfig = search if search is not None else loadSearchPaths()
    log = logger or logging.getLogger(spec.name)
    toolsDir = resolveToolsSubdir(spec.tools_subdir_key, searchConfig)
    extras = {spec.tools_subdir_key: searchConfig[spec.tools_subdir_key]}

    def isValid(candidate: Path) -> bool:
        if spec.validate_func is not None:
            return spec.validate_func(candidate)
        return candidate.is_dir()

    # 1. 환경변수 확인
    for envVar in spec.env_vars:
        if (envVal := os.environ.get(envVar)) and isValid(Path(envVal)):
            log.info(f"[{spec.name}] Using {envVar}: {envVal}")
            return Path(normalizePath(envVal))

    # 2. 시스템 PATH 상의 바이너리 확인
    for binName in spec.bin_names:
        if foundBin := shutil.which(binName):
            binPath = Path(foundBin).resolve()
            for parentCandidate in (binPath.parent, binPath.parent.parent):
                if isValid(parentCandidate):
                    log.info(f"[{spec.name}] Using PATH: {parentCandidate}")
                    return Path(normalizePath(str(parentCandidate)))

    # 3. search_paths.json 탐색 루트 확인
    found = findFirstValidRoot(
        platformSearchRoots(searchConfig, spec.search_roots_key),
        isValid,
        extras=extras,
        skip=toolsDir,
    )
    if found:
        log.info(f"[{spec.name}] Using search root: {found}")
        return Path(normalizePath(str(found)))

    # 4. 로컬 Tools/<subdir> 키트 확인
    if isValid(toolsDir):
        log.info(f"[{spec.name}] Using project kit: {toolsDir}")
        return toolsDir

    return None
