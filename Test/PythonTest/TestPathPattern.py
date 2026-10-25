#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`Scripts/common/PathPattern.py`(경로 마디 단위 패턴)의 단위 테스트.

C++ `AssetImportPathFilter` 의 테스트(`RulePatternsMatchPathSegments`)와 같은 사례를 둔다 — 두 구현이 같은 뜻을 지키는지 한눈에 맞춰 보게.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.PathPattern import matchesAnyPattern, matchesPattern, splitSegments  # noqa: E402
from lint.gate.CheckResourceCredits import findCaseOnlyDuplicatesInternal  # noqa: E402


class PathPatternTest(unittest.TestCase):
    def testStarDoesNotCrossFolders(self) -> None:
        self.assertTrue(matchesPattern("*/textures_raw/flat/*", "engine/textures_raw/flat/a.png"))
        self.assertFalse(matchesPattern("*/textures_raw/flat/*", "engine/textures_raw/flat/sub/a.png"))
        self.assertFalse(matchesPattern("*/textures_raw/flat/*", "a/b/textures_raw/flat/a.png"))

    def testDoubleStarCrossesZeroOrMoreSegments(self) -> None:
        self.assertTrue(matchesPattern("**/textures_raw/deep/**", "game/empty/textures_raw/deep/x/y/a.png"))
        self.assertTrue(matchesPattern("**/textures_raw/deep/**", "textures_raw/deep/a.png"))
        self.assertFalse(matchesPattern("**/textures_raw/deep/**", "game/textures_raw/shallow/a.png"))

    def testPatternWithoutSlashMatchesFileNameOnly(self) -> None:
        self.assertTrue(matchesPattern("*_mask.*", "any/dir/Hero_MASK.png"))   # 대소문자 무시
        self.assertFalse(matchesPattern("*_mask.*", "any/dir_mask.x/hero.png"))
        self.assertTrue(matchesPattern("README.md", "a/b/README.md"))

    def testBackslashAndBracketAreLiteralSeparators(self) -> None:
        self.assertEqual(["a", "b", "c.png"], splitSegments("a\\b//c.png"))
        self.assertTrue(matchesPattern("a/[x]/*.png", "a/[x]/c.png"))   # `[` 는 글자 그대로다(fnmatch 문자 집합이 아니다)
        self.assertFalse(matchesPattern("a/[x]/*.png", "a/x/c.png"))

    def testAnyPattern(self) -> None:
        self.assertTrue(matchesAnyPattern(["*.ogg", "x/*.png"], "x/a.png"))
        self.assertFalse(matchesAnyPattern([], "x/a.png"))


class CaseOnlyDuplicateTest(unittest.TestCase):
    """`CheckResourceCredits` 의 대소문자 쌍 검사(경로 목록을 받는 순수 함수)."""

    def testCaseOnlyPairIsReported(self) -> None:
        listViolation = findCaseOnlyDuplicatesInternal(["Resource/a/Foo.png", "Resource/a/foo.png", "Resource/a/bar.png"])
        self.assertEqual(1, len(listViolation))
        self.assertTrue(listViolation[0].startswith("Resource/a/foo.png: Resource/a/Foo.png 와 대소문자만 다릅니다"))

    def testFolderCaseDifferenceIsReported(self) -> None:
        self.assertEqual(1, len(findCaseOnlyDuplicatesInternal(["Resource/UI/a.png", "Resource/ui/a.png"])))

    def testDistinctNamesAndEmptyAreClean(self) -> None:
        self.assertEqual([], findCaseOnlyDuplicatesInternal(["Resource/a.png", "Resource/b.png", "Resource/sub/a.png"]))
        self.assertEqual([], findCaseOnlyDuplicatesInternal([]))

    def testThreeWayCollisionReportsEachLaterPath(self) -> None:
        self.assertEqual(2, len(findCaseOnlyDuplicatesInternal(["R/A.png", "R/a.png", "R/A.PNG"])))


if __name__ == "__main__":
    unittest.main(verbosity=2)
