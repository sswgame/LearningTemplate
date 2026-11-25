"""
vcpkg 매니페스트 포맷 회귀 검사 — `--check` 는 어긋난 파일을 알리고 고치지 않으며, 고치는 쪽은 다른 파일만 다시 쓴다. 명령이 없거나 지면 오류다.

실행: python -X utf8 Scripts/lint/tests/test_vcpkg_manifest_format.py (CTest `Scripts.VcpkgManifestFormat`). vcpkg 대신 키를 정렬해 쓰는 가짜 명령을 띄운다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.format_files import run_vcpkg_manifest_format  # noqa: E402

FAKE_VCPKG = """
import json, sys
assert sys.argv[1] == "format-manifest"
for path in sys.argv[2:]:
    with open(path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(json.dumps(manifest, sort_keys=True))
"""
UNSORTED = '{"b": 1, "a": 2}'
SORTED = '{"a": 2, "b": 1}'


class VcpkgManifestFormatTest(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        (self.folder / "fake.py").write_text(FAKE_VCPKG, encoding="utf-8")
        self.command = [sys.executable, self.folder / "fake.py"]

    def make_manifest(self, name: str, text: str) -> Path:
        path = self.folder / name / "vcpkg.json"
        path.parent.mkdir()
        path.write_text(text, encoding="utf-8")
        return path

    def test_check_reports_without_writing(self) -> None:
        manifest = self.make_manifest("a", UNSORTED)
        self.assertNotEqual(0, run_vcpkg_manifest_format([manifest], check_only=True, vcpkg_command=self.command))
        self.assertEqual(UNSORTED, manifest.read_text(encoding="utf-8"))

    def test_fix_rewrites_and_check_then_passes(self) -> None:
        list_manifest = [self.make_manifest("a", UNSORTED), self.make_manifest("b", SORTED)]
        self.assertEqual(0, run_vcpkg_manifest_format(list_manifest, check_only=False, vcpkg_command=self.command))
        self.assertEqual([SORTED, SORTED], [path.read_text(encoding="utf-8") for path in list_manifest])
        self.assertEqual(0, run_vcpkg_manifest_format(list_manifest, check_only=True, vcpkg_command=self.command))

    def test_failing_or_missing_command_is_error(self) -> None:
        broken = self.make_manifest("a", "{")
        self.assertNotEqual(0, run_vcpkg_manifest_format([broken], check_only=False, vcpkg_command=self.command))
        self.assertEqual("{", broken.read_text(encoding="utf-8"))
        self.assertNotEqual(0, run_vcpkg_manifest_format([broken], check_only=True, vcpkg_command=[self.folder / "missing"]))


if __name__ == "__main__":
    unittest.main()
