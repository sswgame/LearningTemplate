#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`py -3 -m Scripts test --changed` 의 고르기(ListChangedTests) — 빌드 폴더 없이 손으로 만든 그래프로."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts" / "dev"))

import ListChangedTests  # noqa: E402

_kTestExecutable = {"CoreTest", "EngineTest", "EditorTest"}


def makeGraphInternal() -> ListChangedTests.BuildGraph:
    graph = ListChangedTests.BuildGraph()
    graph.mapFileTarget = {
        "source/core/math/mathutil.h": {"Core", "EngineTest"},
        "source/engine/scene/scene.cpp": {"Engine"},
        "source/editor/editorpanel.cpp": {"EditorModule", "EditorTest"},
        "source/games/nilecity/game.cpp": {"SWGame_NileCity"},
    }
    graph.mapDependent = {
        "Core": {"Engine"},
        "Engine": {"CoreTest", "EngineTest", "GameFramework"},
        "GameFramework": {"EngineTest"},
        "EditorModule": {"App"},
    }
    return graph


class ListChangedTestsTest(unittest.TestCase):
    def setUp(self) -> None:
        self._temp = tempfile.TemporaryDirectory()
        self.root = Path(self._temp.name)

    def tearDown(self) -> None:
        self._temp.cleanup()

    def writeInternal(self, relativePath: str, text: str) -> None:
        path = self.root / relativePath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def select(self, listPath: list[str]) -> ListChangedTests.TestSelection:
        return ListChangedTests.selectTests(listPath, self.root, _kTestExecutable, makeGraphInternal())

    def testDocumentsRunOnlyLint(self) -> None:
        selection = self.select(["docs/06_Backlog.md", "Source/Engine/README.md"])
        self.assertFalse(selection.bFull)
        self.assertTrue(selection.isEmpty())

    def testScriptRunsPythonTests(self) -> None:
        selection = self.select(["Scripts/dev/RunTests.py"])
        self.assertTrue(selection.bPython)
        self.assertFalse(selection.bFull)

    def testHeaderReachesEveryDependentExecutable(self) -> None:
        selection = self.select(["Source/Core/Math/MathUtil.h"])
        self.assertFalse(selection.bFull)
        self.assertEqual({"CoreTest", "EngineTest"}, selection.setExecutable)

    def testTargetWithoutTestDependentRunsEverything(self) -> None:
        self.assertTrue(self.select(["Source/Games/NileCity/Game.cpp"]).bFull)

    def testUnknownSourceRunsEverything(self) -> None:
        self.assertTrue(self.select(["Source/Engine/NewFile.cpp"]).bFull)

    def testUnknownFolderRunsEverything(self) -> None:
        self.assertTrue(self.select(["Resource/engine/scene.xml"]).bFull)
        self.assertTrue(self.select(["CMakeLists.txt"]).bFull)
        self.assertTrue(self.select(["Test/TestFramework/TestRunner.cpp"]).bFull)

    def testMissingGraphRunsEverything(self) -> None:
        selection = ListChangedTests.selectTests(["Source/Core/Math/MathUtil.h"], self.root, _kTestExecutable, None)
        self.assertTrue(selection.bFull)

    def testCaseOnlyTestFileRunsItsSuites(self) -> None:
        self.writeInternal("Test/EngineTest/Scene/TestScene.cpp", "SW_TEST_CASE( SceneTest, A )\n{}\nSW_TEST_CASE( SceneLoadTest, B )\n{}\n")
        selection = self.select(["Test/EngineTest/Scene/TestScene.cpp"])
        self.assertEqual({"EngineTest": {"SceneTest", "SceneLoadTest"}}, selection.mapExecutableSuite)
        self.assertEqual(set(), selection.setExecutable)

    def testTestFileThatRegistersRunsTheWholeExecutable(self) -> None:
        self.writeInternal("Test/EngineTest/TestMock.cpp", "REFLECT( MockComponent )\nSW_TEST_CASE( MockTest, A )\n{}\n")
        self.writeInternal("Test/EngineTest/Fixture.cpp", "void helper() {}\n")
        self.writeInternal("Test/EngineTest/Fixture.h", "void helper();\n")
        for path in ("Test/EngineTest/TestMock.cpp", "Test/EngineTest/Fixture.cpp", "Test/EngineTest/Fixture.h"):
            selection = self.select([path])
            self.assertEqual({"EngineTest"}, selection.setExecutable, path)

    def testWholeExecutableSwallowsItsSuites(self) -> None:
        self.writeInternal("Test/EngineTest/TestScene.cpp", "SW_TEST_CASE( SceneTest, A )\n{}\n")
        selection = self.select(["Test/EngineTest/TestScene.cpp", "Source/Engine/Scene/Scene.cpp"])
        self.assertEqual({}, selection.mapExecutableSuite)
        self.assertIn("EngineTest", selection.setExecutable)

    def testDeletedTestFileRunsTheWholeExecutable(self) -> None:
        self.assertEqual({"EngineTest"}, self.select(["Test/EngineTest/TestGone.cpp"]).setExecutable)


if __name__ == "__main__":
    unittest.main()
