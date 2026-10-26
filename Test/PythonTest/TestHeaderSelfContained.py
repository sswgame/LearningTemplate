#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
헤더 단독 컴파일(`common/HeaderSelfContained.py`)의 컴파일러 없이 테스트할 수 있는 부분 — 빌려 온 명령줄에서 무엇을 떼는가, 어느 TU 를
씨앗으로 고르는가, 빌드 폴더가 쓸 수 있는 상태인가 — 와 CTest 등록에서 빠지는 게이트(`ctestSkipReason`).

명령줄 떼기가 PCH 하나를 놓치면 pch 가 미리 넣어 준 이름이 누락을 가려 **검사가 조용히 통과한다**. 그래서 플랫폼마다 CMake 가 쓰는 철자를
그대로 넣어 본다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Sequence
from unittest import mock

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common import HeaderSelfContained  # noqa: E402
from common.HeaderSelfContained import (findHeaderProbeProblem, findHeadersNotSelfContained, findSeedEntry, makeSeedIndex,  # noqa: E402
                                        makeSyntaxOnlyCommand, selectProbeBuildDir)
from lint.LintCatalog import discoverLintScripts, discoverLintTargets  # noqa: E402

kProbe = Path("probe.cpp")


class SyntaxOnlyCommandTest(unittest.TestCase):
    def testClangClPchFlagsAreRemoved(self) -> None:
        command = ("clang-cl.exe /nologo -TP -DSW_X64 -ID:/r/Source /YuD:/b/cmake_pch.hxx /FpD:/b/cmake_pch.cxx.pch "
                   "/FID:/b/Engine.dir/cmake_pch.hxx /FoSource\\x.obj /FdSource\\ -c -- D:\\r\\Source\\Engine\\X.cpp")
        listToken = makeSyntaxOnlyCommand({"command": command}, kProbe)
        self.assertFalse([token for token in listToken if "cmake_pch" in token or token.startswith(("/Fo", "/Fd"))])
        self.assertNotIn("D:\\r\\Source\\Engine\\X.cpp", listToken)
        self.assertEqual(["-fsyntax-only", "-Wno-unused-command-line-argument", str(kProbe)], listToken[-3:])
        self.assertIn("-ID:/r/Source", listToken)

    def testPchCreatingFlagIsRemoved(self) -> None:
        listToken = makeSyntaxOnlyCommand({"command": "clang-cl.exe /YcD:/b/cmake_pch.hxx -c -- D:/b/cmake_pch.cxx"}, kProbe)
        self.assertFalse([token for token in listToken if token.startswith("/Yc")])

    def testClangPchFlagsAreRemoved(self) -> None:
        command = ("/usr/bin/clang++ -DSW_X64 -I/r/Source -Xclang -include-pch -Xclang /b/cmake_pch.hxx.pch "
                   "-Xclang -include -Xclang /b/cmake_pch.hxx -MD -MT obj/X.o -MF obj/X.o.d -o obj/X.o -c /r/Source/Engine/X.cpp")
        listToken = makeSyntaxOnlyCommand({"command": command}, kProbe)
        self.assertEqual(["/usr/bin/clang++", "-DSW_X64", "-I/r/Source", "-fsyntax-only", "-Wno-unused-command-line-argument",
                          str(kProbe)], listToken)

    def testGccStylePchIncludeIsRemovedButOtherIncludeIsKept(self) -> None:
        command = "clang++ -include /b/cmake_pch.hxx -include /r/Source/Core/Forced.h -c /r/X.cpp"
        listToken = makeSyntaxOnlyCommand({"command": command}, kProbe)
        self.assertNotIn("/b/cmake_pch.hxx", listToken)
        self.assertIn("/r/Source/Core/Forced.h", listToken)

    def testArgumentsListFormIsRead(self) -> None:
        listToken = makeSyntaxOnlyCommand({"arguments": ["clang++", "-I/r/Source", "-c", "/r/X.cpp"]}, kProbe)
        self.assertEqual(["clang++", "-I/r/Source"], listToken[:2])


class SeedIndexTest(unittest.TestCase):
    def testPchDummyTranslationUnitIsNotASeed(self) -> None:
        database = [{"file": "D:/r/build/Source/Engine/CMakeFiles/Engine.dir/cmake_pch.cxx", "command": "pch"},
                    {"file": "D:\\r\\Source\\Engine\\Config\\EngineConfig.cpp", "command": "config"}]
        listSeed = makeSeedIndex(database)
        self.assertEqual(["D:/r/Source/Engine/Config/"], [seedDir for seedDir, _ in listSeed])

    def testUnityTranslationUnitIsMappedBackToItsSourceFolder(self) -> None:
        with tempfile.TemporaryDirectory() as rootName:
            root = Path(rootName).resolve()
            buildDir = root / "build" / "CI-Debug"
            unityRoot = buildDir.as_posix() + "/Source/{0}/CMakeFiles/{0}.dir/Unity/unity_0_cxx.cxx"
            listSeed = makeSeedIndex([{"file": unityRoot.format("Core"), "command": "core"},
                                      {"file": unityRoot.format("Editor"), "command": "editor"}], buildDir, root)
            header = root.as_posix() + "/Source/Editor/Common/GUI/EditorThemeUtil.h"
            self.assertEqual("editor", findSeedEntry(header, listSeed)["command"])

    def testLongestSharedFolderWins(self) -> None:
        listSeed = makeSeedIndex([{"file": "D:/r/Source/Engine/Scene/Scene.cpp", "command": "scene"},
                                  {"file": "D:/r/Source/Editor/Common/GUI/EditorThemeUtil.cpp", "command": "editor"}])
        self.assertEqual("editor", findSeedEntry("D:/r/Source/Editor/Common/Widgets/EditorWidgets.h", listSeed)["command"])

    def testAncestorFolderBeatsSiblingSubfolderOnTie(self) -> None:
        listSeed = makeSeedIndex([{"file": "D:/r/Source/Editor/Common/Backend/Queue.cpp", "command": "low"},
                                  {"file": "D:/r/Source/Editor/ImGuiEditor.cpp", "command": "root"}])
        self.assertEqual("root", findSeedEntry("D:/r/Source/Editor/IEditor.h", listSeed)["command"])


class HeaderProbeProblemTest(unittest.TestCase):
    def testMissingDatabaseIsAProblem(self) -> None:
        with tempfile.TemporaryDirectory() as buildDirName:
            self.assertIn("compile_commands.json", findHeaderProbeProblem(Path(buildDirName)))

    def testPlaceholderFlagOpsIsAProblem(self) -> None:
        with tempfile.TemporaryDirectory() as buildDirName:
            buildDir = Path(buildDirName)
            (buildDir / "compile_commands.json").write_text("[]", encoding="utf-8")
            (buildDir / "generated" / "Engine").mkdir(parents=True)
            (buildDir / "generated" / "Engine" / "FlagOps.gen.h").write_text(
                "// AUTO-GENERATED placeholder\n#pragma once\n", encoding="utf-8")
            self.assertIn("FlagOps.gen.h", findHeaderProbeProblem(buildDir))

    def testBuiltFolderHasNoProblem(self) -> None:
        with tempfile.TemporaryDirectory() as buildDirName:
            buildDir = Path(buildDirName)
            (buildDir / "compile_commands.json").write_text("[]", encoding="utf-8")
            (buildDir / "generated" / "Engine").mkdir(parents=True)
            (buildDir / "generated" / "Engine" / "FlagOps.gen.h").write_text("#pragma once\n// generated\n", encoding="utf-8")
            self.assertEqual("", findHeaderProbeProblem(buildDir))


def writeBuiltFolderInternal(buildDir: Path) -> None:
    buildDir.mkdir(parents=True, exist_ok=True)
    (buildDir / "compile_commands.json").write_text("[]", encoding="utf-8")
    (buildDir / "generated" / "Engine").mkdir(parents=True)
    (buildDir / "generated" / "Engine" / "FlagOps.gen.h").write_text("#pragma once\n// generated\n", encoding="utf-8")


class SelectProbeBuildDirTest(unittest.TestCase):
    def testWorktreeWithoutBuildFallsBackToMainBuild(self) -> None:
        with tempfile.TemporaryDirectory() as rootName:
            mainBuild = Path(rootName) / "main" / "build"
            writeBuiltFolderInternal(mainBuild)
            self.assertEqual((mainBuild, ""), selectProbeBuildDir([Path(rootName) / "wt" / "build", mainBuild]))

    def testOwnBuildWinsOverMain(self) -> None:
        with tempfile.TemporaryDirectory() as rootName:
            listBuild = [Path(rootName) / "wt", Path(rootName) / "main"]
            for build in listBuild:
                writeBuiltFolderInternal(build)
            self.assertEqual((listBuild[0], ""), selectProbeBuildDir(listBuild))

    def testNowhereReportsTheFirstCandidateAndItsProblem(self) -> None:
        with tempfile.TemporaryDirectory() as rootName:
            listBuild = [Path(rootName) / "wt", Path(rootName) / "main"]
            buildDir, problem = selectProbeBuildDir(listBuild)
            self.assertEqual(listBuild[0], buildDir)
            self.assertIn("compile_commands.json", problem)


class WorktreeRehomeTest(unittest.TestCase):
    def testHeaderIsProbedWithFlagsMovedToTheWorktree(self) -> None:
        with tempfile.TemporaryDirectory() as rootName:
            root = Path(rootName).resolve()
            mainRoot, worktree = root / "main", root / "wt"
            buildDir = mainRoot / "build"
            buildDir.mkdir(parents=True)
            (worktree / "Source" / "Engine").mkdir(parents=True)
            header = worktree / "Source" / "Engine" / "A.h"
            header.write_text("#pragma once\n", encoding="utf-8")
            mainSource = (mainRoot / "Source" / "Engine" / "A.cpp").as_posix()
            (buildDir / "compile_commands.json").write_text(json.dumps([{
                "directory": buildDir.as_posix(), "file": mainSource,
                "command": f"clang++ -I{(mainRoot / 'Source').as_posix()} -c {mainSource}"}]), encoding="utf-8")
            listSeenCommand: list[str] = []

            def fakeCompile(entry: dict, probeSource: Path, probeBuildDir: Path, listExtraFlag: Sequence[str] = ()) -> tuple[bool, str]:
                listSeenCommand.append(entry["command"].replace("\\", "/").lower())
                return False, "A.h:1:1: error: unknown type name 'Foo'"

            with tempfile.TemporaryDirectory() as probeDirName, mock.patch.object(HeaderSelfContained, "compileSyntaxOnly", fakeCompile):
                listFailure = findHeadersNotSelfContained(worktree, buildDir, [header], Path(probeDirName), workerCount=1, mainRoot=mainRoot)
            self.assertEqual([("Engine/A.h", "unknown type name 'Foo'")], listFailure)
            self.assertEqual(1, len(listSeenCommand))
            self.assertIn((worktree / "Source").as_posix().lower(), listSeenCommand[0])
            self.assertNotIn((mainRoot / "Source").as_posix().lower(), listSeenCommand[0])


class LintCatalogCtestSkipTest(unittest.TestCase):
    def testGateWithCtestSkipReasonIsNotRegistered(self) -> None:
        listSkipped = [script.name for script in discoverLintScripts("gate")
                       if script.gateClass is not None and script.gateClass.ctestSkipReason]
        self.assertIn("CheckHeaderSelfContained", listSkipped)
        listRegistered = {target.name for target in discoverLintTargets()}
        self.assertFalse(set(listSkipped) & listRegistered)
        self.assertIn("CheckCodeConventions", listRegistered)


if __name__ == "__main__":
    unittest.main()
