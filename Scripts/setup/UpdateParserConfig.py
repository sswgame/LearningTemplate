"""Config/parser_config.json 시드/머지."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict

from ConfigHelper import GetProjectRoot


def LoadParserConfigSeed(project_root: Path) -> Dict[str, Any]:
    """커밋된 parser_config.defaults.json 또는 내장 기본값."""
    defaults_file = project_root / "Config" / "parser_config.defaults.json"
    if defaults_file.exists():
        try:
            with open(defaults_file, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict) and isinstance(loaded.get("default_parser_args"), list):
                return loaded
        except Exception:
            pass

    return {
        "default_parser_args": [
            "-std=c++17",
            "-D__REFLECT_PARSER__",
            "-DSW_API=",
            '-DREFLECT(...)=__attribute__((annotate("REFLECT;" #__VA_ARGS__)))',
            '-DPROPERTY(...)=__attribute__((annotate("PROPERTY;" #__VA_ARGS__)))',
            '-DFUNCTION(...)=__attribute__((annotate("FUNCTION;" #__VA_ARGS__)))',
            '-DENUM(...)=__attribute__((annotate("ENUM;" #__VA_ARGS__)))',
            "-x",
            "c++",
            "-w",
        ],
        "platform_parser_args": {
            "windows": [
                "-fms-compatibility",
                "-fms-compatibility-version=19",
                "-fms-extensions",
            ],
            "linux": [],
            "darwin": [],
        },
    }


def UpdateParserConfig(target_os: str, parser_config_file: Path) -> None:
    """parser_config.json 의 parser 인자를 갱신합니다."""
    project_root = GetProjectRoot()
    parser_data: Dict[str, Any] = LoadParserConfigSeed(project_root)

    if parser_config_file.exists():
        try:
            with open(parser_config_file, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict):
                if isinstance(loaded.get("default_parser_args"), list):
                    parser_data["default_parser_args"] = loaded["default_parser_args"]
                if isinstance(loaded.get("platform_parser_args"), dict):
                    parser_data["platform_parser_args"] = loaded["platform_parser_args"]
        except Exception:
            pass

    default_args = parser_data.get("default_parser_args", [])
    platform_map = parser_data.get("platform_parser_args", {})
    platform_args = platform_map.get(target_os, [])
    parser_data["parser_args"] = list(default_args) + list(platform_args)

    parser_config_file.parent.mkdir(parents=True, exist_ok=True)
    with open(parser_config_file, "w", encoding="utf-8") as f:
        json.dump(parser_data, f, indent=4)
