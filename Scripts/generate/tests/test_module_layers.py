"""
모듈 층 표 회귀 검사 — 실제 `Config/Rules/Layers/EngineLayers.toml` 로 층을 매긴다. 포괄 키가 없는 `Engine/Automation` 뿌리에 더한 파일은
표에 없는 파일이라 configure 오류이고(기능의 감시 값 · 조건은 주인 폴더에서 등록한다), 가장 긴 키가 이긴다.
없는 파일을 가리키는 [tier] 줄도 configure 오류다(옮긴 파일의 줄이 남아 통합 구성이 선 자리). 실제 표 셋(Engine · GameFramework · Editor)을
configure 와 같은 생성기로 돌려 지금 저장소에서 서는지도 본다.

실행: python -X utf8 Scripts/generate/tests/test_module_layers.py (CTest `Scripts.ModuleLayers`).
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.generator_output import GeneratorError  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from generate.editor_layers import generate_editor_layers  # noqa: E402
from generate.module_layers import LIST_LAYER_TABLE, assign_tiers, require_no_stale_keys, find_layer_table, generate_module_layers, read_tier_table  # noqa: E402


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
        tiers = self._assign("Automation/Watch/AutomationWatch.h", "Automation/Scenario/AutomationCaptureUtil.cpp", "Module/ModuleTypeRegistry.h", "Module/ModuleTypeRegistry.cpp")
        self.assertEqual(tiers, {"Automation/Watch/AutomationWatch.h": 5, "Automation/Scenario/AutomationCaptureUtil.cpp": 6, "Module/ModuleTypeRegistry.h": 1,
                                 "Module/ModuleTypeRegistry.cpp": 11})

    def test_row_naming_a_missing_file_is_configure_error(self) -> None:
        root_dir = get_project_root() / "Source" / "Engine"
        map_tier = dict(self._map_tier)
        map_tier["Input/NoSuchMovedFile.cpp"] = 5
        with self.assertRaises(GeneratorError) as context:
            require_no_stale_keys(map_tier, root_dir, "EngineLayers.toml")
        self.assertIn("Input/NoSuchMovedFile.cpp", str(context.exception))
        require_no_stale_keys(self._map_tier, root_dir, "EngineLayers.toml")   # 지금 표에는 낡은 줄이 없다


class RealLayerTablesConfigureTest(unittest.TestCase):
    def test_every_layer_table_writes_its_cmake(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            for table in LIST_LAYER_TABLE:
                with self.subTest(module=table._module):
                    generate_module_layers(table._module, Path(folder) / f"{table._module}Layers.cmake")
            generate_editor_layers(Path(folder) / "EditorLayers.cmake")


if __name__ == "__main__":
    unittest.main()
