r"""
Scripts/ConfigHelper.py

엔진 프리빌드 파이프라인(SetupEnvironment, FindVcpkg, SetupNinja) 공통 유틸리티.

네이밍: 공개 함수 PascalCase (GetProjectRoot, NormalizePath, LoadEngineConfig, ...).
경로 문자열은 POSIX 슬래시(C:/...) 로 정규화한다.
"""

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict

def GetProjectRoot() -> Path:
    """
    현재 스크립트 위치(Scripts/)를 기준으로 최상위 프로젝트 루트 절대 경로 객체를 반환합니다.
    """
    script_dir = Path(__file__).resolve().parent
    return script_dir.parent

def NormalizePath(path_str: str) -> str:
    """
    윈도우 백슬래시('\')를 POSIX 표준 슬래시('/')로 정규화합니다.
    (CMake와 Python 및 리플렉션 파서 간 이스케이프 오류를 차단합니다)
    """
    if not path_str:
        return ""
    return str(Path(path_str).as_posix())

def LoadEngineConfig() -> Dict[str, Any]:
    """Config/engine_config.json 설정을 로드합니다. (환경설정이 없으면 빈 딕셔너리 반환)"""
    project_root = GetProjectRoot()
    config_file = project_root / "Config" / "engine_config.json"

    if config_file.exists():
        try:
            with open(config_file, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {}

def UpdateEngineConfig(key: str, value: Any) -> None:
    """Config/engine_config.json 파일의 특정 키-값 정보를 업데이트합니다."""
    project_root = GetProjectRoot()
    config_dir = project_root / "Config"
    config_file = config_dir / "engine_config.json"

    config_dir.mkdir(parents=True, exist_ok=True)

    config_data = LoadEngineConfig()
    config_data[key] = NormalizePath(str(value)) if isinstance(value, (str, Path)) else value

    with open(config_file, "w", encoding="utf-8") as f:
        json.dump(config_data, f, indent=4)
