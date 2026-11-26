"""
이름 전수 집계(`naming_census.py`) 회귀 검사 — 종류별 이름을 낱말로 쪼개 센다.

실행: python -X utf8 Scripts/refactor/tests/test_naming_census.py (CTest `Scripts.NamingCensus`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from refactor.naming_census import build_census, count_concepts  # noqa: E402


class NamingCensusTest(unittest.TestCase):
    def test_names_are_split_into_words(self) -> None:
        census = build_census([("member", "_walkSpeed", "a.h"), ("function", "hoverMark", "a.h"), ("file", "SceneWalkKey", "Source/a/SceneWalkKey.h")])
        assert set(census["walk"]) == {"_walkSpeed", "SceneWalkKey"} and census["walk"]["_walkSpeed"] == ("member", "a.h"), census
        assert "hover" in census and "mark" in census and "_" not in "".join(census), census

    def test_concepts_count_synonym_names(self) -> None:
        census = build_census([("function", "liveReload", "a.h"), ("member", "_reloadLive", "a.h"), ("type", "SceneFingerprint", "a.h"), ("type", "SceneDigest", "a.h")])
        rows = count_concepts(census, {"해시": {"word": "Digest", "synonyms": ["Fingerprint"]}, "바꿔 끼우기": {"word": "HotReload", "synonyms": ["LiveReload"]}})
        self.assertEqual(rows, [("해시", "Digest", "Fingerprint", ["SceneFingerprint"]), ("바꿔 끼우기", "HotReload", "LiveReload", ["liveReload"])])   # 음성: 차례가 다른 _reloadLive


if __name__ == "__main__":
    unittest.main()
