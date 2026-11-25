"""
이름 전수 집계(`naming_census.py`) 회귀 검사 — 종류별 이름을 낱말로 쪼개 센다.

실행: python -X utf8 Scripts/dev/tests/test_naming_census.py (CTest `Scripts.NamingCensus`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.naming_census import build_census  # noqa: E402


class NamingCensusTest(unittest.TestCase):
    def test_names_are_split_into_words(self) -> None:
        census = build_census([("member", "_walkSpeed", "a.h"), ("function", "hoverMark", "a.h"), ("file", "SceneWalkKey", "Source/a/SceneWalkKey.h")])
        assert set(census["walk"]) == {"_walkSpeed", "SceneWalkKey"} and census["walk"]["_walkSpeed"] == ("member", "a.h"), census
        assert "hover" in census and "mark" in census and "_" not in "".join(census), census


if __name__ == "__main__":
    unittest.main()
