"""
MigrateAssetRefs 회귀 검사 — `AssetRef<T>` · `DataPath` · `_base` 의 경로 글을 GUID 참조로 바꾸고(임포트 산출물은 원본에서 난 GUID), 경로 글 칸 · 색인에 없는 경로 · 글 칸과 이름이 겹친 경로 값은 바꾸지 않는다.

실행: python -X utf8 Scripts/dev/tests/test_migrate_asset_refs.py (CTest `Scripts.MigrateAssetRefs`). 저장소를 읽지 않는다(글과 표를 테스트가 만든다).
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.migrate_asset_refs import RefFieldTable, load_ref_field_table, migrate_text  # noqa: E402
from lint.asset_identity import ImportedFolder, make_output_guid  # noqa: E402
from lint.asset_references import FieldConfig  # noqa: E402

GUID_MESH = "0123456789abcdef0123456789abcdef"
GUID_PREFAB = "fedcba9876543210fedcba9876543210"
CONFIG = FieldConfig(("game", "common"), (), (".xml", ".json"), frozenset({"_mesh", "_prefab", "_base", "_material", "_shader", "_file"}), (), (), ())
REF_FIELD_TABLE = RefFieldTable(frozenset({"_mesh", "_prefab", "_material"}), (), (), frozenset({"_material"}), frozenset({"_shader"}))
MAP_PATH_TO_GUID = {"game/x/rock.prefab.xml": GUID_PREFAB, "game/x/models_raw/rock.glb": GUID_MESH}
IMPORTED = (ImportedFolder("models", "models_raw", (".glb",), (".mesh",), True), ImportedFolder("textures", "textures_raw", (".png",), (".dds",), False))


class MigrateAssetRefsTest(unittest.TestCase):
    def test_only_asset_ref_field_values_turn_into_guid_references(self) -> None:
        text = '<Root _file="game/x/rock.prefab.xml"><C _prefab="Game/X/Rock.prefab.xml"/></Root>'
        result = migrate_text("Content/game/x/a.xml", text, CONFIG, REF_FIELD_TABLE, MAP_PATH_TO_GUID, IMPORTED)
        self.assertEqual(result.rewrite_count, 1)
        self.assertEqual(result.text, f'<Root _file="game/x/rock.prefab.xml"><C _prefab="guid:{GUID_PREFAB}"/></Root>')
        self.assertEqual(result.list_unresolved, [])
        again = migrate_text("Content/game/x/a.xml", result.text, CONFIG, REF_FIELD_TABLE, MAP_PATH_TO_GUID, IMPORTED)
        self.assertEqual((again.text, again.rewrite_count), (result.text, 0))   # 다시 돌려도 그대로다

    def test_base_and_data_path_values_turn_into_guid_references(self) -> None:
        """`_base` 는 파일 부분만(`#항목 id` 는 그대로), `DataPath` 는 다른 꼴과 이름이 겹치지 않을 때만. 색인에 없는 경로(팩 뿌리)는 남긴다."""
        text = '<Root _base="game/x/rock.prefab.xml" _shader="game/x"><E id="b" _base="game/x/rock.prefab.xml#a"/><E _base="a" _shader="game/x/rock.prefab.xml"/></Root>'
        result = migrate_text("Content/game/x/a.xml", text, CONFIG, REF_FIELD_TABLE, MAP_PATH_TO_GUID, IMPORTED)
        self.assertEqual(result.text, f'<Root _base="guid:{GUID_PREFAB}" _shader="game/x"><E id="b" _base="guid:{GUID_PREFAB}#a"/><E _base="a" _shader="guid:{GUID_PREFAB}"/></Root>')
        self.assertEqual((result.rewrite_count, result.kept_count, result.list_unresolved), (3, 1, []))

    def test_values_in_one_text_keep_their_places(self) -> None:
        text = '{"_prefab": "game/x/rock.prefab.xml", "_mesh": "game/x/models/rock.mesh", "other": 1}'
        result = migrate_text("Content/game/x/a.json", text, CONFIG, REF_FIELD_TABLE, MAP_PATH_TO_GUID, IMPORTED)
        self.assertEqual(result.text, f'{{"_prefab": "guid:{GUID_PREFAB}", "_mesh": "guid:{make_output_guid(GUID_MESH, ".mesh")}", "other": 1}}')   # 임포트 산출물 — 원본에서 난 GUID

    def test_paths_missing_from_index_are_reported(self) -> None:
        text = '<C _prefab="game/x/gone.prefab.xml"/>'
        result = migrate_text("Content/game/x/a.xml", text, CONFIG, REF_FIELD_TABLE, MAP_PATH_TO_GUID, IMPORTED)
        self.assertEqual(result.rewrite_count, 0)
        self.assertEqual(result.text, text)
        self.assertEqual(len(result.list_unresolved), 1)

    def test_path_value_of_a_ref_and_text_field_name_is_reported(self) -> None:
        """`_material` 은 한 주인에서 `AssetRef<Material>`, 다른 주인에서 글 칸(물리 재질 이름)이다 — 경로 값은 어느 칸의 것인지 몰라 멈추고, 경로가 아닌 값은 그대로 둔다."""
        text = '<A _material="game/x/m.material"/><B _material="Stone"/>'
        result = migrate_text("Content/game/x/a.xml", text, CONFIG, REF_FIELD_TABLE, {"game/x/m.material": GUID_MESH}, IMPORTED)
        self.assertEqual(result.rewrite_count, 0)
        self.assertEqual(result.text, text)
        self.assertEqual(len(result.list_ambiguous), 1)
        self.assertEqual(result.list_unresolved, [])

    def test_field_table_without_kinds_or_with_conflicts_is_reported(self) -> None:
        generated = {"listOwner": [
            {"owner": "", "listField": ["_mesh", "_base", "_material"], "listAssetRefField": ["_mesh", "_material"], "listDataPathField": ["_shader", "_base", "_name"],
             "listPathTextField": ["_base", "_mesh"], "listTextField": []},
            {"owner": "game:A", "listField": [], "listAssetRefField": [], "listPathTextField": [], "listTextField": ["_material", "_name"]},
            {"owner": "game:Old", "listField": ["_x"], "listAssetRefField": ["_x"], "listPathTextField": []},
        ]}
        ref_field_table = load_ref_field_table(generated)
        self.assertEqual(ref_field_table.set_ref_field, frozenset({"_mesh", "_material"}))
        self.assertEqual(ref_field_table.list_owner_without_kind, ("game:Old",))   # 글 칸 목록이 없는 표는 옛 파서의 것이다
        self.assertEqual(ref_field_table.list_conflict, ("_mesh",))
        self.assertEqual(ref_field_table.set_ambiguous_field, frozenset({"_material"}))
        self.assertEqual(ref_field_table.set_data_field, frozenset({"_shader"}))   # 경로 글 칸(_base) · 글 칸(_name)과 겹친 데이터 경로 이름은 바꾸지 않는다


if __name__ == "__main__":
    unittest.main()
