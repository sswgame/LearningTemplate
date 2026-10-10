#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""약어 철자 도구 — 등록부의 철자 판정, 코드모드가 건드리지 않는 자리, 파일 · 데이터 적용(임시 git 저장소), 게이트의 강제, 전후 컴파일."""

from __future__ import annotations

import shutil
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint" / "fixer"))

import AcronymRegistry as registry  # noqa: E402
import FormatAcronymSpelling as codemod  # noqa: E402
from common import runProcess  # noqa: E402

_kHeader = """#pragma once
namespace sw
{
    class UiThing
    {
    public:
        int getOwnerId() const { return _ownerId; }
        void updateUi() {}

    private:
        int _ownerId = 0;
    };
}
"""

_kSource = """#include "UI/UiThing.h"

int gv_uiScale = 1;
#define SW_UI_ENABLED 1

int useThing()
{
    sw::UiThing thing;   // UiThing 은 주석이라 그대로
    thing.updateUi();
    const char* kTypeName = "UiThing";
    (void)kTypeName;
    return thing.getOwnerId() + gv_uiScale + SW_UI_ENABLED;
}
"""


def findClangInternal() -> Path | None:
    for candidate in (kRepositoryRoot / "Tools" / "LLVM" / "bin" / "clang++.exe", kRepositoryRoot / "Tools" / "LLVM" / "bin" / "clang++"):
        if candidate.is_file():
            return candidate
    found = shutil.which("clang++")
    return Path(found) if found else None


def runGitInternal(root: Path, *arguments: str) -> str:
    result = runProcess(["git", *arguments], cwd=root)
    if result.returnCode != 0:
        raise AssertionError(f"git {arguments}: {result.stderr}")
    return result.stdout


class AcronymRegistryTest(unittest.TestCase):
    def testRespellName(self) -> None:
        cases = {
            "UiSystem": "UISystem", "updateUi": "updateUI", "pUiSystem": "pUISystem", "_pGpuScene": "_pGPUScene",
            "entityIds": "entityIDs", "getOwnerId": "getOwnerID", "EUiMode": "EUIMode", "kUiScale": "kUIScale",
            "ClassicJrpg": "ClassicJRPG", "OpenSslContext": "OpenSSLContext", "TacticsSrpg": "TacticsSRPG", "SqliteStore": "SQLiteStore",
            # 그대로인 것
            "uiSystem": "uiSystem", "_gpuScene": "_gpuScene", "RHIDevice": "RHIDevice", "TagID": "TagID", "ImGuiLayer": "ImGuiLayer",
            "Idle": "Idle", "Guid": "Guid", "Idx": "Idx", "Aim": "Aim", "Ios": "Ios", "gv_uiScale": "gv_uiScale", "SW_UI": "SW_UI",
        }
        for name, expected in cases.items():
            self.assertEqual(registry.respellName(name), expected, name)

    def testOneAcronymAtATime(self) -> None:
        self.assertEqual(registry.respellName("UiGpuPass", ("UI",)), "UIGpuPass")
        self.assertEqual(registry.resolveAcronyms(["Ui,gpu"]), ("UI", "GPU"))
        with self.assertRaises(ValueError):
            registry.resolveAcronyms(["Nav"])

    def testAdjacentAcronyms(self) -> None:
        self.assertEqual(registry.findAdjacentAcronyms("RHIUIPass"), ["RHIUI"])
        self.assertEqual(registry.findAdjacentAcronyms("GPUIDs"), ["GPUID"])
        self.assertEqual(registry.findAdjacentAcronyms("AABBTree"), [])
        self.assertEqual(registry.findAdjacentAcronyms("UUIDText"), [])

    def testRegistryIsConsistent(self) -> None:
        self.assertEqual(registry.checkRegistry(), [])

    def testCodeLeavesLiteralsCommentsIncludesAndExternalNames(self) -> None:
        text = ('#include "UI/UiThing.h"\n'
                'R"(UiRaw)"; auto s = "UiThing"; // UiThing\n'
                "ImGui::GetIo(); ed::NodeId node; GetCurrentProcessId(); VkPhysicalDeviceIdProperties p;\n"
                "UiThing thing; thing.updateUi();\n")
        newText, mapRename = registry.respellCode(text)
        self.assertEqual(mapRename, {"UiThing": "UIThing", "updateUi": "updateUI"})
        self.assertIn('#include "UI/UiThing.h"', newText)
        self.assertIn('"UiThing"; // UiThing', newText)
        self.assertIn('R"(UiRaw)"', newText)
        self.assertIn("ImGui::GetIo(); ed::NodeId node; GetCurrentProcessId(); VkPhysicalDeviceIdProperties p;", newText)
        self.assertIn("UIThing thing; thing.updateUI();", newText)

    def testFixPassSamples(self) -> None:
        for fixPass in codemod.FormatAcronymSpellingFixer.listPass:
            self.assertTrue(fixPass.apply(fixPass.badSample, "")[1])
            self.assertFalse(fixPass.apply(fixPass.goodSample, "")[1])


class AcronymApplyTest(unittest.TestCase):
    """작은 저장소 하나에서 `--apply-files` 와 식별자 고치기를 차례로 — 파일 이름 · include · CMake · 데이터가 같이 움직이는지, 컴파일이 그대로 서는지."""

    def setUp(self) -> None:
        self.root = Path(tempfile.mkdtemp(prefix="acronymApply"))
        files = {
            "Source/UI/UiThing.h": _kHeader,
            "Source/UI/UseThing.cpp": _kSource,
            "Source/UI/CMakeLists.txt": "target_sources(Probe PRIVATE UiThing.h UseThing.cpp)\n",
            "Resource/game/probe/scene.xml": '<entity type="UiThing" _ownerId="3" note="Uid"/>\n',
            "Config/Game/Probe.json": '{ "_ownerId": 1, "label": "Idle" }\n',
        }
        for relPath, content in files.items():
            path = self.root / relPath
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8", newline="\n")
        runGitInternal(self.root, "init", "-q")
        runGitInternal(self.root, "-c", "user.name=t", "-c", "user.email=t@t", "add", "-A")
        runGitInternal(self.root, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "init")

    def tearDown(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)

    def compileInternal(self, clang: Path) -> None:
        result = runProcess([clang, "-fsyntax-only", "-std=c++20", "-I", self.root / "Source", self.root / "Source" / "UI" / "UseThing.cpp"])
        self.assertEqual(result.returnCode, 0, result.stderr)

    def testDryRunChangesNothing(self) -> None:
        listLine = codemod.applyFiles(self.root, registry.kSelectable, bDryRun=True)
        self.assertTrue(any("UiThing.h" in line for line in listLine), listLine)
        self.assertEqual(runGitInternal(self.root, "status", "--porcelain"), "")

    def testApplyFilesThenIdentifiers(self) -> None:
        clang = findClangInternal()
        if clang is not None:
            self.compileInternal(clang)

        codemod.applyFiles(self.root, registry.kSelectable, bDryRun=False)
        listTracked = runGitInternal(self.root, "ls-files").split()
        self.assertIn("Source/UI/UIThing.h", listTracked)   # 대소문자만 바뀐 이름 — git 에 새 철자로
        self.assertNotIn("Source/UI/UiThing.h", listTracked)
        self.assertIn('#include "UI/UIThing.h"', (self.root / "Source/UI/UseThing.cpp").read_text(encoding="utf-8"))
        self.assertIn("UIThing.h UseThing.cpp", (self.root / "Source/UI/CMakeLists.txt").read_text(encoding="utf-8"))
        scene = (self.root / "Resource/game/probe/scene.xml").read_text(encoding="utf-8")
        self.assertIn('type="UIThing" _ownerID="3" note="Uid"', scene)
        self.assertIn('"_ownerID": 1, "label": "Idle"', (self.root / "Config/Game/Probe.json").read_text(encoding="utf-8"))

        for relPath in ("Source/UI/UIThing.h", "Source/UI/UseThing.cpp"):
            path = self.root / relPath
            newText, _ = registry.respellCode(path.read_text(encoding="utf-8"))
            path.write_text(newText, encoding="utf-8", newline="\n")
        source = (self.root / "Source/UI/UseThing.cpp").read_text(encoding="utf-8")
        self.assertIn("sw::UIThing thing;   // UiThing 은 주석이라 그대로", source)
        self.assertIn('"UiThing"', source)
        self.assertIn("gv_uiScale", source)
        self.assertIn("thing.getOwnerID()", source)
        if clang is not None:
            self.compileInternal(clang)

    def testDataRenameWorksAfterCodeIsRenamed(self) -> None:
        for relPath in ("Source/UI/UiThing.h", "Source/UI/UseThing.cpp"):
            path = self.root / relPath
            path.write_text(registry.respellCode(path.read_text(encoding="utf-8"))[0], encoding="utf-8", newline="\n")
        codemod.applyFiles(self.root, registry.kSelectable, bDryRun=False)
        self.assertIn('type="UIThing" _ownerID="3"', (self.root / "Resource/game/probe/scene.xml").read_text(encoding="utf-8"))


class AcronymTextTest(unittest.TestCase):
    """`--apply-text` — 코드가 바뀐 뒤 주석 · 문서는 고치고, 문자열은 묻고, 코드에 없는 철자(남의 이름)는 남긴다."""

    def testCommentsAndProseFollowTheCode(self) -> None:
        mapRename: dict[str, str] = {}
        setIdentifier = {"UIThing", "updateUI"}
        code = '#include "UI/UiThing.h"\n// UiThing 을 쓴다\nconst char* k = "UiThing";\nint UIThingX = 0;\n'
        newCode, listEdit = codemod.rewriteText("a.cpp", code, mapRename, setIdentifier, registry.kSelectable, bCode=True, bStrings=False)
        self.assertIn('#include "UI/UiThing.h"', newCode)
        self.assertIn("// UIThing 을 쓴다", newCode)
        self.assertIn('"UiThing"', newCode)
        self.assertEqual([edit.kind for edit in listEdit], ["comment", "string"])
        newCode, _ = codemod.rewriteText("a.cpp", code, mapRename, setIdentifier, registry.kSelectable, bCode=True, bStrings=True)
        self.assertIn('"UIThing"', newCode)

        prose = "`UiThing` 과 `updateUi` 는 바뀌고, 언리얼의 `FJsonObject` 는 남는다.\n"
        newProse, listEdit = codemod.rewriteText("a.md", prose, mapRename, setIdentifier, registry.kSelectable, bCode=False, bStrings=False)
        self.assertEqual(newProse, "`UIThing` 과 `updateUI` 는 바뀌고, 언리얼의 `FJsonObject` 는 남는다.\n")
        self.assertEqual([edit.old for edit in listEdit if edit.kind == "leftover"], ["FJsonObject"])


class AcronymGateTest(unittest.TestCase):
    def runGateInternal(self, root: Path, *arguments: str) -> int:
        result = runProcess([sys.executable, kRepositoryRoot / "Scripts" / "lint" / "gate" / "CheckAcronymSpelling.py", "--root", root, *arguments])
        return result.returnCode

    def testReportsOnlyUntilEnforced(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Source" / "Probe").mkdir(parents=True)
            (root / "Source" / "Probe" / "Probe.h").write_text("class CpuProbe {};\nclass GPUScene {};\n", encoding="utf-8")
            self.assertEqual(self.runGateInternal(root), 0)
            self.assertEqual(self.runGateInternal(root, "--enforce", "GPU"), 0)
            self.assertEqual(self.runGateInternal(root, "--enforce", "CPU"), 1)


if __name__ == "__main__":
    unittest.main()
