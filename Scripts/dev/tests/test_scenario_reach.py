"""
scenario_reach 회귀 검사 — 참조 닫힘(경로 · GUID · 임포트 원본 · include), 소스가 정의한 이름, 상한과 표본.

실행: python -X utf8 Scripts/dev/tests/test_scenario_reach.py (CTest `Scripts.ScenarioReach`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.scenario_reach import Reach, collect_references, find_asset_reach, find_source_reach, resolve_token, select_reached  # noqa: E402
from lint.asset_identity import ImportedFolder  # noqa: E402

IMPORTED = [ImportedFolder("models", "models_raw", (".glb",), (".mesh",), True)]
GUID = "a45146ba951afbe1d718e7011b81f425"
CONTENT = frozenset({"game/g/automation/walk.scenario.xml", "game/g/automation/idle.scenario.xml", "game/g/maps/town.scene.xml", "game/g/materials/stone.material",
                     "engine/shaders/lit.hlsl", "engine/shaders/light.hlsli", "game/g/models_raw/tree.glb", "game/g/textures/wall.png"})
TEXT = {
    "game/g/automation/walk.scenario.xml": '<Scenario><!-- game/g/textures/wall.png 는 주석이다 --><Launch variable="gv_firstScene" value="game/g/maps/town.scene.xml"/></Scenario>',
    "game/g/automation/idle.scenario.xml": '<Scenario><Expect watch="Nav.Agents" equals="1"/><ExpectLog id="Nav.Lost"/><Click/></Scenario>',
    "game/g/maps/town.scene.xml": f'<Scene><Mesh material="game/g/materials/stone.material" mesh="game/g/models/tree_leaf.mesh" image="{GUID}"/></Scene>',
    "game/g/materials/stone.material": '<MaterialDef shader="engine/shaders/lit.hlsl"/>',
    "engine/shaders/lit.hlsl": '  #include "light.hlsli"\n',
}
REFERENCES = {path: collect_references(path, text, CONTENT, {GUID: "game/g/textures/wall.png"}, IMPORTED) for path, text in TEXT.items()}


class ScenarioReachTest(unittest.TestCase):
    def test_imported_product_resolves_to_its_source(self) -> None:
        self.assertEqual(resolve_token("game/g/models/tree_leaf.mesh", CONTENT, IMPORTED), "game/g/models_raw/tree.glb")
        self.assertIsNone(resolve_token("game/g/models/treehouse.mesh", CONTENT, IMPORTED))

    def test_changed_include_model_and_guid_target_reach_the_scenario(self) -> None:
        for changed in ("engine/shaders/light.hlsli", "game/g/models_raw/tree.glb", "game/g/models_raw/tree.glb.meta", "game/g/textures/wall.png"):
            list_reach = find_asset_reach([changed], REFERENCES)
            self.assertEqual([reach.list_scenario for reach in list_reach], [("game/g/automation/walk.scenario.xml",)], changed)

    def test_unused_asset_and_changed_scenario_reach_nothing(self) -> None:
        self.assertEqual(find_asset_reach(["game/g/textures/other.png", "game/g/automation/walk.scenario.xml"], REFERENCES), [])

    def test_source_of_used_names_reaches_the_scenario(self) -> None:
        map_scenario = {path: text for path, text in TEXT.items() if path.endswith(".scenario.xml")}
        for source in ('SW_AUTOMATION_WATCH( navAgents, "Nav.Agents", "x", &read );', 'SW_AUTOMATION_QUERY_WATCH( navAgents, "Nav.Agents", "x", &read, false, false );',
                       'SW_AUTOMATION_STEP( click, "Click", &run, nullptr, {} );', 'SW_LOG_ERROR_ID( "Nav.Lost", "x" );'):
            self.assertEqual([reach.list_scenario for reach in find_source_reach({"Source/a.cpp": source}, map_scenario)], [("game/g/automation/idle.scenario.xml",)], source)
        self.assertEqual(find_source_reach({"Source/a.cpp": 'SW_LOG_ERROR_ID( "Nav.Other", "x" );'}, map_scenario), [])

    def test_wide_file_is_sampled_and_the_narrow_file_goes_first(self) -> None:
        wide = Reach("engine/shaders/common.hlsli", tuple(f"s{index}.scenario.xml" for index in range(10)))
        narrow = Reach("game/g/a.material", ("z.scenario.xml",))
        chosen, list_line = select_reached([wide, narrow], {"s0.scenario.xml"}, per_file=3, total=3)
        self.assertEqual(len(chosen), 3)
        self.assertIn("z.scenario.xml", chosen)
        self.assertNotIn("s0.scenario.xml", chosen)   # 이미 고른 것은 다시 세지 않는다
        self.assertEqual(len(list_line), 2)           # 파일 표본 · 모두 합친 상한
        self.assertEqual(chosen, select_reached([wide, narrow], {"s0.scenario.xml"}, per_file=3, total=3)[0])


if __name__ == "__main__":
    unittest.main()
