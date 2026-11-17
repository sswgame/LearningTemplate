"""
MigratePrefabLinks 회귀 검사 — 다른 프리팹을 가리키는 옛 순번 키(중첩 노드 인덱스 · Part 경로 · 덮어쓰기 key/after · 노드 attach)를 고정 지역 id 키로 바꾼다(감사 A-2).

실행: python -X utf8 Scripts/dev/tests/test_migrate_prefab_links.py (CTest `Scripts.MigratePrefabLinks`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.migrate_component_ids import fnv1a_64, format_key, migrate_text  # noqa: E402
from dev.migrate_prefab_links import LinkMigration  # noqa: E402

WHEEL = ('<Prefab name="Wheel"><GameObject _name="Wheel"><_listComponent>'
         '<SceneComponent _componentName="Root"/><MeshComponent _componentName="Tire"/>'
         '</_listComponent></GameObject></Prefab>')
CART = ('<Prefab name="Cart"><GameObject _name="Cart"><_listComponent><SceneComponent _componentName="Axle"/></_listComponent></GameObject>'
        '<PrefabInstance name="WheelA" prefab="game/x/wheel.prefab.xml" attach="Axle#0"/>'
        '<PrefabInstance name="WheelB" prefab="game/x/wheel.prefab.xml"/>'
        '</Prefab>')
SCENE = ('<Scene><entity id="1" name="Cart" prefab="game/x/cart.prefab.xml"><PrefabOverrides>'
         '<Part path="1"><PrefabOverrides><Override key="Tire#0"><MeshComponent _mesh="x"/></Override></PrefabOverrides></Part>'
         '<Override key="Axle#0"><SceneComponent _localPosition="1,1,1"/></Override>'
         '<Add after="Gone#0"><AudioComponent/></Add>'
         '</PrefabOverrides></entity></Scene>')


class MigratePrefabLinksTest(unittest.TestCase):
    def setUp(self) -> None:
        self.map_text = {"game/x/wheel.prefab.xml": migrate_text(WHEEL)[0], "game/x/cart.prefab.xml": migrate_text(CART)[0], "game/x/s.scene.xml": migrate_text(SCENE)[0]}
        self.links = LinkMigration({key: text for key, text in self.map_text.items() if key.endswith(".prefab.xml")})

    def test_nested_nodes_get_ids_and_attach_keys(self) -> None:
        cart = self.links.migrate("game/x/cart.prefab.xml", self.map_text["game/x/cart.prefab.xml"])
        node_b = format_key(fnv1a_64("game/x/cart.prefab.xml#node1"))
        self.assertIn(f'localID="{node_b}"', cart)
        self.assertIn(f'attach="{format_key(fnv1a_64("Axle#0"))}"', cart)
        self.assertEqual(self.links.migrate("game/x/cart.prefab.xml", cart), cart)   # 두 번 돌려도 같다

    def test_part_paths_and_override_keys_follow_the_prefabs(self) -> None:
        scene = self.links.migrate("game/x/s.scene.xml", self.map_text["game/x/s.scene.xml"])
        node_b = format_key(fnv1a_64("game/x/cart.prefab.xml#node1"))
        self.assertIn(f'<Part path="{node_b}">', scene)
        self.assertIn(f'<Override key="{format_key(fnv1a_64("Tire#0"))}">', scene)    # 부품(바퀴) 프리팹의 컴포넌트
        self.assertIn(f'<Override key="{format_key(fnv1a_64("Axle#0"))}">', scene)    # 인스턴스(수레) 프리팹의 컴포넌트
        self.assertIn('<Add after="Gone#0">', scene)                                  # 지운 컴포넌트는 그대로 두고 보고한다
        self.assertEqual(len(self.links.list_unresolved), 1)


if __name__ == "__main__":
    unittest.main()
