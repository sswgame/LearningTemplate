"""
MigrateComponentIds 회귀 검사 — 씬 · 프리팹 컴포넌트에 C++ `ComponentStableKey::resolveLocalIDs` 와 같은 id 를 적고, 같은 오브젝트의 옛 부착 키를 지역 id 키로 바꾼다.

실행: python -X utf8 Scripts/dev/tests/test_migrate_component_ids.py (CTest `Scripts.MigrateComponentIds`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.migrate_component_ids import fnv1a_64, format_key, migrate_text, resolve_local_ids  # noqa: E402


class MigrateComponentIdsTest(unittest.TestCase):
    def test_fnv_matches_the_engine_constants(self) -> None:
        self.assertEqual(fnv1a_64(""), 14695981039346656037)          # 빈 글은 씨앗 그대로
        self.assertEqual(fnv1a_64("a"), 0xaf63dc4c8601ec8c)           # FNV-1a 64 표준 값

    def test_written_ids_stay_and_legacy_ids_avoid_collision(self) -> None:
        legacy = fnv1a_64("Body#0")
        list_resolved = resolve_local_ids([("Body", 0), ("Body", legacy)])
        self.assertEqual(list_resolved[1], legacy)                    # 적힌 id 는 그대로
        self.assertNotEqual(list_resolved[0], legacy)                 # 옛 키 해시가 겹치면 다시 섞는다
        self.assertEqual(list_resolved[0], fnv1a_64("Body#0", legacy))

    def test_ids_are_added_and_same_object_attach_keys_are_rewritten(self) -> None:
        text = ('<GameObject><_listComponent>'
                '<BoxComponent _componentName="None"/>'
                '<MeshComponent _attachComponent="BoxComponent#0" _attachOwnerID="0"/>'
                '<MeshComponent _localID="7" _attachComponent="Far#0" _attachOwnerID="12"/>'
                '</_listComponent></GameObject>')
        rewritten, added, attached = migrate_text(text)
        box_id = fnv1a_64("BoxComponent#0")
        self.assertEqual(added, 2)
        self.assertEqual(attached, 1)
        self.assertIn(f'<BoxComponent _localID="{box_id}" _componentName="None"/>', rewritten)
        self.assertIn(f'_attachComponent="{format_key(box_id)}"', rewritten)
        self.assertIn('_attachComponent="Far#0" _attachOwnerID="12"', rewritten)   # 다른 오브젝트의 부착은 그대로
        self.assertEqual(migrate_text(rewritten)[0], rewritten)                     # 두 번 돌려도 같다


if __name__ == "__main__":
    unittest.main()
