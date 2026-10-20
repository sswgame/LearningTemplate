#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`Scripts/` 의 모든 모듈은 이름이 하나다 — `Scripts/` 기준 점 이름(`lint.gate.CheckEngineLayers`, `common.Worktree`).

import 루트가 여럿이면 같은 파일이 `gate.CheckX` 와 `CheckX` 처럼 두 모듈로 로드되어 모듈 상태 · 예외 클래스 · `mock.patch.object` 대상이
갈라진다(`docs/plans/ScriptsStructure.md`). 모든 모듈을 점 이름으로 import 한 뒤 `sys.modules` 에서 같은 파일을 가진 모듈이 둘 이상이면 진다.
맨 이름 import 를 쓰는 자리는 `CheckScriptLayers` 가 막고, 이 테스트는 실제로 두 번 로드되는 경로를 잡는다.
"""

from __future__ import annotations

import importlib
import sys
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)


def listDottedNamesInternal() -> list[str]:
    """`Scripts/` 아래 모든 모듈의 점 이름. `__main__.py`(`-m Scripts` 의 진입점)는 import 하면 명령을 돌리므로 뺀다."""
    listName: list[str] = []
    for path in sorted(kScriptsRoot.rglob("*.py")):
        relative = path.relative_to(kScriptsRoot)
        if "__pycache__" in relative.parts or relative.name == "__main__.py":
            continue
        listPart = list(relative.with_suffix("").parts)
        if listPart[-1] == "__init__":
            listPart.pop()
        listName.append(".".join(listPart))
    return listName


class ScriptsImportIdentityTest(unittest.TestCase):
    def testEveryModuleLoadsUnderOneName(self) -> None:
        for name in listDottedNamesInternal():
            with self.subTest(module=name):
                importlib.import_module(name)
        mapFileToName: dict[Path, list[str]] = {}
        for name, module in list(sys.modules.items()):
            filePath = getattr(module, "__file__", None)
            if not filePath:
                continue
            resolved = Path(filePath).resolve()
            if kScriptsRoot in resolved.parents:
                mapFileToName.setdefault(resolved, []).append(name)
        listDuplicate = [f"{path.relative_to(kScriptsRoot).as_posix()}: {', '.join(sorted(names))}"
                         for path, names in sorted(mapFileToName.items()) if len(names) > 1]
        self.assertEqual(listDuplicate, [], "같은 파일이 여러 모듈 이름으로 로드됐습니다 — Scripts 기준 점 이름 하나로 import 합니다")


if __name__ == "__main__":
    unittest.main()
