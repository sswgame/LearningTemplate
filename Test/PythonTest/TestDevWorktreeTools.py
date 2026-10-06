#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""워크트리 도구(MakeWorktree · RemoveWorktree)와 CI 조회 도구(ListCiJobs)의 판단 — git · 네트워크 없이."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "dev"))

import ListCiJobs  # noqa: E402
import MakeWorktree  # noqa: E402
import RemoveWorktree  # noqa: E402


class MakeWorktreePlanTest(unittest.TestCase):
    def testPlanUsesSiblingFolderAndBranchPrefix(self) -> None:
        mainRoot = Path("/repo/LearningTemplate")
        plan = MakeWorktree.makePlan(mainRoot, "a5-split", "origin/main", None)
        self.assertEqual(plan.worktreePath, Path("/repo") / MakeWorktree.kWorktreeFolderName / "a5-split")
        self.assertEqual(plan.branch, "wt/a5-split")
        self.assertEqual(plan.base, "origin/main")

    def testPlanRejectsNamesThatAreNotFolderSafe(self) -> None:
        for name in ("", "a/b", "..", "a b", "x;rm"):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    MakeWorktree.makePlan(Path("/repo/main"), name, "origin/main", None)

    def testSharedLinksListOnlyFoldersMainHas(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            mainRoot = Path(temp) / "main"
            (mainRoot / "Tools/LLVM").mkdir(parents=True)
            (mainRoot / "build/vcpkg_installed").mkdir(parents=True)
            plan = MakeWorktree.makePlan(mainRoot, "unit", "origin/main", Path(temp) / "wt")
            listRel = sorted(link.relative_to(plan.worktreePath).as_posix() for link, _ in MakeWorktree.listSharedLinks(plan))
            self.assertEqual(listRel, ["Tools/LLVM", "build/vcpkg_installed"])

    def testDirectoryLinkIsRemovedWithoutTouchingItsTarget(self) -> None:
        # 지우기는 링크만 끊어야 한다 — 링크 너머(main 의 도구 폴더)가 남는지가 이 도구의 핵심 계약이다.
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "main/Tools/LLVM"
            target.mkdir(parents=True)
            (target / "keep.txt").write_text("x", encoding="utf-8")
            link = Path(temp) / "wt/Tools/LLVM"
            self.assertTrue(MakeWorktree.createDirectoryLink(link, target))
            self.assertTrue(RemoveWorktree.isDirectoryLink(link))
            self.assertFalse(RemoveWorktree.isDirectoryLink(target))
            RemoveWorktree.removeDirectoryLink(link)
            self.assertFalse(link.exists())
            self.assertTrue((target / "keep.txt").is_file())

    def testDifferentTripletBytesAreReported(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            mainRoot = Path(temp) / "main"
            plan = MakeWorktree.makePlan(mainRoot, "unit", "origin/main", Path(temp) / "wt")
            for root, text in ((mainRoot, b"set(A 1)\n"), (plan.worktreePath, b"set(A 1)\r\n")):
                folder = root / "cmake/Modules/Toolchain/Vcpkg"
                folder.mkdir(parents=True)
                (folder / "x64-windows.cmake").write_bytes(text)
            self.assertEqual(MakeWorktree.listDifferentTriplets(plan), ["cmake/Modules/Toolchain/Vcpkg/x64-windows.cmake"])


class ListCiJobsTest(unittest.TestCase):
    def testRepositoryIsReadFromBothRemoteForms(self) -> None:
        self.assertEqual(ListCiJobs.parseGithubRepository("git@github.com:owner/Repo.git"), "owner/Repo")
        self.assertEqual(ListCiJobs.parseGithubRepository("https://github.com/owner/Repo.git\n"), "owner/Repo")
        self.assertEqual(ListCiJobs.parseGithubRepository("https://github.com/owner/Repo"), "owner/Repo")
        self.assertIsNone(ListCiJobs.parseGithubRepository("https://gitlab.com/owner/Repo.git"))

    def testJobLinesNameTheFailedSteps(self) -> None:
        payload = {"jobs": [
            {"id": 1, "name": "windows", "conclusion": "failure",
             "steps": [{"name": "Configure", "conclusion": "success"}, {"name": "Test", "conclusion": "failure"}]},
            {"id": 2, "name": "linux", "conclusion": "success", "steps": []},
        ]}
        listLine = ListCiJobs.formatJobLines(payload)
        self.assertEqual(listLine[0], "1  windows  failure  | 실패 단계: Test")
        self.assertEqual(listLine[1], "2  linux  success")
        self.assertEqual([job["id"] for job in ListCiJobs.listFailedJobs(payload)], [1])

    def testOnlyFailureAnnotationsArePrinted(self) -> None:
        listAnnotation = [
            {"annotation_level": "warning", "path": "a.cpp", "start_line": 3, "title": "w", "message": "skip me"},
            {"annotation_level": "failure", "path": "b.cpp", "start_line": 7, "title": "FooTest.Bar", "message": "x" * 3000},
        ]
        listLine = ListCiJobs.formatFailureAnnotations(listAnnotation, maxChars=10)
        self.assertEqual(listLine, ["-- b.cpp:7 FooTest.Bar", "x" * 10])


if __name__ == "__main__":
    unittest.main()
