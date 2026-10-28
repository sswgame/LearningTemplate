#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`syntax-check`(`common/SyntaxCheck.py` · `dev/RunSyntaxCheck.py`)의 자가 테스트 — 진짜 컴파일러 없이 **가짜 컴파일러**(소스에 `BROKEN` 이 있으면
`파일(줄,1): error: boom` 을 찍고 1 로 끝나는 파이썬 스크립트)와 가짜 컴파일 DB 로 본다.

본다: 틀린 소스가 파일:줄로 잡히는가 · DB 에 없는 새 파일이 이웃 TU 의 플래그를 받는가 · 바꾼 헤더가 탐침과 include 하는 소스로 검사되는가 ·
워크트리가 제 것으로 가진 경로만 옮겨지고 빌드 폴더 · 링크는 그대로인가 · 바뀐 파일 목록이 커밋 + 작업 트리를 모두 보는가 · 컴파일 DB 가 없으면 종료 코드 2 인가.
"""

from __future__ import annotations

import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.CompileDatabaseRehome import rehomeDatabase  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.SyntaxCheck import SyntaxJob, listChangedSourceFiles, parseDiagnostics, planSyntaxJobs, runSyntaxJobs  # noqa: E402
from dev.RunSyntaxCheck import main as syntaxCheckMain  # noqa: E402

kFakeCompiler = """import shlex, sys
listArgument = sys.argv[1:]
# Windows 는 인자를 응답 파일(`@파일`)로 받는다 — 진짜 clang 처럼 풀어 읽는다.
if len(listArgument) == 1 and listArgument[0].startswith("@"):
    listArgument = [token.strip('"') for token in shlex.split(open(listArgument[0][1:], encoding="utf-8").read(), posix=False)]
source = listArgument[-1]
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

    def makeEntry(self, source: Path, listExtraFlag: str = "") -> dict:
        command = f'{self.fakeCompilerCommand} -I{self.root}/Source {listExtraFlag} -c -- {source}'
        return {"directory": str(self.buildDir), "command": command, "file": str(source)}

    @property
    def fakeCompilerCommand(self) -> str:
        """Windows 는 컴파일러가 한 실행 파일이어야 응답 파일 호출이 맞는다 — 파이썬을 감싼 `.cmd` 를 컴파일러로 쓴다."""
        if sys.platform != "win32":
            return f'"{sys.executable}" "{self.root / "fakecc.py"}"'
        wrapper = self.root / "fakecc.cmd"
        wrapper.write_text(f'@"{sys.executable}" "{self.root / "fakecc.py"}" %*\r\n', encoding="utf-8")
        return f'"{wrapper}"'

    def runJobs(self, listFile: list[Path]) -> tuple[list[tuple[SyntaxJob, list[str]]], list[str]]:
        with tempfile.TemporaryDirectory() as probeDir:
            listJob, listNote = planSyntaxJobs(self.root, self.buildDir, listFile, self.database, Path(probeDir))
            return runSyntaxJobs(listJob, self.buildDir, self.root), listNote


class SyntaxJobTest(SyntaxCheckFixture):
    def testBrokenSourceIsReportedWithFileAndLine(self) -> None:
        listResult, _ = self.runJobs([self.root / "Source" / "Mod" / "B.cpp"])
        self.assertEqual([["Source/Mod/B.cpp:2: boom"]], [listDiagnostic for _, listDiagnostic in listResult])

    def testCommandLongerThanCmdLimitStillCompiles(self) -> None:
        # Windows 는 문자열 명령을 cmd.exe 로 돌리면 8191 자에서 "명령줄이 너무 깁니다" 로 끝난다 — 응답 파일로 넘겨 길이에 걸리지 않아야 한다(헤더 단독 검사와 같은 함수).
        listLongFlag = " ".join(f"-I{self.root}/Source/Long/Directory/Number{index}" for index in range(400))
        longEntry = self.makeEntry(self.root / "Source" / "Mod" / "A.cpp", listLongFlag)
        brokenEntry = self.makeEntry(self.root / "Source" / "Mod" / "B.cpp", listLongFlag)
        self.assertGreater(len(longEntry["command"]), 12000)
        self.database = [longEntry, brokenEntry]
        listResult, _ = self.runJobs([self.root / "Source" / "Mod" / "A.cpp", self.root / "Source" / "Mod" / "B.cpp"])
        self.assertEqual([[], ["Source/Mod/B.cpp:2: boom"]], [listDiagnostic for _, listDiagnostic in listResult])

    def testGoodSourcePasses(self) -> None:
        listResult, _ = self.runJobs([self.root / "Source" / "Mod" / "A.cpp"])
        self.assertEqual([[]], [listDiagnostic for _, listDiagnostic in listResult])

    def testNewSourceBorrowsNeighborFlagsAndIsStillChecked(self) -> None:
        newSource = self.writeSource("New.cpp", "BROKEN\n")
        listResult, listNote = self.runJobs([newSource])
        self.assertEqual([["Source/Mod/New.cpp:1: boom"]], [listDiagnostic for _, listDiagnostic in listResult])
        self.assertTrue(any("이웃 TU" in note for note in listNote), listNote)

    def testBorrowedFlagsDropEditorLayerStubsButOwnEntryKeepsThem(self) -> None:
        stubFlag = f"-I{self.buildDir}/generated/sw/EditorLayerStubs/tier0"
        self.database = [self.makeEntry(self.root / "Source" / "Mod" / "A.cpp", stubFlag)]
        newSource = self.writeSource("New.cpp", "int ok;\n")
        _, listNote = self.runJobs([newSource])
        self.assertTrue(any("껍데기는 뗀다" in note for note in listNote), listNote)
        with tempfile.TemporaryDirectory() as probeDir:
            listJob, _ = planSyntaxJobs(self.root, self.buildDir, [newSource, self.root / "Source" / "Mod" / "A.cpp"], self.database, Path(probeDir))
        mapCommand = {job.source.name: job.entry["command"] for job in listJob}
        self.assertNotIn("EditorLayerStubs", mapCommand["New.cpp"])   # 빌린 층 껍데기는 떼고
        self.assertIn("EditorLayerStubs", mapCommand["A.cpp"])        # 자기 항목은 그대로 둔다
        self.assertIn(str(self.root / "Source"), mapCommand["New.cpp"])

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


def makeDirectoryLink(link: Path, target: Path) -> None:
    """`link` 가 `target` 을 가리키는 폴더 링크(워크트리가 메인의 도구 · 빌드 폴더를 거는 모양)를 만든다. 심볼릭 링크가 막히면 Windows 정션."""
    try:
        os.symlink(target, link, target_is_directory=True)
    except OSError:
        completed = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True, text=True)
        assert completed.returncode == 0, completed.stdout + completed.stderr


class RehomeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        base = Path(self.tempDir.name).resolve()
        self.mainRoot = base / "mainRepo"
        self.worktree = base / "wt" / "job"
        for folder in ("Source", "Tools/LLVM/bin", "Tools/OnlineLoadBot", "build/Ninja-Debug/generated", "ThirdParty/stb"):
            (self.mainRoot / folder).mkdir(parents=True)
        # 워크트리: 추적되는 폴더는 제 것으로 있고, 도구 · 빌드 폴더는 메인으로의 링크다. 저장소에 새로 생긴 최상위 폴더(Novel)도 제 것이다.
        for folder in ("Source", "Tools/OnlineLoadBot", "Novel/Sub", "ThirdParty/stb"):
            (self.worktree / folder).mkdir(parents=True)
        (self.worktree / "Source" / "X.cpp").write_text("", encoding="utf-8")
        (self.worktree / "Tools" / "OnlineLoadBot" / "Main.cpp").write_text("", encoding="utf-8")
        makeDirectoryLink(self.worktree / "Tools" / "LLVM", self.mainRoot / "Tools" / "LLVM")
        (self.worktree / "build").mkdir()
        makeDirectoryLink(self.worktree / "build" / "vcpkg_installed", self.mainRoot / "build" / "Ninja-Debug")

    def testSourceSidePathsMoveButBuildAndLinksStay(self) -> None:
        mainRoot, worktree = self.mainRoot, self.worktree
        mainBack = str(mainRoot).replace("/", "\\")
        command = (f"{mainBack}\\Tools\\LLVM\\bin\\clang-cl.exe -I{mainBack}\\Source -I{mainBack}\\build\\Ninja-Debug\\generated "
                   f"/FI{mainRoot.as_posix()}/build/Ninja-Debug/gen.h -I{mainBack}\\build\\vcpkg_installed\\x /external:I{mainRoot.as_posix()}/ThirdParty/stb "
                   f"-c -- {mainBack}\\Source\\X.cpp")
        entry = {"directory": str(mainRoot / "build"), "command": command, "file": f"{mainRoot.as_posix()}/Source/X.cpp"}
        rehomed = rehomeDatabase([entry], mainRoot, worktree)[0]
        worktreeBack = str(worktree).replace("/", "\\")
        self.assertIn(f"-I{worktreeBack}\\Source ", rehomed["command"])
        self.assertTrue(rehomed["command"].endswith(f"-- {worktreeBack}\\Source\\X.cpp"))
        self.assertEqual(f"{worktree.as_posix()}/Source/X.cpp", rehomed["file"])
        for stays in (f"{mainBack}\\Tools\\LLVM", f"-I{mainBack}\\build\\Ninja-Debug\\generated", f"/FI{mainRoot.as_posix()}/build/",
                      f"-I{mainBack}\\build\\vcpkg_installed"):
            self.assertIn(stays, rehomed["command"])
        self.assertIn(f"/external:I{worktree.as_posix()}/ThirdParty/stb", rehomed["command"])
        self.assertEqual(entry["directory"], rehomed["directory"])

    def testToolsSourceAndFoldersNotInAnyListMove(self) -> None:
        # 옮길 폴더를 이름 목록으로 정하지 않는다 — `Tools/OnlineLoadBot`(Tools 안에 링크가 섞인 폴더)과 새로 생긴 `Novel` 도 워크트리에 있으면 옮겨진다.
        mainRoot, worktree = self.mainRoot, self.worktree
        entry = {"directory": str(mainRoot / "build"), "file": f"{mainRoot.as_posix()}/Tools/OnlineLoadBot/Main.cpp",
                 "command": f"cc -I{mainRoot.as_posix()}/Tools -I{mainRoot.as_posix()}/Novel/Sub -I{mainRoot.as_posix()}/Tools/LLVM/bin -I{mainRoot.as_posix()}/Gone -c x.cpp"}
        rehomed = rehomeDatabase([entry], mainRoot, worktree)[0]
        self.assertEqual(f"{worktree.as_posix()}/Tools/OnlineLoadBot/Main.cpp", rehomed["file"])
        self.assertEqual(f"cc -I{worktree.as_posix()}/Tools -I{worktree.as_posix()}/Novel/Sub -I{mainRoot.as_posix()}/Tools/LLVM/bin "
                         f"-I{mainRoot.as_posix()}/Gone -c x.cpp", rehomed["command"])   # 링크 아래 · 워크트리에 없는 경로는 메인 그대로

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
