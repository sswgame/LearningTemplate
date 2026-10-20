#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""데이터 주도 구조 게이트(CheckDataDrivenStructure)의 판단 — 실제 규칙 데이터로 조각을 읽고, 기준선 비교는 작은 표로."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from lint.gate import CheckDataDrivenStructure  # noqa: E402

kRuleSet = CheckDataDrivenStructure.buildRuleSet(
    CheckDataDrivenStructure.CheckDataDrivenStructureGate.readRules("CheckDataDrivenStructure", CheckDataDrivenStructure.kRuleSchema))


def findKindsInternal(relative: str, text: str) -> list[str]:
    return [finding.kind for finding in CheckDataDrivenStructure.findInText(relative, text, kRuleSet)]


class FindInTextTest(unittest.TestCase):
    def testAssetPathLiteral(self) -> None:
        self.assertEqual(findKindsInternal("Source/Engine/A.h", 'string _path{ "engine/shaders/forwardlit.hlsl" };\n'), ["AssetPathLiteral"])

    def testAssetPathInCommentIsIgnored(self) -> None:
        self.assertEqual(findKindsInternal("Source/Engine/A.h", '// 예: "engine/shaders/forwardlit.hlsl"\nint x;\n'), [])

    def testExtensionWithoutRootIsNotAssetPath(self) -> None:
        self.assertEqual(findKindsInternal("Source/Engine/A.h", 'static constexpr const utf8* kExtension = ".material";\n'), [])

    def testSilentFallbackCpp(self) -> None:
        text = ("float32 readFloat( string_view key, float32 fallback );\n"
                "x = getColorOrDefault( name, kRed );\n"
                "SW_LOG_WARNING( \"broken - using defaults\" );\n")
        self.assertEqual(findKindsInternal("Source/Engine/A.cpp", text), ["SilentFallback"] * 3)

    def testFallbackConstantIsNotParameter(self) -> None:
        self.assertEqual(findKindsInternal("Source/Engine/A.cpp", "static constexpr float32 kFallbackRadius = 0.35f;\n"), [])

    def testDiscardedLoadResult(self) -> None:
        self.assertEqual(findKindsInternal("Source/Editor/A.cpp", "(void)_table.loadFromResource();\n"), ["DiscardedLoadResult"])

    def testSwallowedPythonException(self) -> None:
        text = "try:\n    f()\nexcept Exception:\n    pass\n"
        self.assertEqual(findKindsInternal("Scripts/dev/A.py", text), ["SilentFallback"])

    def testReraisedPythonExceptionIsAllowed(self) -> None:
        text = "try:\n    f()\nexcept Exception as exception:\n    print(exception)\n    raise\nx = 1\n"
        self.assertEqual(findKindsInternal("Scripts/dev/A.py", text), [])

    def testCmakeDefaultValue(self) -> None:
        text = "# DEFAULT_VALUE 는 주석\nfunction(sw_probe OUT_VAR DEFAULT_VALUE)\n"
        self.assertEqual(findKindsInternal("cmake/Probe.cmake", text), ["SilentFallback"])

    def testMagicNameIsScopedToGames(self) -> None:
        text = 'auto id = hashed_string( "Jump" );\n'
        self.assertEqual(findKindsInternal("Source/Games/X/A.cpp", text), ["MagicNameLiteral"])
        self.assertEqual(findKindsInternal("Source/Engine/A.cpp", text), [])


class CompareBaselineTest(unittest.TestCase):
    def testOverBaselineFails(self) -> None:
        listViolation, _ = CheckDataDrivenStructure.compareBaseline({"SilentFallback:a.cpp": 2}, {"SilentFallback:a.cpp": 1}, None)
        self.assertEqual(len(listViolation), 1)

    def testNewFileFails(self) -> None:
        listViolation, _ = CheckDataDrivenStructure.compareBaseline({"AssetPathLiteral:b.cpp": 1}, {}, None)
        self.assertEqual(len(listViolation), 1)

    def testLowerAndFixedAreNotes(self) -> None:
        listViolation, listNote = CheckDataDrivenStructure.compareBaseline({"SilentFallback:a.cpp": 1},
                                                                          {"SilentFallback:a.cpp": 3, "SilentFallback:c.cpp": 1}, None)
        self.assertEqual(listViolation, [])
        self.assertEqual(len(listNote), 2)

    def testPartialChecksOnlyGivenFiles(self) -> None:
        listViolation, listNote = CheckDataDrivenStructure.compareBaseline({"SilentFallback:a.cpp": 1},
                                                                          {"SilentFallback:a.cpp": 1, "SilentFallback:c.cpp": 1}, {"a.cpp"})
        self.assertEqual((listViolation, listNote), ([], []))


class RuleDataTest(unittest.TestCase):
    def testBadPatternKeyIsRejected(self) -> None:
        data = {"scanRoots": ("Source",), "reportOnly": (), "language": {"cpp": ".cpp"}, "pattern": {"Bad:cpp": "x"},
                "scope": {}, "bodyEscape": {}, "baseline": {}}
        with self.assertRaises(CheckDataDrivenStructure.GateError):
            CheckDataDrivenStructure.buildRuleSet(data)

    def testAreaGroupsSourceModules(self) -> None:
        self.assertEqual(CheckDataDrivenStructure.getArea("Source/Engine/UI/A.cpp"), "Source/Engine")
        self.assertEqual(CheckDataDrivenStructure.getArea("Scripts/dev/A.py"), "Scripts")


if __name__ == "__main__":
    unittest.main()
