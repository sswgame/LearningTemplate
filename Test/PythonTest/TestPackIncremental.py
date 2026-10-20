#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
팩 증분 쿠킹(`CookAssets.PackWriter.writeTo` 와 옆의 `<팩>.packrec`)의 단위 테스트.

항목 · 설정이 같으면 다시 쓰지 않고, 내용 · 경로 · 설정이 바뀌거나 기록이 없거나 낡았거나 팩 바이트가 기록과 다르면 다시 쓴다.
다시 쓴 팩은 기록 없이 처음 쓴 팩과 바이트가 같아야 한다(증분이 다른 결과를 내지 않는다).
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

kRepositoryRoot = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(kRepositoryRoot / "Scripts"))


from generate import CookAssets  # noqa: E402


class PackIncrementalTest(unittest.TestCase):
    def setUp(self) -> None:
        self._folder = tempfile.TemporaryDirectory()
        self.root = Path(self._folder.name)
        self.sourceDir = self.root / "source"
        (self.sourceDir / "sub").mkdir(parents=True)
        (self.sourceDir / "a.txt").write_bytes(b"alpha" * 100)
        (self.sourceDir / "sub" / "b.bin").write_bytes(bytes(range(256)))
        self.packPath = self.root / "out" / "test.pack"

    def tearDown(self) -> None:
        self._folder.cleanup()

    def writePack(self, packPath: Path | None = None, compressionLevel: int = 6) -> object:
        writer = CookAssets.PackWriter(CookAssets._gPackFormat, compression=CookAssets._gPackFormat.codecZlib, compressionLevel=compressionLevel)
        for filePath in sorted(self.sourceDir.rglob("*")):
            if filePath.is_file():
                writer.addFile(filePath.relative_to(self.sourceDir).as_posix(), filePath)
        writer.addBytes("generated/registry.json", b"{}")
        return writer.writeTo(packPath or self.packPath)

    def testFirstWriteThenUpToDate(self) -> None:
        first = self.writePack()
        self.assertTrue(first.bWritten)
        self.assertEqual(first.reason, "기록이 없습니다")
        recordPath = CookAssets.PackRecord.pathFor(self.packPath)
        self.assertTrue(recordPath.is_file())
        before = self.packPath.stat().st_mtime_ns

        second = self.writePack()
        self.assertFalse(second.bWritten)
        self.assertEqual(second.packSize, first.packSize)
        self.assertEqual(self.packPath.stat().st_mtime_ns, before)

    def testContentChangeRewritesAndMatchesAFreshPack(self) -> None:
        self.writePack()
        (self.sourceDir / "a.txt").write_bytes(b"changed")
        result = self.writePack()
        self.assertTrue(result.bWritten)
        self.assertEqual(result.reason, "항목 · 설정이 바뀌었습니다")
        # 증분으로 다시 쓴 팩과 기록 없이 처음 쓴 팩의 바이트가 같다.
        freshPath = self.root / "fresh" / "test.pack"
        self.writePack(freshPath)
        self.assertEqual(self.packPath.read_bytes(), freshPath.read_bytes())

    def testSettingChangeRewrites(self) -> None:
        self.writePack()
        result = self.writePack(compressionLevel=9)
        self.assertTrue(result.bWritten)
        self.assertEqual(result.reason, "항목 · 설정이 바뀌었습니다")

    def testOldRecordFormatIsRewrittenNotSkipped(self) -> None:
        self.writePack()
        recordPath = CookAssets.PackRecord.pathFor(self.packPath)
        data = json.loads(recordPath.read_text(encoding="utf-8"))
        data["record_format"] = CookAssets.kPackRecordFormatVersion - 1
        recordPath.write_text(json.dumps(data), encoding="utf-8")
        result = self.writePack()
        self.assertTrue(result.bWritten)
        self.assertIn("기록 형식이 낡았습니다", result.reason)
        self.assertEqual(json.loads(recordPath.read_text(encoding="utf-8"))["record_format"], CookAssets.kPackRecordFormatVersion)

    def testBrokenRecordAndTamperedPackAreRewritten(self) -> None:
        self.writePack()
        recordPath = CookAssets.PackRecord.pathFor(self.packPath)
        recordPath.write_text("{ not json", encoding="utf-8")
        self.assertIn("기록을 읽지 못했습니다", self.writePack().reason)

        original = self.packPath.read_bytes()
        self.packPath.write_bytes(original[:-1] + bytes([original[-1] ^ 0xFF]))
        result = self.writePack()
        self.assertTrue(result.bWritten)
        self.assertEqual(result.reason, "팩 파일이 없거나 바이트가 기록과 다릅니다")
        self.assertEqual(self.packPath.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
