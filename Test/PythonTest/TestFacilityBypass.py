#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
`Scripts/lint/gate/CheckFacilityBypass.py`(엔진 기능을 우회한 직접 호출)의 단위 테스트.

게이트의 `selfTestCases` 가 "반드시 잡는 것" 을 보이고, 여기서는 예외가 **규칙마다 따로** 적용되는지(로그 구현 파일이라도 난수 직접 사용은 센다)와
규칙 데이터의 `[baseline]` 키가 규칙 표와 맞는지를 본다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from lint.gate.CheckFacilityBypass import CheckFacilityBypassGate, findBypassCalls  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402


def _makeSource(relative: str, text: str) -> SourceFile:
    return SourceFile(kRepositoryRoot / relative, relative, text)


class FacilityBypassTest(unittest.TestCase):
    def testExemptionAppliesOnlyToItsRule(self) -> None:
        source = _makeSource("Source/Core/Log/Logger.cpp", "void f()\n{\n    std::fprintf( stderr, \"x\" );\n    std::random_device d;\n}\n")
        listFound = findBypassCalls(source)
        self.assertEqual(["random"], [finding.rule for finding in listFound])
        self.assertEqual(4, listFound[0].firstLine)

    def testGlobExemptionCoversFolder(self) -> None:
        source = _makeSource("Test/TestFramework/Probe.cpp", "void f() { std::printf( \"x\" ); }\n")
        self.assertEqual([], findBypassCalls(source))

    def testNamePartsAndMembersAreNotCalls(self) -> None:
        source = _makeSource("Source/Probe/A.cpp", "void f( Host& h, char* p )\n{\n    std::snprintf( p, 2, \"x\" );\n    h.system( 1 );\n    p->printf( 2 );\n    getSystem( 3 );\n    restore( 4 );\n}\n")
        self.assertEqual([], findBypassCalls(source))

    def testBaselineKeysMatchRules(self) -> None:
        mapBaseline = CheckFacilityBypassGate.readRules("CheckFacilityBypass", CheckFacilityBypassGate.ruleSchema)["baseline"]
        self.assertEqual(set(CheckFacilityBypassGate.baselineRules), set(mapBaseline))


if __name__ == "__main__":
    unittest.main()
