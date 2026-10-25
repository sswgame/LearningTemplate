#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""qa.WriteSnapshot - 전후 스냅샷 비교와 알려진 쓰기 위치 분류, 위치 표(Test/AppTest/HostWriteLocations.json)의 모양."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from qa import WriteSnapshot  # noqa: E402

kRepositoryRoot = Path(__file__).resolve().parents[2]


class WriteSnapshotTest(unittest.TestCase):
    def testRealTableLoadsAndCoversWorkLocations(self) -> None:
        listRoot, listLocation = WriteSnapshot.loadLocationTable(kRepositoryRoot / WriteSnapshot.kLocationTableRelativePath)
        self.assertTrue({location.root for location in listLocation} <= set(listRoot))
        self.assertTrue({"W1", "W2", "W5", "W7", "W8", "W13"} <= {location.locationId for location in listLocation})

    def testDiffFindsAddedChangedRemoved(self) -> None:
        before = {"a:x": (1, 1), "a:y": (2, 2), "a:z": (3, 3)}
        after = {"a:x": (1, 1), "a:y": (2, 9), "a:w": (4, 4)}
        self.assertEqual(WriteSnapshot.diffSnapshots(before, after), {"added": ["a:w"], "changed": ["a:y"], "removed": ["a:z"]})

    def testUnknownPathIsReportedAndKnownIsGrouped(self) -> None:
        listLocation = [WriteSnapshot.KnownLocation("W1", "resource", "scratch", ""), WriteSnapshot.KnownLocation("W13", "temp", "", "")]
        mapDiff = {"added": ["resource:scratch/a/b.xml", "temp:sw_1/c.ppm", "binSaved:Foo/bar.txt"], "changed": [], "removed": []}
        mapKnown, listUnknown = WriteSnapshot.classifyChanges(mapDiff, listLocation)
        self.assertEqual(sorted(mapKnown), ["W1", "W13"])
        self.assertEqual(listUnknown, ["added binSaved:Foo/bar.txt"])

    def testPrefixDoesNotMatchSiblingFolder(self) -> None:
        listLocation = [WriteSnapshot.KnownLocation("W1", "resource", "scratch", "")]
        self.assertIsNone(WriteSnapshot.findLocation("resource:scratch2/a.xml", listLocation))

    def testTakeSnapshotSkipsNonSwTempFolders(self) -> None:
        with tempfile.TemporaryDirectory() as tempName:
            temp = Path(tempName)
            (temp / "sw_1").mkdir()
            (temp / "sw_1" / "a.txt").write_text("x", encoding="utf-8")
            (temp / "other").mkdir()
            (temp / "other" / "b.txt").write_text("y", encoding="utf-8")
            self.assertEqual(list(WriteSnapshot.takeSnapshot({"temp": temp})), ["temp:sw_1/a.txt"])

    def testUnsupportedGlobStopsLoudly(self) -> None:
        with tempfile.TemporaryDirectory() as tempName:
            path = Path(tempName) / "t.json"
            path.write_text('{"roots": {"r": ""}, "locations": [{"id": "W9", "root": "r", "glob": "a/*.txt", "what": ""}]}', encoding="utf-8")
            with self.assertRaises(WriteSnapshot.LocationTableError):
                WriteSnapshot.loadLocationTable(path)


if __name__ == "__main__":
    unittest.main()
