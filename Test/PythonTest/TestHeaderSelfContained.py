#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
헤더 단독 컴파일(`common/HeaderSelfContained.py`)의 컴파일러 없이 시험할 수 있는 부분 — 빌려 온 명령줄에서 무엇을 떼는가, 어느 TU 를
씨앗으로 고르는가, 빌드 폴더가 쓸 수 있는 상태인가 — 와 CTest 등록에서 빠지는 게이트(`ctestSkipReason`).

명령줄 떼기가 PCH 하나를 놓치면 pch 가 미리 넣어 준 이름이 누락을 가려 **검사가 조용히 통과한다**. 그래서 플랫폼마다 CMake 가 쓰는 철자를
그대로 넣어 본다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))

from common.HeaderSelfContained import findHeaderProbeProblem, findSeedEntry, makeSeedIndex, makeSyntaxOnlyCommand  # noqa: E402
from LintCatalog import discoverLintScripts, discoverLintTargets  # noqa: E402

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
            header = root.as_posix() + "/Source/Editor/Common/Gui/EditorThemeUtil.h"
            self.assertEqual("editor", findSeedEntry(header, listSeed)["command"])

    def testLongestSharedFolderWins(self) -> None:
        listSeed = makeSeedIndex([{"file": "D:/r/Source/Engine/Scene/Scene.cpp", "command": "scene"},
                                  {"file": "D:/r/Source/Editor/Common/Gui/EditorThemeUtil.cpp", "command": "editor"}])
        self.assertEqual("editor", findSeedEntry("D:/r/Source/Editor/Common/Widgets/EditorWidgets.h", listSeed)["command"])


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
