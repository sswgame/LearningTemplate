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

from lint.clang_tidy import select_entries  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
