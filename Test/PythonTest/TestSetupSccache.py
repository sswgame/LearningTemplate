#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""setup.SetupSccache — 경로 무관 캐시 설정(basedirs = 저장소 + git 워크트리 루트, 캐시 폴더 하나)과 손으로 쓴 설정 보호."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import normalizePath, runProcess  # noqa: E402
from setup import SetupSccache  # noqa: E402


def runGitInternal(cwd: Path, *args: str) -> None:
    result = runProcess(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "core.autocrlf=false", *args], cwd=cwd)
    if not result.bSucceeded:
        raise RuntimeError(result.output)


def applyWithoutRestartInternal(projectRoot: Path) -> bool:
    """설정만 쓴다 — 시험이 이 기계의 sccache 서버를 멈추면 안 된다."""
    return SetupSccache.applySccacheConfig(projectRoot, None, bRestart=False)


class SetupSccacheTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tempDir = tempfile.TemporaryDirectory()
        self._root = Path(self._tempDir.name).resolve()
        self._configPath = self._root / "sccache-config" / "config"
        patcher = mock.patch.dict(os.environ, {"SCCACHE_CONF": str(self._configPath)})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self._tempDir.cleanup)

    def makeRepositoryWithWorktreeInternal(self) -> tuple[Path, Path]:
        mainRoot = self._root / "main"
        worktreeRoot = self._root / "wt" / "feature"
        mainRoot.mkdir()
        runGitInternal(mainRoot, "init", "-q")
        (mainRoot / "a.txt").write_text("a", encoding="utf-8")
        runGitInternal(mainRoot, "add", "a.txt")
        runGitInternal(mainRoot, "commit", "-q", "-m", "init")
        runGitInternal(mainRoot, "worktree", "add", "-q", "-b", "feature", str(worktreeRoot))
        return mainRoot, worktreeRoot

    def testConfigListsEveryWorktreeAndOneCacheDir(self) -> None:
        mainRoot, worktreeRoot = self.makeRepositoryWithWorktreeInternal()
        # 워크트리 쪽에서 불러도 캐시 폴더는 주 저장소 것 — 서버를 띄운 워크트리에 따라 캐시가 갈리면 안 된다.
        self.assertTrue(applyWithoutRestartInternal(worktreeRoot))
        text = self._configPath.read_text(encoding="utf-8")
        self.assertTrue(text.startswith(SetupSccache.kSccacheConfigMarker))
        self.assertIn(f'basedirs = ["{normalizePath(mainRoot)}", "{normalizePath(worktreeRoot)}"]', text)
        self.assertIn(f'dir = "{normalizePath(mainRoot / "build" / "sccache_cache")}"', text)

    def testSecondApplyIsUnchanged(self) -> None:
        mainRoot, _ = self.makeRepositoryWithWorktreeInternal()
        self.assertTrue(applyWithoutRestartInternal(mainRoot))
        before = self._configPath.stat().st_mtime_ns
        self.assertTrue(applyWithoutRestartInternal(mainRoot))
        self.assertEqual(self._configPath.stat().st_mtime_ns, before)

    def testRemovedWorktreeIsDropped(self) -> None:
        mainRoot, worktreeRoot = self.makeRepositoryWithWorktreeInternal()
        runGitInternal(mainRoot, "worktree", "remove", str(worktreeRoot))
        self.assertTrue(applyWithoutRestartInternal(mainRoot))
        self.assertNotIn(normalizePath(worktreeRoot), self._configPath.read_text(encoding="utf-8"))

    def testHandWrittenConfigIsLeftAlone(self) -> None:
        mainRoot, _ = self.makeRepositoryWithWorktreeInternal()
        self._configPath.parent.mkdir(parents=True)
        self._configPath.write_text("[cache.disk]\ndir = \"x\"\n", encoding="utf-8")
        self.assertFalse(applyWithoutRestartInternal(mainRoot))
        self.assertEqual(self._configPath.read_text(encoding="utf-8"), "[cache.disk]\ndir = \"x\"\n")

    def testNotARepositoryFallsBackToItself(self) -> None:
        plainDir = self._root / "plain"
        plainDir.mkdir()
        self.assertEqual(SetupSccache.listWorktreeRoot(plainDir), [normalizePath(plainDir)])

    def testGitFailureInARepositoryWritesNothing(self) -> None:
        mainRoot, _ = self.makeRepositoryWithWorktreeInternal()
        failed = runProcess(["sw-no-such-tool-xyz"])
        with mock.patch.object(SetupSccache, "runProcess", lambda command, **_: failed):
            self.assertEqual(SetupSccache.listWorktreeRoot(mainRoot), [])
            self.assertFalse(applyWithoutRestartInternal(mainRoot))
        self.assertFalse(self._configPath.exists())

    def testOldSccacheGetsNoConfig(self) -> None:
        # 0.14 미만은 basedirs 를 모른다 — 그 키가 든 파일을 쓰면 서버가 뜨지 못할 수 있다.
        mainRoot, _ = self.makeRepositoryWithWorktreeInternal()
        with mock.patch.object(SetupSccache, "readSccacheVersion", lambda exePath: "0.8.1"):
            self.assertFalse(SetupSccache.applySccacheConfig(mainRoot, self._root / "sccache.exe", bRestart=False))
        self.assertFalse(self._configPath.exists())

    def testVersionIsParsedFromOutput(self) -> None:
        fakeExe = self._root / "fake.py"
        fakeExe.write_text("print('sccache 0.18.0')\n", encoding="utf-8")
        with mock.patch.object(SetupSccache, "runProcess", lambda command, **_: runProcess([sys.executable, fakeExe])):
            self.assertEqual(SetupSccache.readSccacheVersion(fakeExe), "0.18.0")


if __name__ == "__main__":
    unittest.main()
