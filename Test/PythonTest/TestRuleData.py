#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""RuleData — 규칙 데이터(`Scripts/lint/rules/*.toml`)의 스키마 검사와, 파이썬 3.10 용 부분 집합 읽기가 tomllib 과 같은 답을 내는지."""

from __future__ import annotations

import sys
import unittest
import unittest.mock
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))
sys.path.insert(0, str(kRepositoryRoot / "Scripts" / "lint"))

from common import RuleData  # noqa: E402
from common.RuleData import RuleDataError, kKindInteger, kKindReason, kKindText, kKindTextList, readTomlSubset  # noqa: E402
from LintGate import LintGate  # noqa: E402

_kSample = """# 머리 주석
list = [
    "a", 'b',   # 줄 끝 주석
    "c\\"d",
]
count = -3
flag = true

[exemption]
"Source/A B.cpp" = "이유 — 따옴표 \\"안\\" · 탭\\t"
bare-key_1 = 'literal \\n 그대로'

[tier]
Common = 0   # 주석
Log = 5
"""


class RuleDataSubsetReaderTest(unittest.TestCase):
    """부분 집합 읽기 — 쓰는 문법은 tomllib 과 같은 답, 모르는 문법은 오류."""

    def testSubsetMatchesTomllib(self) -> None:
        try:
            import tomllib
        except ModuleNotFoundError:
            self.skipTest("tomllib 이 없는 파이썬(3.10)")
        self.assertEqual(readTomlSubset(_kSample, "probe"), tomllib.loads(_kSample))

    def testEveryRuleFileReadsTheSameWithBothReaders(self) -> None:
        try:
            import tomllib
        except ModuleNotFoundError:
            self.skipTest("tomllib 이 없는 파이썬(3.10)")
        listPath = RuleData.listRuleFiles()
        self.assertTrue(listPath, "Scripts/lint/rules/ 가 비어 있다")
        for path in listPath:
            text = path.read_text(encoding="utf-8")
            self.assertEqual(readTomlSubset(text, path.name), tomllib.loads(text), path.name)

    def testUnsupportedSyntaxIsAnError(self) -> None:
        for text in ("a.b = 1\n", "x = { y = 1 }\n", "[[t]]\n", 'x = """multi"""\n', "x = 1979-05-27\n", "[t]\n[t]\n", "x = 1\nx = 2\n",
                     'x = "unterminated\n', "x = 1 junk\n"):
            with self.subTest(text=text), self.assertRaises(RuleDataError):
                readTomlSubset(text, "probe")


class RuleDataSchemaTest(unittest.TestCase):
    """스키마 검사 — 모르는 키 · 빈 이유 · 종류가 다른 값 · 빠진 필수 키 · 없는 파일."""

    def readWith(self, text: str, schema: dict[str, str], **kwargs: object) -> dict:
        RuleData.readRawRuleFileInternal.cache_clear()
        with unittest.mock.patch.object(RuleData, "readRawRuleFileInternal", lambda _path: readTomlSubset(text, "probe")), \
                unittest.mock.patch.object(Path, "is_file", lambda _self: True):
            return RuleData.readRuleFile("Probe", schema, **kwargs)

    def testValidDataReadsWithDefaults(self) -> None:
        data = self.readWith("[exemption]\na = \"이유\"\n", {"exemption": kKindReason, "tier": kKindInteger, "list": kKindTextList})
        self.assertEqual(data, {"exemption": {"a": "이유"}, "tier": {}, "list": ()})

    def testErrors(self) -> None:
        listCase = [
            ("[exemptions]\na = \"x\"\n", {"exemption": kKindReason}, {}),             # 모르는 키
            ("[exemption]\na = \"  \"\n", {"exemption": kKindReason}, {}),             # 빈 이유
            ("[tier]\na = \"0\"\n", {"tier": kKindInteger}, {}),                        # 종류가 다른 값
            ("list = [\"a\", \"a\"]\n", {"list": kKindTextList}, {}),                   # 같은 값 두 번
            ("[map]\na = 1\n", {"map": kKindText}, {}),                                 # 글이 아닌 값
            ("", {"list": kKindTextList}, {"requiredKeys": ("list",)}),                 # 빠진 필수 키
        ]
        for text, schema, kwargs in listCase:
            with self.subTest(text=text), self.assertRaises(RuleDataError):
                self.readWith(text, schema, **kwargs)

    def testMissingFile(self) -> None:
        with self.assertRaises(RuleDataError):
            RuleData.readRuleFile("NoSuchRuleFile", {"exemption": kKindReason})
        self.assertEqual(RuleData.readRuleFile("NoSuchRuleFile", {"exemption": kKindReason}, bMissingFileIsEmpty=True), {"exemption": {}})


class LintGateRuleDataTest(unittest.TestCase):
    """기반이 예외 표를 rules/<이름>.toml 에서 채우고, 읽기 오류는 import 가 아니라 main() 의 종료 2 로 알린다."""

    def testGateWithoutRuleFileStartsEmpty(self) -> None:
        class _NoRuleFileGate(LintGate):
            selfTestSkipReason = "시험용"

        self.assertEqual(_NoRuleFileGate.mapExemption, {})
        self.assertEqual(_NoRuleFileGate._ruleDataError, "")

    def testExemptionComesFromRuleFile(self) -> None:
        class CheckCoreLayersGate(LintGate):   # noqa: N801 — 이름이 데이터 파일을 고른다
            selfTestSkipReason = "시험용"
            ruleSchema = {"tier": kKindInteger}

        self.assertIn("Source/Core/Math/VectorMath.cpp", CheckCoreLayersGate.mapExemption)

    def testBrokenRuleFileStopsTheGate(self) -> None:
        with unittest.mock.patch.object(LintGate, "readRules", side_effect=RuleDataError("probe: 깨진 파일")):
            class _BrokenGate(LintGate):
                selfTestSkipReason = "시험용"

        self.assertEqual(_BrokenGate._ruleDataError, "probe: 깨진 파일")
        self.assertEqual(_BrokenGate().main(["--root", str(kRepositoryRoot)]), 2)


if __name__ == "__main__":
    unittest.main()
