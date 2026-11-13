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

from generate.asset_reference_field_list import update_table  # noqa: E402

OWNER_COUNT = 12


def update_one_owner(arguments: tuple[str, int]) -> bool:
    """자식 프로세스 하나 — 주인 하나의 줄을 갈아 끼운다(빌드 폴더 하나의 생성 단계)."""
    table_path, owner_index = arguments
    return update_table(Path(table_path), {f"game:owner{owner_index}": {f"Type{owner_index}._field"}})


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
            self.assertTrue(update_table(table_path, {"game:a": {"A._x"}}))
            self.assertFalse(update_table(table_path, {"game:a": {"A._x"}}))


if __name__ == "__main__":
    unittest.main()
