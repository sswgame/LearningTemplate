"""
clang_tidy 의 TU 고르기 회귀 검사 — 짝 .cpp 가 없는 헤더는 그 헤더를 (헤더를 거쳐서라도) 포함하는 TU 로 돈다.

실행: python -X utf8 Scripts/lint/tests/test_clang_tidy.py (CTest `Scripts.ClangTidySelect`). 임시 폴더만 쓴다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.clang_tidy import IncludeClosure, make_cache_key, select_entries  # noqa: E402


class SelectEntriesTest(unittest.TestCase):
    def test_header_without_pair_runs_through_including_unit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "Source"
            (source / "Core").mkdir(parents=True)
            (source / "Core/Pair.h").write_text("#pragma once\n", encoding="utf-8")
            (source / "Core/Pair.cpp").write_text('#include "Core/Pair.h"\n', encoding="utf-8")
            (source / "Core/Lone.h").write_text("#pragma once\n", encoding="utf-8")
            (source / "Core/Middle.h").write_text('#include "Core/Lone.h"\n', encoding="utf-8")
            (source / "Core/User.cpp").write_text('#include "Core/Middle.h"\n', encoding="utf-8")
            (source / "Core/Orphan.h").write_text("#pragma once\n", encoding="utf-8")
            list_entry = [{"directory": temporary, "file": (source / name).resolve().as_posix(), "command": "cl"} for name in ("Core/Pair.cpp", "Core/User.cpp")]
            selected, skipped = select_entries(list_entry, [source / "Core/Pair.h", source / "Core/Lone.h", source / "Core/Orphan.h"], source)
            self.assertEqual(sorted(Path(entry["file"]).name for entry in selected), ["Pair.cpp", "User.cpp"])   # 짝 · 헤더를 거친 포함
            self.assertEqual([path.name for path in skipped], ["Orphan.h"])   # 포함하는 TU 가 없다


class CacheKeyTest(unittest.TestCase):
    """결과 캐시 키 — 포함 닫힘(-I · /FI · 조건부 포함)의 내용이 바뀌면 키가 바뀌고, 닫힘 밖 파일은 키를 바꾸지 않는다."""

    def test_key_follows_include_closure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, generated = root / "Source", root / "gen"
            (source / "Core").mkdir(parents=True)
            generated.mkdir()
            (source / "Core/A.cpp").write_text('#include "Core/B.h"\n#ifdef X\n#include <Gen.h>\n#endif\n', encoding="utf-8")
            (source / "Core/B.h").write_text("#pragma once\n#include <vector>\n", encoding="utf-8")
            (generated / "Gen.h").write_text("int g;\n", encoding="utf-8")
            (root / "Forced.h").write_text("#pragma once\n", encoding="utf-8")
            (source / "Core/Other.h").write_text("#pragma once\n", encoding="utf-8")
            entry = {"directory": temporary, "file": (source / "Core/A.cpp").as_posix(),
                     "command": f"clang-cl -I{source} -I{generated} /FI{root / 'Forced.h'} -c Core/A.cpp"}

            def key() -> str | None:
                return make_cache_key("base", entry, IncludeClosure(), root)

            first = key()
            self.assertIsNotNone(first)
            (source / "Core/Other.h").write_text("int changed;\n", encoding="utf-8")
            self.assertEqual(key(), first)   # 닫힘 밖
            for path in (source / "Core/B.h", generated / "Gen.h", root / "Forced.h"):
                path.write_text(path.read_text(encoding="utf-8") + "// changed\n", encoding="utf-8")
                changed = key()
                self.assertNotEqual(changed, first, path.name)   # 따옴표 · 조건부 꺾쇠 · 강제 포함
                first = changed
            (source / "Gen.h").write_text("int shadow;\n", encoding="utf-8")   # 앞 포함 폴더(-I Source)에 같은 이름 헤더가 새로 생긴다
            self.assertNotEqual(key(), first)
            (source / "Core/A.cpp").write_text('#include SW_HEADER\n', encoding="utf-8")
            self.assertIsNone(key())   # 매크로 포함은 캐시하지 않는다


if __name__ == "__main__":
    unittest.main()
