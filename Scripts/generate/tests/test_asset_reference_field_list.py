"""
AssetReferenceFieldList 회귀 검사 — 여러 빌드 폴더가 같은 표를 동시에 갱신해도 서로 다른 주인의 줄을 잃지 않는다(잠금 + 원자 교체).

실행: python -X utf8 Scripts/generate/tests/test_asset_reference_field_list.py (CTest `Scripts.AssetReferenceFieldList`). 표는 임시 폴더의 사본이다.
"""

from __future__ import annotations

import concurrent.futures
import json
import multiprocessing
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.asset_reference_field_list import merge_owners, read_built_owners, update_table  # noqa: E402

OWNER_COUNT = 12


def update_one_owner(arguments: tuple[str, int]) -> bool:
    """자식 프로세스 하나 — 주인 하나의 줄을 갈아 끼운다(빌드 폴더 하나의 생성 단계)."""
    table_path, owner_index = arguments
    return update_table(Path(table_path), {f"game:owner{owner_index}": {f"Type{owner_index}._field": {"ref"}}})


class AssetReferenceFieldListTest(unittest.TestCase):
    def test_parallel_updates_keep_every_owner(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            table_path = Path(folder) / "AssetReferenceFields.generated.json"
            table_path.write_text(json.dumps({"listOwner": [{"owner": "", "listField": ["Engine._path"]}]}) + "\n", encoding="utf-8")
            context = multiprocessing.get_context("spawn")
            with concurrent.futures.ProcessPoolExecutor(max_workers=OWNER_COUNT, mp_context=context) as pool:
                list(pool.map(update_one_owner, [(str(table_path), index) for index in range(OWNER_COUNT)]))
            list_owner = json.loads(table_path.read_text(encoding="utf-8"))["listOwner"]
            set_owner = {entry["owner"] for entry in list_owner}
            self.assertEqual(set_owner, {""} | {f"game:owner{index}" for index in range(OWNER_COUNT)})
            self.assertEqual([path.name for path in Path(folder).iterdir()], [table_path.name])  # 임시 파일이 남지 않는다

    def test_unchanged_table_is_not_rewritten(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            table_path = Path(folder) / "AssetReferenceFields.generated.json"
            table_path.write_text(json.dumps({"listOwner": []}) + "\n", encoding="utf-8")
            self.assertTrue(update_table(table_path, {"game:a": {"A._x": {"path"}}}))
            self.assertFalse(update_table(table_path, {"game:a": {"A._x": {"path"}}}))

    def test_field_names_are_listed_by_stored_form(self) -> None:
        """`AssetRef<T>` 칸(ref) · `DataPath` 칸(data) · 경로 글 칸(path) · 경로가 아닌 글 칸(text)을 따로 적는다 — GUID 이전 도구가 ref 칸만 바꾸고, ref 와 text 가 겹친 이름의 경로 값은 멈춘다.
        한 주인에서 두 꼴인 이름은 두 목록에 다 든다. 글 칸은 경로 칸 목록(`listField` — 검사 · 옮기기가 읽는다)에 들지 않는다."""
        with tempfile.TemporaryDirectory() as folder:
            list_path = Path(folder) / "AssetReferenceFields.list"
            list_path.write_text("_mesh\tMesh\tref\n_base\t-\tpath\n_shared\tMesh\tref\n_shared\t-\tpath\n_shader\t-\tdata\n_name\t-\ttext\n", encoding="utf-8")
            inputs_path = Path(folder) / "inputs.txt"
            inputs_path.write_text(f"game:a\t{list_path}\n", encoding="utf-8")
            list_owner = merge_owners([], read_built_owners(inputs_path))
            self.assertEqual(list_owner, [{"owner": "game:a", "listField": ["_base", "_mesh", "_shader", "_shared"],
                                           "listAssetRefField": ["_mesh", "_shared"], "listDataPathField": ["_shader"], "listPathTextField": ["_base", "_shared"],
                                           "listTextField": ["_name"]}])

    def test_two_column_lines_of_old_parser_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            list_path = Path(folder) / "AssetReferenceFields.list"
            list_path.write_text("_mesh\tMesh\n", encoding="utf-8")
            inputs_path = Path(folder) / "inputs.txt"
            inputs_path.write_text(f"game:a\t{list_path}\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                read_built_owners(inputs_path)


if __name__ == "__main__":
    unittest.main()
