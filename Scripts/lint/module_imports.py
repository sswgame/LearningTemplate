#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/module_imports.py

Scripts 안 모듈이 거쳐서라도 import 하는 모듈의 파일 — 규칙 표(`rules.py`)는 규칙의 입력을, 사실 캐시(`index_cache.py`)는 뽑는 법의 판을 이것으로 정한다.
`rules.py` 에 두면 사실 캐시가 규칙 표를 import 해, 표에 규칙 한 줄을 더해도 모든 규칙의 캐시가 버려져 저장소 전체를 다시 읽었다.
"""

from __future__ import annotations

import ast
import functools
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[1]


@functools.cache
def list_imported_module_files(module_name: str) -> tuple[str, ...]:
    """`moduleName`(`lint.cpp_shape`)과 그것이 거쳐서라도 import 하는 Scripts 안 모듈의 파일(저장소 기준) — 손으로 적은 의존 목록은 낡는다."""
    set_seen: set[str] = set()
    list_pending = [module_name]
    while list_pending:
        name = list_pending.pop()
        base = SCRIPTS_DIR / name.replace(".", "/")
        path = base.with_suffix(".py") if base.with_suffix(".py").is_file() else base / "__init__.py"
        if name in set_seen or not path.is_file():
            continue   # 표준 · 외부 모듈
        set_seen.add(name)
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                list_pending.append(node.module)
                list_pending.extend(f"{node.module}.{alias.name}" for alias in node.names)   # `from common import Paths` 의 하위 모듈
            elif isinstance(node, ast.Import):
                list_pending.extend(alias.name for alias in node.names)
    return tuple(sorted(path.relative_to(SCRIPTS_DIR.parent).as_posix()
                        for name in set_seen
                        for path in (SCRIPTS_DIR / (name.replace(".", "/") + ".py"), SCRIPTS_DIR / name.replace(".", "/") / "__init__.py") if path.is_file()))


def get_module_file(module_name: str) -> str:
    """`lint.cpp_shape` → `Scripts/lint/cpp_shape.py`(저장소 기준)."""
    return (SCRIPTS_DIR / (module_name.replace(".", "/") + ".py")).relative_to(SCRIPTS_DIR.parent).as_posix()
