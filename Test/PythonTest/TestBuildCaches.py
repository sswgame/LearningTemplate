#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.BuildCaches — 콜드 측정이 지울 캐시 목록(cmake/Config/BuildCaches.json)을 읽고 지우는가, 링크 폴더는 지우지 않는가, 정본을 못 읽으면 멈추는가."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.BuildCaches import clearColdCaches, kBuildCachesFile, loadColdCaches  # noqa: E402


def makeDirectoryLink(link: Path, target: Path) -> None:
    """`link` 가 `target` 을 가리키는 폴더 링크. 심볼릭 링크가 막히면 Windows 정션."""
    try:
        os.symlink(target, link, target_is_directory=True)
    except OSError:
        completed = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True, text=True)
        assert completed.returncode == 0, completed.stdout + completed.stderr


class RealFileTest(unittest.TestCase):
    def testRealListHasTheLinkCacheAndCmakeReadsTheSameKey(self) -> None:
        mapCache = {cache.name: cache for cache in loadColdCaches(kRepositoryRoot)}
        self.assertEqual(("build", "LTOCache"), (mapCache["lto_link_cache"].base, mapCache["lto_link_cache"].path))
        # 링크 캐시 이름은 CMake 도 이 파일에서 읽는다 — 이름을 CMake 에 다시 적으면 지우는 폴더와 쓰는 폴더가 갈라진다.
        layoutText = (kRepositoryRoot / "cmake" / "Engine" / "BuildLayout.cmake").read_text(encoding="utf-8")
        self.assertIn("cold_clear lto_link_cache path", layoutText)
        self.assertNotIn("/LTOCache\"", layoutText)

    def testKeptFoldersAreNotInTheClearList(self) -> None:
        document = json.loads((kRepositoryRoot / kBuildCachesFile).read_text(encoding="utf-8"))
        listClearPath = {entry["path"] for entry in document["cold_clear"].values()}
        for name, entry in document["keep"].items():
            self.assertNotIn(entry["path"], listClearPath, name)
            self.assertTrue(entry["reason"], name)


class ClearTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        base = Path(self.tempDir.name).resolve()
        self.root = base / "repo"
        self.buildDir = self.root / "build" / "Ninja-Shipping-Baseline"
        for folder in ("build/Ninja-Shipping-Baseline/LTOCache", "build/Ninja-Shipping-Baseline/Bin", "Saved/DDC", "Saved/Editor"):
            (self.root / folder).mkdir(parents=True)
        (self.root / "Saved" / "DDC" / "ab").write_text("x", encoding="utf-8")
        (self.root / "Saved" / "Editor" / "layout.ini").write_text("x", encoding="utf-8")
        self.writeList({"lto": ("build", "LTOCache"), "ddc": ("source", "Saved/DDC")})

    def writeList(self, mapEntry: dict) -> None:
        document = {"cold_clear": {name: {"base": base, "path": path, "reason": "r"} for name, (base, path) in mapEntry.items()}}
        target = self.root / kBuildCachesFile
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(document), encoding="utf-8")

    def testListedCachesAreRemovedAndNothingElse(self) -> None:
        listRemoved = clearColdCaches(self.buildDir, self.root)
        self.assertEqual(sorted([self.buildDir / "LTOCache", self.root / "Saved" / "DDC"]), sorted(listRemoved))
        self.assertFalse((self.buildDir / "LTOCache").exists())
        self.assertFalse((self.root / "Saved" / "DDC").exists())
        self.assertTrue((self.buildDir / "Bin").is_dir())
        self.assertTrue((self.root / "Saved" / "Editor" / "layout.ini").is_file())   # Saved 의 다른 상태는 캐시가 아니다
        self.assertEqual([], clearColdCaches(self.buildDir, self.root))   # 없는 것은 건너뛴다

    def testMissingListStopsInsteadOfClearingNothing(self) -> None:
        (self.root / kBuildCachesFile).unlink()
        with self.assertRaises(RuntimeError):
            clearColdCaches(self.buildDir, self.root)

    def testBadBaseOrPathStops(self) -> None:
        for bad in (("elsewhere", "LTOCache"), ("build", "../Outside"), ("build", "")):
            self.writeList({"lto": bad})
            with self.assertRaises(RuntimeError, msg=str(bad)):
                loadColdCaches(self.root)

    def testLinkFolderIsNeverDeleted(self) -> None:
        shared = Path(self.tempDir.name).resolve() / "shared"
        (shared / "keep.bin").parent.mkdir(parents=True)
        (shared / "keep.bin").write_text("x", encoding="utf-8")
        (self.buildDir / "LTOCache").rmdir()
        makeDirectoryLink(self.buildDir / "LTOCache", shared)
        with self.assertRaises(RuntimeError):
            clearColdCaches(self.buildDir, self.root)
        self.assertTrue((shared / "keep.bin").is_file())
        self.assertTrue((self.root / "Saved" / "DDC").is_dir())   # 하나라도 막히면 아무것도 지우지 않는다


if __name__ == "__main__":
    unittest.main()
