"""
모듈 층 표 회귀 검사 — 실제 `Config/Rules/Layers/EngineLayers.toml` 로 층을 매긴다. 포괄 키가 없는 `Engine/Automation` 뿌리에 더한 파일은
표에 없는 파일이라 configure 오류이고(기능의 감시 값 · 조건은 주인 폴더에서 등록한다), 가장 긴 키가 이긴다.

실행: python -X utf8 Scripts/generate/tests/test_module_layers.py (CTest `Scripts.ModuleLayers`).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.generator_output import GeneratorError  # noqa: E402
from generate.module_layers import assign_tiers, find_layer_table, read_tier_table  # noqa: E402


class EngineLayerTableTest(unittest.TestCase):
    def setUp(self) -> None:
        self._map_tier = read_tier_table(find_layer_table("Engine").get_rule_file())

    def _assign(self, *list_relative: str) -> dict[str, int]:
        return assign_tiers(self._map_tier, list_relative, "EngineLayers.toml", "Engine")

    def test_file_added_to_automation_root_is_configure_error(self) -> None:
        for relative in ("Automation/AutomationNewWatches.cpp", "Automation/AutomationNewUtil.h", "Automation/Client/AutomationNewWatches.cpp"):
            with self.subTest(relative=relative), self.assertRaises(GeneratorError) as context:
                self._assign("Automation/Scenario/AutomationScenario.cpp", relative)
            self.assertIn(relative, str(context.exception))

    def test_automation_root_keeps_only_runner_interface_headers(self) -> None:
        self.assertEqual(self._assign("Automation/AutomationResult.h", "Automation/AutomationRuntime.h"),
                         {"Automation/AutomationResult.h": 11, "Automation/AutomationRuntime.h": 11})

    def test_longest_key_wins(self) -> None:
        tiers = self._assign("Automation/Watch/AutomationWatch.h", "Automation/Scenario/AutomationCaptureUtil.cpp", "Input/InputManager.cpp", "Input/InputWatches.cpp")
        self.assertEqual(tiers, {"Automation/Watch/AutomationWatch.h": 5, "Automation/Scenario/AutomationCaptureUtil.cpp": 6, "Input/InputManager.cpp": 3,
                                 "Input/InputWatches.cpp": 5})


if __name__ == "__main__":
    unittest.main()
