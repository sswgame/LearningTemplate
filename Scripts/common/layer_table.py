"""
Scripts/common/layer_table.py

층 표(`Config/Rules/Layers/<모듈>Layers.toml`) 읽기와 "이 파일은 몇 층인가" 판정 — 가드 생성기(`generate/module_layer_guards.py` ·
`generate/editor_layer_guards.py`)와 린트(`layer-includes` · `layer-cycles`)가 함께 쓴다. 규칙이 한 곳이라 린트와 컴파일 가드가 다르게 판정하지 않는다
(언리얼 UBT 가 `ModuleRules` 한 모델로 검사와 생성을 함께 내는 자리).

- Engine · GameFramework(`Runtime`) · Core: 키는 모듈 폴더 아래 경로 접두어(폴더 또는 파일)이고 가장 긴 키가 이긴다.
- Editor: 층 이름은 `Common/<하위>`(Common 바로 아래 파일은 `Common`), 그 밖의 최상위 폴더, `Source/Editor` 바로 아래 파일은 `<root>` 이다.
"""

from __future__ import annotations

import tomllib
from pathlib import Path

from .command import CommandError
from .paths import get_project_root

EDITOR_ROOT_LAYER = "<root>"
EDITOR_COMMON_FOLDER = "Common"


def get_layer_table_path(module: str, project_root: Path | None = None) -> Path:
    return (project_root or get_project_root()) / "Config" / "Rules" / "Layers" / f"{module}Layers.toml"


def read_layer_table(module: str, project_root: Path | None = None) -> dict:
    """`<module>Layers.toml` 전체. 파일이 없거나 TOML 이 깨졌거나 `[layer]` 가 없으면 멈춘다(빈 표로 대신하지 않는다)."""
    path = get_layer_table_path(module, project_root)
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as exception:
        raise CommandError(f"{path.name}: 읽지 못했다 ({exception!r})") from exception
    if not isinstance(data.get("layer"), dict):
        raise CommandError(f"{path.name}: [layer] 표가 있어야 한다")
    return data


def find_layer_key(map_layer: dict[str, int], relative: str) -> str | None:
    """모듈 폴더 기준 경로 @p relative 를 덮는 가장 긴 키(폴더 또는 파일). 없으면 None."""
    best: str | None = None
    for key in map_layer:
        if (relative == key or relative.startswith(key + "/")) and (best is None or len(key) > len(best)):
            best = key
    return best


def find_layer(map_layer: dict[str, int], relative: str) -> int | None:
    key = find_layer_key(map_layer, relative)
    return map_layer[key] if key is not None else None


def get_editor_layer_name(relative: str) -> str:
    """`Source/Editor` 기준 경로의 층 이름."""
    list_part = relative.split("/")
    if len(list_part) == 1:
        return EDITOR_ROOT_LAYER
    if list_part[0] != EDITOR_COMMON_FOLDER:
        return list_part[0]
    return EDITOR_COMMON_FOLDER if len(list_part) == 2 else f"{EDITOR_COMMON_FOLDER}/{list_part[1]}"

