"""cook_code_fingerprint 회귀 검사 — 셰이더 도구 버전 글이 DXC 와 Tint(dawn) 판을 모두 싣고, 하나라도 설치 기록에 없으면 오류다.

실행: python -X utf8 Scripts/generate/tests/test_cook_code_fingerprint.py. 설치 기록은 임시 폴더에 만든다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from generate.cook_code_fingerprint import _make_header_text, _make_tool_version_text  # noqa: E402

STATUS_TEXT = (
    "Package: directx-dxc\nVersion: 2025-02-20\nStatus: install ok installed\n\n"
    "Package: dawn\nVersion: 20250621.085109\nStatus: install ok installed\n\n"
    "Package: dawn\nFeature: tint-tools\nStatus: install ok installed\n"
)


class CookCodeFingerprintTest(unittest.TestCase):
    def test_tool_version_carries_dxc_and_tint(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            status_path = Path(folder) / "status"
            status_path.write_text(STATUS_TEXT, encoding="utf-8")
            version = _make_tool_version_text(status_path)
        self.assertEqual("directx-dxc 2025-02-20; dawn 20250621.085109", version)
        self.assertIn(f'kShaderCompilerVersion = "{version}"', _make_header_text({}, version))

    def test_missing_tint_fails(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            status_path = Path(folder) / "status"
            status_path.write_text("Package: directx-dxc\nVersion: 2025-02-20\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                _make_tool_version_text(status_path)


if __name__ == "__main__":
    unittest.main()
