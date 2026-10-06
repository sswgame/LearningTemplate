#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.Search — 제외 폴더는 경로 글자가 아니라 **폴더 이름**으로 본다(체크아웃 경로에 "build" 가 든 워크트리), 버전은 자연순."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import collectRepositoryFiles, naturalVersionKey, resolveFileArguments  # noqa: E402


class CommonSearchTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tempDir = tempfile.TemporaryDirectory()
        # 저장소 자체가 "build" 가 든 경로 아래에 있다 — 그래도 저장소 안의 파일은 모두 대상이다.
        self.root = (Path(self._tempDir.name) / "build" / "repo-build-x").resolve()
        for relPath in ("Source/A.h", "Source/rebuild/B.h", "Source/build/C.h", "Source/Builder/D.h"):
            path = self.root / relPath
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x\n", encoding="utf-8")

    def tearDown(self) -> None:
        self._tempDir.cleanup()

    def relative(self, listPath: list[Path]) -> list[str]:
        return [path.relative_to(self.root).as_posix() for path in listPath]

    def testExcludedFolderIsMatchedByNameNotSubstring(self) -> None:
        listFile = self.relative(collectRepositoryFiles(self.root, ("Source",), suffixes=(".h",)))
        self.assertEqual(listFile, ["Source/A.h", "Source/Builder/D.h", "Source/rebuild/B.h"])

    def testFileArgumentsUseTheSameRule(self) -> None:
        listArgument = ["Source/A.h", "Source/build/C.h", "Source/rebuild/B.h"]
        self.assertEqual(self.relative(resolveFileArguments(self.root, listArgument, suffixes=(".h",))),
                         ["Source/A.h", "Source/rebuild/B.h"])

    def testNaturalVersionKey(self) -> None:
        self.assertEqual(sorted(["14.9.1", "14.44.35207", "14.38.0"], key=naturalVersionKey), ["14.9.1", "14.38.0", "14.44.35207"])


if __name__ == "__main__":
    unittest.main()
