#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
복합 문자 셰이핑 테스트 글꼴 — 커밋한 `Test/EngineTest/Text/Fonts/swshapingtest.ttf` 가 생성기(`Scripts/common/ShapingTestFont.py`)의 출력과 같은 바이트이고,
표 디렉터리와 head 의 검사합이 맞으며, 필요한 표(GSUB · cmap · glyf …)와 글리프 수가 있는지 본다. 글꼴을 고치려면 생성기를 고치고 `GenerateShapingTestFont.py` 를 다시 돌린다.
"""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))

from common.ShapingTestFont import buildShapingTestFont, kListAdvance  # noqa: E402

kFontPath = kRepositoryRoot / "Test/EngineTest/Text/Fonts/swshapingtest.ttf"


def readTableDirectoryInternal(data: bytes) -> dict[str, tuple[int, int, int]]:
    """표 이름 → (검사합, 오프셋, 길이)."""
    tableCount = struct.unpack(">H", data[4:6])[0]
    mapTable = {}
    for index in range(tableCount):
        tag, checksum, offset, length = struct.unpack(">4sIII", data[12 + 16 * index:28 + 16 * index])
        mapTable[tag.decode("ascii")] = (checksum, offset, length)
    return mapTable


class ShapingTestFontTest(unittest.TestCase):
    def testCommittedFontMatchesGenerator(self) -> None:
        self.assertEqual(buildShapingTestFont(), kFontPath.read_bytes(), "swshapingtest.ttf 이 생성기와 다르다 — GenerateShapingTestFont.py 를 돌린다")

    def testTablesAndChecksums(self) -> None:
        data = buildShapingTestFont()
        mapTable = readTableDirectoryInternal(data)
        for tag in ("GSUB", "cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp", "name", "post"):
            self.assertIn(tag, mapTable)
        for tag, (checksum, offset, length) in mapTable.items():
            if tag == "head":
                continue        # head 의 검사합은 checkSumAdjustment 칸을 0 으로 두고 구한다
            padded = data[offset:offset + length] + b"\x00" * (-length % 4)
            self.assertEqual(checksum, sum(struct.unpack(">%dI" % (len(padded) // 4), padded)) & 0xFFFFFFFF, tag)
        total = sum(struct.unpack(">%dI" % (len(data) // 4), data)) & 0xFFFFFFFF
        self.assertEqual(0xB1B0AFBA, total)         # 파일 전체의 합은 늘 이 값이다(head 의 보정값 덕분)

    def testGlyphCountMatchesMetrics(self) -> None:
        data = buildShapingTestFont()
        mapTable = readTableDirectoryInternal(data)
        glyphCount = struct.unpack(">H", data[mapTable["maxp"][1] + 4:mapTable["maxp"][1] + 6])[0]
        self.assertEqual(len(kListAdvance), glyphCount)
        self.assertEqual(4 * glyphCount, mapTable["hmtx"][2])


if __name__ == "__main__":
    unittest.main()
