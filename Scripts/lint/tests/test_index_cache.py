"""
사실 저장소(`index_cache.open_facts`) 회귀 검사 — 고치기 · 지우기 · 더하기 뒤 요약이 처음부터 접은 것과 같고, 조각을 망가뜨리면 다시 지어 같은 답을 낸다.

실행: python -X utf8 Scripts/lint/tests/test_index_cache.py (CTest `Scripts.IndexCache`). 임시 폴더에 짓는다.
"""

from __future__ import annotations

import collections
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.index_cache import CACHE_FOLDER, open_facts  # noqa: E402


class IndexCacheTest(unittest.TestCase):
    def test_incremental_equals_fresh(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)

            def run(map_text: dict[str, str], want: list[str]) -> tuple[dict, collections.Counter[str]]:
                facts = open_facts(root, "probe", [], sorted(map_text), lambda _, text: text.split(), map_key={path: f"k{hash(text)}" for path, text in map_text.items()},
                                  read_texts=lambda list_path: {path: map_text.get(path) for path in list_path}, want=want, contribute=lambda _, list_word: list_word)
                return facts.map_fact, facts.summary

            def expect(map_text: dict[str, str], want: list[str], what: str) -> None:
                map_fact, summary = run(map_text, want)
                self.assertEqual(summary, collections.Counter(word for text in map_text.values() for word in text.split()), f"사실 저장소: {what} 뒤 요약이 처음부터 접은 것과 다르다")
                self.assertEqual(map_fact, {path: map_text[path].split() for path in sorted(want) if path in map_text}, f"사실 저장소: {what} 뒤 물은 사실이 다르다")

            map_text = {f"d{index % 7}/f{index}.txt": f"a b w{index % 3}" for index in range(40)}
            expect(map_text, [], "처음 짓기")
            map_text["d1/f1.txt"] = "a c"
            del map_text["d2/f2.txt"]
            map_text["d9/new.txt"] = "z z"
            expect(map_text, ["d1/f1.txt", "d9/new.txt"], "고치기 · 지우기 · 더하기")
            for shard in (root / CACHE_FOLDER / "probe").glob("shard-*.json"):
                shard.write_text("{}", encoding="utf-8")
                break
            expect(map_text, sorted(map_text), "조각을 망가뜨린")


if __name__ == "__main__":
    unittest.main()
