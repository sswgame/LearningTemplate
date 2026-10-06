#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RunBuildScriptInventory — 죽은 CMake 함수 · 문자열 안 이름 · 진입점 모양 · common 을 비켜 간 호출을 세는지(임시 저장소로)."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "lint" / "report"))

import RunBuildScriptInventory  # noqa: E402


class BuildScriptInventoryTest(unittest.TestCase):
    def testCountsDeadFunctionsEntryPointsAndBypass(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "cmake").mkdir()
            (root / "cmake" / "a.cmake").write_text(
                'function(sw_used)\nendfunction()\nfunction(sw_dead)\nendfunction()\nmessage("sw_dead(")\nsw_used()\n', encoding="utf-8")
            (root / "Scripts" / "x").mkdir(parents=True)
            (root / "Scripts" / "x" / "Tool.py").write_text(
                'import subprocess\n\n\ndef main():\n    subprocess.run(["x"])\n\n\nif __name__ == "__main__":\n    main()\n', encoding="utf-8")

            cmake = RunBuildScriptInventory.inventoryCmake(root)
            python = RunBuildScriptInventory.inventoryPython(root)

        self.assertEqual(cmake["functions"]["sw_dead"]["calls"], 0)
        self.assertEqual(cmake["functions"]["sw_used"]["calls"], 1)
        self.assertEqual(python["entryPoints"]["Scripts/x/Tool.py"]["main"], "main()")
        self.assertEqual(python["bypass"]["Scripts/x/Tool.py"]["subprocess"], 1)


if __name__ == "__main__":
    unittest.main()
