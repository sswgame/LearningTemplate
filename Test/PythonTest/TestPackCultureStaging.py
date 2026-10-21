#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
쿠킹 문화권 필터(`CookAssets.collectUnstagedTranslationTablesInternal`)의 단위 테스트.

로컬라이제이션 프로젝트의 `stagedCultures` 밖 문화권의 번역 표(`<문화권>.translation.json`)는 팩에 들어가지 않고, 출시 문화권의 표와 원문 표는 남는다.
프로젝트를 읽지 못하면 어느 표를 뺄지 모르므로 오류로 멈춘다. 저장소의 실제 `Resource/` 는 지금 출시 문화권 밖의 표가 없어 빠지는 것이 없다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.Config import ConfigError  # noqa: E402
from generate import CookAssets  # noqa: E402


class PackCultureStagingTest(unittest.TestCase):
    def testUnstagedTablesAreLeftOut(self) -> None:
        """`cultures` 에 있어도 `stagedCultures` 밖이면 뺀다."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / "data" / "localization"
            folder.mkdir(parents=True)
            project = {"name": "t", "sourceCulture": "en", "cultures": ["ko", "ja", "fr"], "stagedCultures": ["ko"], "stringTables": ["t.strings.json"]}
            (folder / "t.locproject.json").write_text(json.dumps(project), encoding="utf-8")
            for name in ("t.strings.json", "ko.translation.json", "ja.translation.json", "FR.translation.json"):
                (folder / name).write_text("{}", encoding="utf-8")
            unstaged = CookAssets.collectUnstagedTranslationTablesInternal(root)
            self.assertEqual({"data/localization/ja.translation.json", "data/localization/fr.translation.json"}, unstaged)

    def testBrokenProjectStops(self) -> None:
        """프로젝트에 `stagedCultures` 가 없으면 오류다(전부 넣거나 전부 빼는 폴백이 없다)."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "x.locproject.json").write_text(json.dumps({"name": "x"}), encoding="utf-8")
            with self.assertRaises(ConfigError):
                CookAssets.collectUnstagedTranslationTablesInternal(root)

    def testRepositoryShipsOnlyStagedTables(self) -> None:
        """저장소의 도메인 폴더는 지금 출시 문화권 밖의 표가 없다(엔진 · 에디터 · Shooter3D)."""
        resourceDir = kRepositoryRoot / "Resource"
        for domainDir in [resourceDir / "engine", resourceDir / "editor", resourceDir / "game" / "shooter3d"]:
            self.assertEqual(set(), CookAssets.collectUnstagedTranslationTablesInternal(domainDir), domainDir.name)


if __name__ == "__main__":
    unittest.main()
