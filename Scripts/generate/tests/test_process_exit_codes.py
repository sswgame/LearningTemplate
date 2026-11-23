"""
프로세스 종료 코드 생성 검사 — 저장소 표가 읽히고, 범위 밖 · 예약된 코드 · 겹친 줄 · 0 이 Passed 가 아닌 표는 오류이며, 생성 글이 표의 줄을 그대로 낸다.

실행: python -X utf8 Scripts/generate/tests/test_process_exit_codes.py. 표는 임시 폴더에 만든다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from generate.generator_output import GeneratorError  # noqa: E402
from generate.process_exit_codes import CONFIG_RELATIVE, load_rows, make_cmake_text, make_header_text  # noqa: E402

PASSED = {"name": "Passed", "code": 0, "label": "passed"}


class ProcessExitCodesTest(unittest.TestCase):
    def _load(self, rows: list[dict[str, object]], reserved: list[dict[str, object]] | None = None) -> list[tuple[str, int, str]]:
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "ProcessExitCodes.json"
            path.write_text(json.dumps({"exit_codes": rows, "reserved_codes": reserved or []}), encoding="utf-8")
            return load_rows(path)

    def _assert_rejected(self, rows: list[dict[str, object]], reserved: list[dict[str, object]] | None = None) -> None:
        with self.assertRaises(GeneratorError):
            self._load(rows, reserved)

    def test_repository_table_loads(self) -> None:
        names = [name for name, _, _ in load_rows(get_project_root() / CONFIG_RELATIVE)]
        self.assertIn("RHIUnusableHere", names)

    def test_codes_outside_the_platform_range_are_errors(self) -> None:
        for code in (-1, 126, 127, 128, 256, 3.0, "10", True):
            self._assert_rejected([PASSED, {"name": "Other", "code": code, "label": "other"}])
        self.assertEqual(self._load([PASSED, {"name": "Other", "code": 125, "label": "other"}])[1], ("Other", 125, "other"))

    def test_reserved_codes_are_errors(self) -> None:
        reserved = [{"code": 14, "reason": "removed"}]
        self._assert_rejected([PASSED, {"name": "Other", "code": 14, "label": "other"}], reserved)
        self._assert_rejected([PASSED], [{"code": 14}])
        self._assert_rejected([PASSED], [{"code": 300, "reason": "outside"}])

    def test_zero_is_passed_and_rows_are_unique(self) -> None:
        self._assert_rejected([{"name": "Failed", "code": 0, "label": "failed"}])
        self._assert_rejected([{"name": "Failed", "code": 10, "label": "failed"}])
        self._assert_rejected([{"name": "Passed", "code": 10, "label": "passed"}])
        for duplicate in ({"name": "A", "code": 11, "label": "b"}, {"name": "B", "code": 10, "label": "b"}, {"name": "B", "code": 11, "label": "a"}):
            self._assert_rejected([PASSED, {"name": "A", "code": 10, "label": "a"}, duplicate])
        self._assert_rejected([PASSED, {"name": "not-an-identifier", "code": 10, "label": "a"}])

    def test_generated_texts_carry_every_row(self) -> None:
        rows = self._load([PASSED, {"name": "Failed", "code": 10, "label": "failed"}])
        header = make_header_text(rows)
        self.assertIn("Failed = 10,", header)
        self.assertIn('case 10:\n                return "failed";', header)
        self.assertIn("set(SW_EXIT_CODE_Failed 10)", make_cmake_text(rows))


if __name__ == "__main__":
    unittest.main()
