#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ModuleImports.py

Scripts 안 모듈이 거쳐서라도 import 하는 모듈의 파일 — 규칙 표(`Rules.py`)는 규칙의 입력을, 사실 캐시(`FactCache.py`)는 뽑는 법의 판을 이것으로 정한다.
`Rules.py` 에 두면 사실 캐시가 규칙 표를 import 해, 표에 규칙 한 줄을 더해도 모든 규칙의 캐시가 버려져 저장소 전체를 다시 읽었다.
"""

from __future__ import annotations

import ast
import functools
from pathlib import Path

kScriptsDir = Path(__file__).resolve().parents[1]


@functools.cache
def listImportedModuleFiles(moduleName: str) -> tuple[str, ...]:
    """`moduleName`(`lint.CppShape`)과 그것이 거쳐서라도 import 하는 Scripts 안 모듈의 파일(저장소 기준) — 손으로 적은 의존 목록은 낡는다."""
    setSeen: set[str] = set()
    listPending = [moduleName]
    while listPending:
        name = listPending.pop()
        base = kScriptsDir / name.replace(".", "/")
        path = base.with_suffix(".py") if base.with_suffix(".py").is_file() else base / "__init__.py"
        if name in setSeen or not path.is_file():
            continue   # 표준 · 외부 모듈
        setSeen.add(name)
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                listPending.append(node.module)
                listPending.extend(f"{node.module}.{alias.name}" for alias in node.names)   # `from common import Paths` 의 하위 모듈
            elif isinstance(node, ast.Import):
                listPending.extend(alias.name for alias in node.names)
    return tuple(sorted(path.relative_to(kScriptsDir.parent).as_posix()
                        for name in setSeen
                        for path in (kScriptsDir / (name.replace(".", "/") + ".py"), kScriptsDir / name.replace(".", "/") / "__init__.py") if path.is_file()))


def getModuleFile(moduleName: str) -> str:
    """`lint.CppShape` → `Scripts/lint/CppShape.py`(저장소 기준)."""
    return (kScriptsDir / (moduleName.replace(".", "/") + ".py")).relative_to(kScriptsDir.parent).as_posix()
