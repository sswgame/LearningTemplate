#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""작업 장부(dev.RunClaims)의 판단 — 겹침 · 덮어쓰기 · release · 공지. git 없이 임시 폴더로."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from dev import RunClaims  # noqa: E402


class ClaimsTest(unittest.TestCase):
    def testOverlapIsDecidedPerPathSegment(self) -> None:
        self.assertTrue(RunClaims.isOverlapping("Source/Engine", "Source/Engine/A.cpp"))
        self.assertTrue(RunClaims.isOverlapping("Source/Engine/A.cpp", "source/engine"))
        self.assertTrue(RunClaims.isOverlapping("Source/Engine", "Source/Engine"))
        self.assertFalse(RunClaims.isOverlapping("Source/Engine", "Source/EngineX/A.cpp"))
        self.assertFalse(RunClaims.isOverlapping("Source/Engine/A.cpp", "Source/Engine/B.cpp"))

    def testPathsAreNormalizedAndOutsidePathsRejected(self) -> None:
        root = Path(tempfile.gettempdir()).resolve() / "claims-root"
        self.assertEqual("Source/Engine", RunClaims.normalizeClaimPath("Source\\Engine/", root))
        self.assertEqual("Source/Engine", RunClaims.normalizeClaimPath(str(root / "Source" / "Engine"), root))
        with self.assertRaises(ValueError):
            RunClaims.normalizeClaimPath("../x", root)
        with self.assertRaises(ValueError):
            RunClaims.normalizeClaimPath("", root)
        with self.assertRaises(ValueError):
            RunClaims.normalizeClaimPath(str(root.parent / "elsewhere"), root)

    def testAddOverwritesTheSameOwnerAndCheckFindsOthers(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            RunClaims.writeClaim(directory, RunClaims.Claim("a", ("Source/Engine",), "first", os.getpid(), 1.0))
            RunClaims.writeClaim(directory, RunClaims.Claim("a", ("docs",), "second", os.getpid(), 2.0))
            RunClaims.writeClaim(directory, RunClaims.Claim("b", ("Source/Core",), "", os.getpid(), 3.0))
            listClaim = RunClaims.readClaims(directory)
            self.assertEqual(2, len(listClaim))
            self.assertEqual(("docs",), next(claim for claim in listClaim if claim.owner == "a").paths)
            self.assertEqual([], RunClaims.findConflicts(listClaim, ["Source/Engine/A.cpp"], None))
            conflict = RunClaims.findConflicts(listClaim, ["Source/Core/X.cpp"], None)
            self.assertEqual(["b"], [claim.owner for claim, _, _ in conflict])
            self.assertEqual([], RunClaims.findConflicts(listClaim, ["Source/Core/X.cpp"], "b"))  # 내 항목은 뺀다

    def testReleaseRemovesTheClaimAndUnknownOwnerIsAnError(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            RunClaims.writeClaim(directory, RunClaims.Claim("a", ("docs",), "", os.getpid(), 1.0))
            args = RunClaims.buildParser().parse_args(["release", "a"])
            self.assertEqual(0, RunClaims.runRelease(directory, args))
            self.assertEqual([], RunClaims.readClaims(directory))
            with self.assertRaises(ValueError):
                RunClaims.runRelease(directory, args)

    def testBrokenClaimFileStopsInsteadOfBeingSkipped(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "bad.json").write_text("{", encoding="utf-8")
            with self.assertRaises(ValueError):
                RunClaims.readClaims(directory)

    def testPostsAreReadSinceATime(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            parser = RunClaims.buildParser()
            RunClaims.runPost(directory, parser.parse_args(["post", "a", "X API changed"]))
            listPost = RunClaims.readPosts(directory, 0.0)
            self.assertEqual(["X API changed"], [record["text"] for record in listPost])
            self.assertEqual([], RunClaims.readPosts(directory, float(listPost[0]["time"]) + 1))
            with self.assertRaises(ValueError):
                RunClaims.runPost(directory, parser.parse_args(["post", "a", "  "]))
            with self.assertRaises(ValueError):
                RunClaims.checkOwnerName("bad owner")


if __name__ == "__main__":
    unittest.main()
