"""
AssetReferenceFieldList 회귀 검사 — 여러 빌드 폴더가 같은 표를 동시에 갱신해도 서로 다른 주인의 줄을 잃지 않는다(잠금 + 원자 교체).

실행: python -X utf8 Scripts/generate/Tests/TestAssetReferenceFieldList.py (CTest `Scripts.AssetReferenceFieldList`). 표는 임시 폴더의 사본이다.
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

from generate.AssetReferenceFieldList import updateTable  # noqa: E402

kOwnerCount = 12


def updateOneOwner(arguments: tuple[str, int]) -> bool:
    """자식 프로세스 하나 — 주인 하나의 줄을 갈아 끼운다(빌드 폴더 하나의 생성 단계)."""
    tablePath, ownerIndex = arguments
    return updateTable(Path(tablePath), {f"game:owner{ownerIndex}": {f"Type{ownerIndex}._field"}})


class AssetReferenceFieldListTest(unittest.TestCase):
    def test_parallelUpdatesKeepEveryOwner(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            tablePath = Path(folder) / "AssetReferenceFields.generated.json"
            tablePath.write_text(json.dumps({"listOwner": [{"owner": "", "listField": ["Engine._path"]}]}) + "\n", encoding="utf-8")
            context = multiprocessing.get_context("spawn")
            with concurrent.futures.ProcessPoolExecutor(max_workers=kOwnerCount, mp_context=context) as pool:
                list(pool.map(updateOneOwner, [(str(tablePath), index) for index in range(kOwnerCount)]))
            listOwner = json.loads(tablePath.read_text(encoding="utf-8"))["listOwner"]
            setOwner = {entry["owner"] for entry in listOwner}
            self.assertEqual(setOwner, {""} | {f"game:owner{index}" for index in range(kOwnerCount)})
            self.assertEqual([path.name for path in Path(folder).iterdir()], [tablePath.name])  # 임시 파일이 남지 않는다

    def test_unchangedTableIsNotRewritten(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            tablePath = Path(folder) / "AssetReferenceFields.generated.json"
            tablePath.write_text(json.dumps({"listOwner": []}) + "\n", encoding="utf-8")
            self.assertTrue(updateTable(tablePath, {"game:a": {"A._x"}}))
            self.assertFalse(updateTable(tablePath, {"game:a": {"A._x"}}))


if __name__ == "__main__":
    unittest.main()
