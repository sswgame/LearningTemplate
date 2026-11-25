"""
소스 옮기기(`move_source.py`) 경로 규칙 회귀 검사 — 한 번만 바뀌고, 더 긴 이름 · 바깥 경로 · 곁 이름을 가른다.

실행: python -X utf8 Scripts/dev/tests/test_move_source.py (CTest `Scripts.MoveSource`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.move_source import make_path_rule  # noqa: E402


class PathRuleTest(unittest.TestCase):
    def test_single_pass(self) -> None:
        pattern, replacement = make_path_rule({"A/X.h": "A/Server/X.h"}, [("A/", "A/Server/")])
        text = '#include "A/X.h"\n#include "A/Y.h"\nMyA/X.h'
        expected = '#include "A/Server/X.h"\n#include "A/Server/Y.h"\nMyA/X.h'
        assert pattern.sub(replacement, text) == expected, pattern.sub(replacement, text)
        pattern, replacement = make_path_rule({}, [("cmake/Modules/", "cmake/Toolchain/")], root_base=True)
        text = "${CMAKE_SOURCE_DIR}/cmake/Modules/X.cmake emsdk/cmake/Modules/Platform/E.cmake cmake/Modules/Y.cmake"
        expected = "${CMAKE_SOURCE_DIR}/cmake/Toolchain/X.cmake emsdk/cmake/Modules/Platform/E.cmake cmake/Toolchain/Y.cmake"
        assert pattern.sub(replacement, text) == expected, pattern.sub(replacement, text)
        pattern, replacement = make_path_rule({"Games/Old/G.cpp": "Games/New/G.cpp"}, [("Games/Old/", "Games/New/")])
        text = "<item>Source/Games/Old</item> Games/Oldish Games/Old/G.cpp Engine Games/Old."
        expected = "<item>Source/Games/New</item> Games/Oldish Games/New/G.cpp Engine Games/Old."   # 끝 `/` 없는 폴더 이름도, 더 긴 이름 · 문장 끝 점은 아니다
        assert pattern.sub(replacement, text) == expected, pattern.sub(replacement, text)
        pattern, replacement = make_path_rule({"Config/Server/Old.json": "Config/Server/New.json"}, [], root_base=True, list_stem=[("Config/Server/Old", "Config/Server/New")])
        text = '"Config/Server/Old.json" "Config/Server/Old.online.json" "Config/Server/Old" Config/Server/Older.json'
        expected = '"Config/Server/New.json" "Config/Server/New.online.json" "Config/Server/New" Config/Server/Older.json'   # 곁 이름 · 확장자 없는 이름
        assert pattern.sub(replacement, text) == expected, pattern.sub(replacement, text)


if __name__ == "__main__":
    unittest.main()
