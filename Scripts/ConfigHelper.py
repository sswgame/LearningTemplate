r"""
Scripts/ConfigHelper.py

엔진 프리빌드 파이프라인 공통 유틸리티.

네이밍: 공개 함수 PascalCase (GetProjectRoot, NormalizePath, LoadEngineConfig, ...).
경로 문자열은 POSIX 슬래시(C:/...) 로 정규화한다.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


def EnsureScriptsOnPath() -> Path:
    """Scripts/ 를 sys.path 앞에 넣고 그 경로를 반환합니다."""
    scripts_dir = Path(__file__).resolve().parent
    scripts_str = str(scripts_dir)
    if scripts_str not in sys.path:
        sys.path.insert(0, scripts_str)
    return scripts_dir


def GetProjectRoot() -> Path:
    """CMakeLists.txt 를 위로 탐색해 프로젝트 루트를 반환합니다."""
    start = Path(__file__).resolve().parent
    for candidate in [start, *start.parents]:
        if (candidate / "CMakeLists.txt").is_file():
            return candidate
    # Fallback: Scripts/ 의 부모
    return start.parent


def NormalizePath(path_str: str) -> str:
    """윈도우 백슬래시를 POSIX 슬래시로 정규화합니다."""
    if not path_str:
        return ""
    return str(Path(path_str).as_posix())


def ExpandPathTemplate(template: str) -> str:
    """${ENV} 또는 $ENV 형태를 환경 변수로 치환합니다."""
    if not template:
        return ""
    expanded = os.path.expandvars(template)
    # expandvars leaves unknown ${Var} intact on Windows sometimes — strip empty
    return expanded


def LoadEngineConfig() -> Dict[str, Any]:
    """Config/engine_config.json 설정을 로드합니다."""
    config_file = GetProjectRoot() / "Config" / "engine_config.json"
    if config_file.exists():
        try:
            with open(config_file, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                return data
        except Exception:
            pass
    return {}


def UpdateEngineConfig(key: str, value: Any) -> None:
    """Config/engine_config.json 의 특정 키를 업데이트합니다."""
    project_root = GetProjectRoot()
    config_dir = project_root / "Config"
    config_file = config_dir / "engine_config.json"
    config_dir.mkdir(parents=True, exist_ok=True)

    config_data = LoadEngineConfig()
    config_data[key] = NormalizePath(str(value)) if isinstance(value, (str, Path)) else value

    with open(config_file, "w", encoding="utf-8") as f:
        json.dump(config_data, f, indent=4)


def LoadSearchPaths() -> Dict[str, Any]:
    """
    Config/search_paths.json 을 로드합니다.
    없거나 깨졌으면 search_paths.defaults.json, 둘 다 없으면 빈 dict.
    """
    root = GetProjectRoot()
    for name in ("search_paths.json", "search_paths.defaults.json"):
        path = root / "Config" / name
        if not path.exists():
            continue
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                return data
        except Exception:
            continue
    return {}


def GetOrFindCached(existing: Dict[str, Any], key: str, find_func, *args) -> Any:
    """engine_config 캐시가 유효하면 재사용, 아니면 find_func 호출."""
    val = existing.get(key)
    if val and (not isinstance(val, str) or os.path.exists(val)):
        return val
    if isinstance(val, list) and val:
        return val
    return find_func(*args)


def FindFirstExistingFile(search_dirs: List[Path], file_names: List[str]) -> str:
    for directory in search_dirs:
        if not directory.exists():
            continue
        for file_name in file_names:
            candidate = directory / file_name
            if candidate.is_file():
                return str(candidate)
    return ""


def FindFirstExistingFileRecursive(search_dirs: List[Path], file_names: List[str]) -> str:
    for directory in search_dirs:
        if not directory.exists() or not directory.is_dir():
            continue
        for file_name in file_names:
            try:
                for candidate in directory.rglob(file_name):
                    if candidate.is_file():
                        return str(candidate)
            except (OSError, PermissionError):
                pass
    return ""
