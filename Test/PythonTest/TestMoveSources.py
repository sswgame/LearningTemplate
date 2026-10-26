#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev/MoveSources 의 판단 — 임시 git 저장소에서 경로 치환 · 형제 맨이름 include · 대소문자만 다른 이동 · 새 경로 충돌 · 글롭 파일 수 감소를 본다."""

from __future__ import annotations

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from common.ExitCode import ExitCode  # noqa: E402
from dev import MoveSources  # noqa: E402


class MoveSourcesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tempDir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempDir.cleanup)
        self.root = Path(self.tempDir.name)
        self.gitInternal("init", "-q")

    def gitInternal(self, *arguments: str) -> None:
        subprocess.run(["git", *arguments], cwd=self.root, check=True, capture_output=True)

    def writeInternal(self, rel: str, text: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="")

    def readInternal(self, rel: str) -> str:
        return (self.root / rel).read_text(encoding="utf-8")

    def moveInternal(self, mapText: str, *extraArgs: str) -> tuple[int, str, str]:
        self.gitInternal("add", "-A")
        mapPath = self.root.parent / (self.root.name + ".moves.toml")
        mapPath.write_text("[move]\n" + mapText, encoding="utf-8")
        self.addCleanup(lambda: mapPath.unlink(missing_ok=True))
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = MoveSources.main(["--map", str(mapPath), "--root", str(self.root), *extraArgs])
        return code, out.getvalue(), err.getvalue()

    def testSiblingBareIncludeBecomesFullPath(self) -> None:
        self.writeInternal("Source/Editor/Foo/A.h", '#pragma once\n#include "B.h"\n')
        self.writeInternal("Source/Editor/Foo/B.h", "#pragma once\n")
        self.writeInternal("Source/Editor/Foo/A.cpp", '#include "A.h"\n#include "B.h"\n')
        code, out, err = self.moveInternal('"Editor/Foo/A.h" = "Editor/Bar/A.h"\n')
        self.assertEqual(code, 0, err)
        self.assertTrue((self.root / "Source/Editor/Bar/A.h").is_file())
        self.assertIn('#include "Editor/Foo/B.h"', self.readInternal("Source/Editor/Bar/A.h"))
        cpp = self.readInternal("Source/Editor/Foo/A.cpp")
        self.assertIn('"Editor/Bar/A.h"', cpp)
        self.assertIn('"Editor/Foo/B.h"', cpp)   # include 순서 픽서가 맨이름을 전체 경로로 · pch 를 더한다(이동 도구가 부른 결과)
        self.assertIn("touched Source/Editor/Foo/A.cpp", out)

    def testFolderMoveRewritesTomlPoJsonAndKeepsSiblingBareNames(self) -> None:
        self.writeInternal("Source/Editor/Foo/A.h", "#pragma once\n")
        self.writeInternal("Source/Editor/Foo/A.cpp", '#include "A.h"\n')
        self.writeInternal("Source/Editor/Other.cpp", '#include "Editor/Foo/A.h"\n')
        self.writeInternal("Config/Rules.toml", 'path = "Source/Editor/Foo/A.cpp"\n')
        self.writeInternal("Resource/Text/ko.po", "#: Source/Editor/Foo/A.cpp:12\nmsgid \"x\"\n")
        self.writeInternal("Resource/List.json", '{"header": "Editor/Foo/A.h"}\n')
        code, _, err = self.moveInternal('"Editor/Foo" = "Editor/Baz/Foo"\n')
        self.assertEqual(code, 0, err)
        self.assertIn('"Editor/Baz/Foo/A.h"', self.readInternal("Source/Editor/Other.cpp"))
        self.assertIn("Source/Editor/Baz/Foo/A.cpp", self.readInternal("Config/Rules.toml"))
        self.assertIn("Source/Editor/Baz/Foo/A.cpp:12", self.readInternal("Resource/Text/ko.po"))
        self.assertIn('"Editor/Baz/Foo/A.h"', self.readInternal("Resource/List.json"))
        self.assertIn('#include "A.h"', self.readInternal("Source/Editor/Baz/Foo/A.cpp"))   # 함께 옮겨진 형제는 맨이름 그대로
        self.assertFalse((self.root / "Source/Editor/Foo").exists())

    def testCaseOnlyMove(self) -> None:
        self.writeInternal("Source/Audio/mixer/M.h", "#pragma once\n")
        self.writeInternal("Source/Audio/User.cpp", '#include "Audio/mixer/M.h"\n')
        code, _, err = self.moveInternal('"Audio/mixer" = "Audio/Mixer"\n')
        self.assertEqual(code, 0, err)
        self.assertIn('"Audio/Mixer/M.h"', self.readInternal("Source/Audio/User.cpp"))
        listName = [path.name for path in (self.root / "Source/Audio").iterdir()]
        self.assertIn("Mixer", listName)
        self.assertNotIn("mixer", listName)

    def testMoveIntoItself(self) -> None:
        self.writeInternal("Source/Editor/Workspace/W.h", "#pragma once\n")
        self.writeInternal("Source/Editor/User.cpp", '#include "Editor/Workspace/W.h"\n')
        code, _, err = self.moveInternal('"Editor/Workspace" = "Editor/Workspace/GUI"\n')
        self.assertEqual(code, 0, err)
        self.assertIn('#include "Editor/Workspace/GUI/W.h"', self.readInternal("Source/Editor/User.cpp"))
        self.assertNotIn('"Editor/Workspace/W.h"', self.readInternal("Source/Editor/User.cpp"))
        self.assertTrue((self.root / "Source/Editor/Workspace/GUI/W.h").is_file())

    def testExistingNewPathStopsBeforeWriting(self) -> None:
        self.writeInternal("Source/A/X.h", "#pragma once\n")
        self.writeInternal("Source/B/Y.h", "#pragma once\n")
        self.writeInternal("Source/User.cpp", '#include "A/X.h"\n')
        code, _, err = self.moveInternal('"A" = "B"\n')
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("새 경로가 이미 있습니다", err)
        self.assertEqual(self.readInternal("Source/User.cpp"), '#include "A/X.h"\n')
        self.assertTrue((self.root / "Source/A/X.h").is_file())

    def testMissingOldPathStops(self) -> None:
        self.writeInternal("Source/A/X.h", "#pragma once\n")
        code, _, err = self.moveInternal('"Nope" = "Else"\n')
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("옛 경로에 파일이 없습니다", err)

    def testPartialMoveShrinksNonRecursiveGlobAndFails(self) -> None:
        self.writeInternal("Source/Editor/CMakeLists.txt", "file(GLOB listSource CONFIGURE_DEPENDS Modifiers/*.cpp)\n")
        self.writeInternal("Source/Editor/Modifiers/A.cpp", "int a;\n")
        self.writeInternal("Source/Editor/Modifiers/B.cpp", "int b;\n")
        code, _, err = self.moveInternal('"Editor/Modifiers/A.cpp" = "Editor/MeshModifiers/A.cpp"\n')
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("글롭", err)
        self.assertTrue((self.root / "Source/Editor/Modifiers/A.cpp").is_file())   # 아무것도 옮기지 않았다

    def testGlobInsideMovedFolderKeepsCount(self) -> None:
        self.writeInternal("Source/Editor/Mod/CMakeLists.txt", "file(GLOB listSource ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)\n")
        self.writeInternal("Source/Editor/Mod/A.cpp", "int a;\n")
        code, _, err = self.moveInternal('"Editor/Mod" = "Editor/Meshes/Mod"\n')
        self.assertEqual(code, 0, err)

    def testUnresolvedVariableGlobNearMovedFolderStopsUntilReviewed(self) -> None:
        self.writeInternal("Source/Editor/CMakeLists.txt", 'set(layerPrefix "Common/")\nfile(GLOB layerSource "${layerPrefix}*.cpp")\n')
        self.writeInternal("Source/Editor/Common/GUI/G.cpp", "int g;\n")
        code, _, err = self.moveInternal('"Editor/Common/GUI" = "Editor/GUI"\n', "--dry-run")
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("변수를 풀지 못해", err)
        code, _, err = self.moveInternal('"Editor/Common/GUI" = "Editor/GUI"\n[reviewed]\n"Source/Editor/CMakeLists.txt" = "층 이름 표로 훑는다"\n', "--dry-run")
        self.assertEqual(code, 0, err)

    def testRelativeFolderNameInCmakeAndTomlStops(self) -> None:
        self.writeInternal("Source/Editor/Common/Workspace/W.cpp", "int w;\n")
        self.writeInternal("Source/Editor/CMakeLists.txt", "set(listLayer Common/Asset Common/Workspace)\n")
        self.writeInternal("Config/Layers.toml", '"Common/Workspace" = 2\n')
        code, _, err = self.moveInternal('"Editor/Common/Workspace" = "Editor/Workspace"\n', "--dry-run")
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("Source/Editor/CMakeLists.txt: Common/Workspace", err)
        self.assertIn("Config/Layers.toml: Common/Workspace", err)

    def testAddSubdirectoryOfMovedFolderStops(self) -> None:
        self.writeInternal("Source/Plug/CMakeLists.txt", "add_subdirectory(Mesh)\n")
        self.writeInternal("Source/Plug/Mesh/M.cpp", "int m;\n")
        code, _, err = self.moveInternal('"Plug/Mesh" = "Plug/Other/Mesh"\n', "--dry-run")
        self.assertEqual(code, ExitCode.Violation)
        self.assertIn("Source/Plug/CMakeLists.txt: Mesh", err)

    def testDryRunWritesNothingButListsTouchedFiles(self) -> None:
        self.writeInternal("Source/A/X.h", "#pragma once\n")
        self.writeInternal("Source/User.cpp", '#include "A/X.h"\n')
        code, out, err = self.moveInternal('"A" = "Z/A"\n', "--dry-run")
        self.assertEqual(code, 0, err)
        self.assertIn("touched Source/User.cpp", out)
        self.assertEqual(self.readInternal("Source/User.cpp"), '#include "A/X.h"\n')
        self.assertTrue((self.root / "Source/A/X.h").is_file())


if __name__ == "__main__":
    unittest.main()
