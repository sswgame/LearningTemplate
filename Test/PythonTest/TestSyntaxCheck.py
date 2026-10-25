#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`syntax-check`(`common/SyntaxCheck.py` · `dev/RunSyntaxCheck.py`)의 자가 테스트 — 진짜 컴파일러 없이 **가짜 컴파일러**(소스에 `BROKEN` 이 있으면
`파일(줄,1): error: boom` 을 찍고 1 로 끝나는 파이썬 스크립트)와 가짜 컴파일 DB 로 본다.

본다: 틀린 소스가 파일:줄로 잡히는가 · DB 에 없는 새 파일이 이웃 TU 의 플래그를 받는가 · 바꾼 헤더가 탐침과 include 하는 소스로 검사되는가 ·
메인 저장소 경로가 워크트리로 옮겨지되 빌드 폴더 · Tools 는 그대로인가 · 바뀐 파일 목록이 커밋 + 작업 트리를 모두 보는가 · 컴파일 DB 가 없으면 종료 코드 2 인가.
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.Host import runGit  # noqa: E402
from common.SyntaxCheck import (SyntaxJob, listChangedSourceFiles, parseDiagnostics, planSyntaxJobs, rehomeDatabase,  # noqa: E402
                                runSyntaxJobs)
from dev.RunSyntaxCheck import main as syntaxCheckMain  # noqa: E402

kFakeCompiler = """import sys
source = sys.argv[-1]
for number, line in enumerate(open(source, encoding="utf-8").read().splitlines(), start=1):
    if "BROKEN" in line:
        print(f"{source}({number},1): error: boom")
        sys.exit(1)
"""


def runGitChecked(root: Path, *listArgument: str) -> None:
    result = runGit(["-c", "user.name=t", "-c", "user.email=t@t", *listArgument], cwd=root)
    assert result.bSucceeded, result.output


class SyntaxCheckFixture(unittest.TestCase):
    """git 저장소 하나(main 에 A.cpp · B.cpp · A.h 를 커밋)와 가짜 컴파일 DB 가 든 빌드 폴더."""

    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name).resolve()
        self.buildDir = self.root / "build" / "Ninja-Debug"
        (self.root / "Source" / "Mod").mkdir(parents=True)
        (self.buildDir / "generated" / "Engine").mkdir(parents=True)
        (self.buildDir / "generated" / "Engine" / "FlagOps.gen.h").write_text("// real\n", encoding="utf-8")
        (self.root / "fakecc.py").write_text(kFakeCompiler, encoding="utf-8")
        self.writeSource("A.cpp", '#include "Mod/A.h"\nint ok;\n')
        self.writeSource("B.cpp", '#include "Mod/A.h"\nBROKEN\n')
        self.writeSource("A.h", "#pragma once\n")
        self.database = [self.makeEntry(self.root / "Source" / "Mod" / name) for name in ("A.cpp", "B.cpp")]
        runGitChecked(self.root, "init", "-b", "main")
        runGitChecked(self.root, "add", "Source")
        runGitChecked(self.root, "commit", "-m", "base")

    def writeSource(self, name: str, text: str) -> Path:
        path = self.root / "Source" / "Mod" / name
        path.write_text(text, encoding="utf-8")
        return path

    def makeEntry(self, source: Path) -> dict:
        command = f'"{sys.executable}" "{self.root / "fakecc.py"}" -I{self.root}/Source -c -- {source}'
        return {"directory": str(self.buildDir), "command": command, "file": str(source)}

    def runJobs(self, listFile: list[Path]) -> tuple[list[tuple[SyntaxJob, list[str]]], list[str]]:
        with tempfile.TemporaryDirectory() as probeDir:
            listJob, listNote = planSyntaxJobs(self.root, self.buildDir, listFile, self.database, Path(probeDir))
            return runSyntaxJobs(listJob, self.buildDir, self.root), listNote


class SyntaxJobTest(SyntaxCheckFixture):
    def testBrokenSourceIsReportedWithFileAndLine(self) -> None:
        listResult, _ = self.runJobs([self.root / "Source" / "Mod" / "B.cpp"])
        self.assertEqual([["Source/Mod/B.cpp:2: boom"]], [listDiagnostic for _, listDiagnostic in listResult])

    def testGoodSourcePasses(self) -> None:
        listResult, _ = self.runJobs([self.root / "Source" / "Mod" / "A.cpp"])
        self.assertEqual([[]], [listDiagnostic for _, listDiagnostic in listResult])

    def testNewSourceBorrowsNeighborFlagsAndIsStillChecked(self) -> None:
        newSource = self.writeSource("New.cpp", "BROKEN\n")
        listResult, listNote = self.runJobs([newSource])
        self.assertEqual([["Source/Mod/New.cpp:1: boom"]], [listDiagnostic for _, listDiagnostic in listResult])
        self.assertTrue(any("이웃 TU" in note for note in listNote), listNote)

    def testChangedHeaderRunsProbeAndIncluders(self) -> None:
        listResult, _ = self.runJobs([self.root / "Source" / "Mod" / "A.h"])
        mapDiagnostic = {job.source.name: listDiagnostic for job, listDiagnostic in listResult}
        self.assertEqual(3, len(listResult))   # 헤더 단독 탐침 + A.cpp + B.cpp
        self.assertEqual(["Source/Mod/B.cpp:2: boom"], mapDiagnostic["B.cpp"])   # include 하는 소스의 오류가 헤더 변경에서 잡힌다
        self.assertEqual([], mapDiagnostic["A.cpp"])
        self.assertEqual([], mapDiagnostic["Mod_A_probe.cpp"])


class DiagnosticParseTest(unittest.TestCase):
    def testMsvcAndGccFormats(self) -> None:
        output = "\n".join(["D:\\r\\Source\\X.cpp(12,5): error: use of undeclared identifier 'foo'",
                            "/r/Source/Y.h:7:3: fatal error: 'Z.h' file not found",
                            "warning: unused", "clang: error: no such file"])
        self.assertEqual(["D:/r/Source/X.cpp:12: use of undeclared identifier 'foo'", "/r/Source/Y.h:7: 'Z.h' file not found",
                          "no such file"], parseDiagnostics(output))


class RehomeTest(unittest.TestCase):
    def testSourceSidePathsMoveButBuildAndToolsStay(self) -> None:
        mainRoot = Path(tempfile.gettempdir()).resolve() / "mainRepo"
        worktree = Path(tempfile.gettempdir()).resolve() / "wt" / "job"
        mainBack = str(mainRoot).replace("/", "\\")
        command = (f"{mainBack}\\Tools\\LLVM\\bin\\clang-cl.exe -I{mainBack}\\Source -I{mainBack}\\build\\Ninja-Debug\\generated "
                   f"/FI{mainRoot.as_posix()}/build/Ninja-Debug/gen.h /external:I{mainRoot.as_posix()}/ThirdParty -c -- {mainBack}\\Source\\X.cpp")
        entry = {"directory": str(mainRoot / "build"), "command": command, "file": f"{mainRoot.as_posix()}/Source/X.cpp"}
        rehomed = rehomeDatabase([entry], mainRoot, worktree)[0]
        worktreeBack = str(worktree).replace("/", "\\")
        self.assertIn(f"-I{worktreeBack}\\Source ", rehomed["command"])
        self.assertTrue(rehomed["command"].endswith(f"-- {worktreeBack}\\Source\\X.cpp"))
        self.assertEqual(f"{worktree.as_posix()}/Source/X.cpp", rehomed["file"])
        for stays in (f"{mainBack}\\Tools\\LLVM", f"-I{mainBack}\\build\\Ninja-Debug\\generated", f"/FI{mainRoot.as_posix()}/build/",
                      f"/external:I{mainRoot.as_posix()}/ThirdParty"):
            self.assertIn(stays, rehomed["command"])
        self.assertEqual(entry["directory"], rehomed["directory"])

    def testSameRepositoryIsUntouched(self) -> None:
        entry = {"directory": "d", "command": "cc -c x.cpp", "file": "x.cpp"}
        self.assertEqual([entry], rehomeDatabase([entry], Path.cwd(), Path.cwd()))


class ChangedFilesTest(SyntaxCheckFixture):
    def testCommittedAndWorkingTreeChangesAreBothCounted(self) -> None:
        runGitChecked(self.root, "checkout", "-b", "work")
        committed = self.writeSource("A.cpp", "int committed;\n")
        runGitChecked(self.root, "commit", "-am", "edit")
        staged = self.writeSource("A.h", "#pragma once\nint staged;\n")
        runGitChecked(self.root, "add", "Source")
        untracked = self.writeSource("New.cpp", "int fresh;\n")
        (self.root / "Source" / "Mod" / "Notes.txt").write_text("x", encoding="utf-8")
        (self.root / "Resource").mkdir()
        (self.root / "Resource" / "shader.h").write_text("x", encoding="utf-8")
        self.assertEqual(sorted([committed, staged, untracked]), listChangedSourceFiles(self.root, "main"))

    def testUnknownBaseIsAnErrorNotAnEmptyList(self) -> None:
        with self.assertRaises(RuntimeError):
            listChangedSourceFiles(self.root, "no-such-branch")


class CommandExitCodeTest(unittest.TestCase):
    def testMissingCompileDatabaseIsExitCodeTwo(self) -> None:
        with tempfile.TemporaryDirectory() as emptyBuild, contextlib.redirect_stderr(io.StringIO()) as captured:
            self.assertEqual(2, syntaxCheckMain(["--build-dir", emptyBuild]))
        self.assertIn("컴파일 DB", captured.getvalue())


if __name__ == "__main__":
    unittest.main()
