#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""워크트리 도구(MakeWorktree · RemoveWorktree)와 CI 도구(ListCiJobs · CiFailureReport)의 판단 — git · 네트워크 없이."""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "dev"))

import CiFailureReport  # noqa: E402
import ListCiJobs  # noqa: E402
import ListWorktrees  # noqa: E402
import MakeWorktree  # noqa: E402
import RemoveWorktree  # noqa: E402
import RunFullBuild  # noqa: E402
import RunSharedLock as SharedLock  # noqa: E402


class SharedLockTest(unittest.TestCase):
    def testSecondOwnerWaitsUntilTheFirstReleases(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            lockDirectory = SharedLock.getLockDirectory(Path(temp), "vcpkg")
            self.assertTrue(SharedLock.tryAcquire(lockDirectory, "a", "install", 1, 1000.0))
            self.assertFalse(SharedLock.tryAcquire(lockDirectory, "b", "install", 2, 1001.0))
            self.assertFalse(SharedLock.release(lockDirectory, "b"))  # 남의 잠금은 놓지 못한다
            self.assertTrue(SharedLock.release(lockDirectory, "a"))
            self.assertTrue(SharedLock.tryAcquire(lockDirectory, "b", "install", 2, 1002.0))

    def testAbandonedLockIsTakenOver(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            lockDirectory = SharedLock.getLockDirectory(Path(temp), "vcpkg")
            self.assertTrue(SharedLock.tryAcquire(lockDirectory, "gone", "install", 0, 1000.0))  # pid 0 은 살아 있지 않다
            self.assertFalse(SharedLock.tryAcquire(lockDirectory, "b", "install", 2, 1000.0 + 60))
            self.assertTrue(SharedLock.tryAcquire(lockDirectory, "b", "install", 2, 1000.0 + SharedLock.kStaleSeconds + 1))

    @staticmethod
    def makeArgs(owner: str) -> argparse.Namespace:
        return argparse.Namespace(resource="gpu", owner=owner, purpose="test", pid=os.getpid(), slots=2, wait=0)

    def testSharedSlotsAdmitTwoAndBlockExclusive(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertEqual(0, SharedLock.acquireShared(root, self.makeArgs("a")))
            self.assertEqual(0, SharedLock.acquireShared(root, self.makeArgs("b")))
            self.assertEqual(3, SharedLock.acquireShared(root, self.makeArgs("c")))  # 칸 둘이 찼다
            # 독점은 잡더라도 공유 보유자가 놓을 때까지 기다린다(시한 0 이면 바로 진다)
            exclusive = SharedLock.getLockDirectory(root, "gpu")
            self.assertTrue(SharedLock.tryAcquire(exclusive, "x", "measure", os.getpid(), 1000.0))
            self.assertFalse(SharedLock.waitForSharedHolders(root, self.makeArgs("x"), 0.0))
            SharedLock.releaseShared(root, self.makeArgs("a"))
            SharedLock.releaseShared(root, self.makeArgs("b"))
            self.assertTrue(SharedLock.waitForSharedHolders(root, self.makeArgs("x"), 0.0))

    def testSharedWaitsWhileExclusiveIsHeld(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            exclusive = SharedLock.getLockDirectory(root, "gpu")
            self.assertTrue(SharedLock.tryAcquire(exclusive, "x", "measure", os.getpid(), 1000.0))
            self.assertEqual(3, SharedLock.acquireShared(root, self.makeArgs("a")))
            self.assertFalse(any(slot.is_dir() for slot in SharedLock.getSharedSlotDirectories(root, "gpu", 2)))  # 물러난 칸은 남기지 않는다
            self.assertTrue(SharedLock.release(exclusive, "x"))
            self.assertEqual(0, SharedLock.acquireShared(root, self.makeArgs("a")))

    def testSlotCountComesFromTheResourceTable(self) -> None:
        self.assertEqual(SharedLock.getSharedSlotCount("build"), 2)   # 동시 빌드 한도(작업 방식 2 절)
        with self.assertRaises(ValueError):
            SharedLock.getSharedSlotCount("vcpkg")   # 공유 모드가 없는 자원

    def testRunHoldingReleasesEvenWhenTheCommandFails(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            args = argparse.Namespace(resource="build", owner="a", purpose="test", shared=True, slots=2, wait=0, belowNormal=False)
            code = SharedLock.runHolding(root, args, [sys.executable, "-c", "import sys; sys.exit(3)"])
            self.assertEqual(code, 3)
            self.assertFalse(any(slot.is_dir() for slot in SharedLock.getSharedSlotDirectories(root, "build", 2)))

    def testResourceNameMustBeFolderSafe(self) -> None:
        for name in ("", "a/b", ".."):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    SharedLock.getLockDirectory(Path("/repo"), name)


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


class WorktreeOwnerTest(unittest.TestCase):
    def testOwnerRecordRoundTripsAndTellsOtherLiveOwners(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            gitDirectory = Path(temp)
            self.assertIsNone(MakeWorktree.readWorktreeOwner(gitDirectory))   # 이 기능 전 워크트리
            owner = MakeWorktree.WorktreeOwner(owner="unit", pid=os.getpid(), session="s1", startTime=1000.0)
            MakeWorktree.writeWorktreeOwner(gitDirectory, owner)
            self.assertEqual(MakeWorktree.readWorktreeOwner(gitDirectory), owner)
            self.assertFalse(MakeWorktree.isOwnedByOtherLiveProcess(owner, os.getpid()))   # 내 것
            self.assertTrue(MakeWorktree.isOwnedByOtherLiveProcess(owner, None))           # 내 번호를 모르면 살아 있는 주인은 남이다
            gone = MakeWorktree.WorktreeOwner(owner="gone", pid=0, session="", startTime=1000.0)
            self.assertFalse(MakeWorktree.isOwnedByOtherLiveProcess(gone, None))           # 끝난 주인
            (gitDirectory / MakeWorktree.kOwnerFileName).write_text("{}", encoding="utf-8")
            with self.assertRaises(ValueError):
                MakeWorktree.readWorktreeOwner(gitDirectory)   # 깨진 기록은 "없음" 이 아니다

    def testOwnerPidComesFromArgumentThenEnvironment(self) -> None:
        from unittest import mock
        with mock.patch.dict(os.environ, {MakeWorktree.kOwnerPidEnvironment: "4321"}):
            self.assertEqual(MakeWorktree.resolveOwnerProcessId(None), 4321)
            self.assertEqual(MakeWorktree.resolveOwnerProcessId(7), 7)
        with mock.patch.dict(os.environ, {MakeWorktree.kOwnerPidEnvironment: ""}):
            self.assertIsNone(MakeWorktree.resolveOwnerProcessId(None))

    def testWorktreeListIsParsed(self) -> None:
        text = ("worktree D:/repo\nHEAD abc\nbranch refs/heads/main\n\n"
                "worktree D:/w/feat8\nHEAD def\nbranch refs/heads/wt/feat8\n\n"
                "worktree D:/w/old\nHEAD 123\ndetached\n")
        listEntry = ListWorktrees.parseWorktreeList(text)
        self.assertEqual([(entry.path.as_posix(), entry.branch) for entry in listEntry],
                         [("D:/repo", "main"), ("D:/w/feat8", "wt/feat8"), ("D:/w/old", "(detached)")])


class FullBuildEvidenceTest(unittest.TestCase):
    def testOnlyPushesToMainNeedEvidence(self) -> None:
        pushInput = (f"refs/heads/main {'a' * 40} refs/heads/main {'b' * 40}\n"
                     f"refs/heads/wt/x {'c' * 40} refs/heads/wt/x {'d' * 40}\n"
                     f"(delete) {RunFullBuild.kZeroHash} refs/heads/main {'e' * 40}\n")
        listNeeded = RunFullBuild.listPushedTreesNeedingEvidence(pushInput, lambda commit: "tree-of-" + commit[:1])
        self.assertEqual(listNeeded, [("refs/heads/main", "tree-of-a")])


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

    def testJobTableCarriesStepTimeExitCodeAndCtestTimeouts(self) -> None:
        payload = {"jobs": [
            {"id": 1, "name": "tsan", "conclusion": "failure", "status": "completed",
             "started_at": "2026-10-11T14:52:06Z", "completed_at": "2026-10-11T15:10:12Z",
             "steps": [{"name": "Build", "conclusion": "success", "started_at": "2026-10-11T14:53:00Z", "completed_at": "2026-10-11T14:58:00Z"},
                       {"name": "Test", "conclusion": "failure", "started_at": "2026-10-11T14:58:22Z", "completed_at": "2026-10-11T15:10:06Z"},
                       {"name": "Upload", "conclusion": "skipped"}]},
        ]}
        listAnnotation = [
            {"annotation_level": "notice", "title": "vcpkg", "message": "Process completed with exit code 99."},
            {"annotation_level": "failure", "title": "ctest EngineTest_Shard1 [Timeout 180.0 s / 180 s]", "message": "..."},
            {"annotation_level": "failure", "title": "ctest EngineTest_Shard2 [exit 1 after 12.3 s / 180 s]", "message": "..."},
            {"annotation_level": "failure", "title": "", "message": "Process completed with exit code 8."},
        ]
        outcome = ListCiJobs.collectJobOutcome(listAnnotation)
        self.assertEqual(outcome.listExitCode, [8])
        listLine = ListCiJobs.formatJobLines(payload, {1: outcome})
        self.assertEqual(listLine, ["1  tsan  failure  18m06s  | 실패 단계: Test(11m44s, exit 8)  | ctest 진 것 2 (Timeout 1)"])
        self.assertTrue(ListCiJobs.isRunFinished(payload))
        self.assertFalse(ListCiJobs.isRunFinished({"jobs": [{"status": "in_progress"}]}))

    def testRateLimitIsRecognizedAndCachedBodyIsReusedOn304(self) -> None:
        import io
        import urllib.error
        import urllib.request
        from email.message import Message
        from unittest import mock

        def makeError(code: int, remaining: str, body: bytes) -> urllib.error.HTTPError:
            headers = Message()
            headers["X-RateLimit-Limit"] = "60"
            headers["X-RateLimit-Remaining"] = remaining
            headers["X-RateLimit-Reset"] = "1791734015"
            return urllib.error.HTTPError("https://example", code, "error", headers, io.BytesIO(body))

        with tempfile.TemporaryDirectory() as tempDir:
            client = ListCiJobs.GithubClient(Path(tempDir))
            client.writeCacheInternal("https://example/a", 'W/"tag"', {"jobs": []})
            with mock.patch.object(urllib.request, "urlopen", side_effect=makeError(304, "41", b"")):
                self.assertEqual(client.fetch("https://example/a"), {"jobs": []})
            self.assertEqual((client.callCount, client.cacheHitCount, client.remaining), (0, 1, 41))
            with mock.patch.object(urllib.request, "urlopen", side_effect=makeError(403, "0", b'{"message":"API rate limit exceeded"}')):
                with self.assertRaises(ListCiJobs.RateLimitError) as context:
                    client.fetch("https://example/b")
            self.assertEqual((context.exception.limit, context.exception.resetEpoch), (60, 1791734015))
            with mock.patch.object(urllib.request, "urlopen", side_effect=makeError(403, "12", b'{"message":"Must have admin rights"}')):
                with self.assertRaises(urllib.error.HTTPError):
                    client.fetch("https://example/c")

    def testSummaryCountsConsecutiveFailuresAndSkipsCancelledRuns(self) -> None:
        # 새것부터: 취소된 실행은 결과가 아니다. linux 는 세 번 이어서 실패했고(문서만 바뀐 실행에는 잡이 없어 세지도 끊지도 않는다), windows 는 이번만.
        listRun = [{"id": 9, "conclusion": "cancelled"}, {"id": 8, "conclusion": "failure", "head_sha": "abcdef123", "updated_at": "2026-10-17T01:00:00Z"},
                   {"id": 7, "conclusion": "failure"}, {"id": 6, "conclusion": "success"}, {"id": 5, "conclusion": "failure"}, {"id": 4, "conclusion": "success"}]
        listJudged = ListCiJobs.selectJudgedRuns(listRun, 4)
        self.assertEqual([run["id"] for run in listJudged], [8, 7, 6, 5])
        listJobPayload = [
            {"jobs": [{"name": "linux", "conclusion": "failure"}, {"name": "windows", "conclusion": "failure"}]},
            {"jobs": [{"name": "linux", "conclusion": "failure"}, {"name": "windows", "conclusion": "success"}]},
            {"jobs": [{"name": "lint", "conclusion": "success"}]},
            {"jobs": [{"name": "linux", "conclusion": "failure"}, {"name": "windows", "conclusion": "success"}]},
        ]
        listStreak = ListCiJobs.collectFailureStreaks(listJobPayload)
        self.assertEqual(listStreak, [("linux", 3), ("windows", 1)])
        line = ListCiJobs.formatCiSummary("main", listJudged, listStreak)
        self.assertIn("실행 8 (abcdef1, 2026-10-17): 실패 — linux 3회 연속, windows 1회 연속", line)
        self.assertEqual(ListCiJobs.collectFailureStreaks([{"jobs": [{"name": "linux", "conclusion": "success"}]}]), [])
        self.assertEqual(ListCiJobs.formatCiSummary("main", [], []), "main CI: 끝난 실행이 없다")

    def testSessionGuardReadsTheSameFailingExitCode(self) -> None:
        import importlib.util
        hookPath = Path(__file__).resolve().parents[2] / ".claude" / "hooks" / "SessionGuard.py"
        spec = importlib.util.spec_from_file_location("SessionGuard", hookPath)
        sessionGuard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sessionGuard)
        self.assertEqual(sessionGuard.kCiFailingExitCode, ListCiJobs.kExitCiFailing)


class CiFailureReportTest(unittest.TestCase):
    def testTestResultSeparatesTimeoutFromFailureAndCrash(self) -> None:
        timedOut = "[ RUN      ] FooTest.Hangs\n<end of output>\nTest time = 180.03 sec\n"
        failed = "[TestHost] shut down - exit code 1\n<end of output>\nTest time =  12.30 sec\n"
        crashed = "[ RUN      ] FooTest.Crashes\n<end of output>\nTest time =   4.00 sec\n"
        self.assertEqual(CiFailureReport.describeTestResult(timedOut, 180.0), "Timeout 180.0 s / 180 s")
        self.assertEqual(CiFailureReport.describeTestResult(failed, 180.0), "exit 1 after 12.3 s / 180 s")
        self.assertEqual(CiFailureReport.describeTestResult(crashed, None), "no exit line after 4.0 s")
        self.assertEqual(CiFailureReport.describeTestResult("", 180.0), "no elapsed time")

    def testMultiLineMessageKeepsItsContinuationLines(self) -> None:
        listOutput = ["  [FAILED] /x/TestSmoke.cpp:1912", "    Condition: child._exitCode == 0", "    Message  : property check failed:",
                      "sw::editor::Def::_stroke (editor::StrokeDef)", "sw::editor::Def::_typography (editor::TypographyDef)", "",
                      "[2026-10-15 14:19:13] [Test] [Info] - unloading", "not part of the message"]
        self.assertEqual(CiFailureReport.collectMarkedLinesInternal(listOutput), listOutput[:5])


if __name__ == "__main__":
    unittest.main()
