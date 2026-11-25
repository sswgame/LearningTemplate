"""
C++ 글 지우기(`cpp_code.strip_code`) 회귀 검사 — 주석 · 문자열 · 원시 문자열 · 문자 상수 · 전처리 줄을 같은 길이의 공백으로 바꾼다.

실행: python -X utf8 Scripts/lint/tests/test_cpp_code.py (CTest `Scripts.CppCode`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.cpp_code import strip_code  # noqa: E402


class StripCodeTest(unittest.TestCase):
    def test_strip_code(self) -> None:
        stripped = {
            'a // c\nb': 'a     \nb', 'x /* c\nd */ y': 'x     \n     y', 's = "q\\"w";': 's = "    ";', "c = 'x'; it's": "c = ' '; it's",
            'r = R"d(a)"b\n)d";': 'r = R       \n   ;', 'fooR"x"': 'fooR" "', '#define A \\\n  B\nint q;': '           \n   \nint q;',
            'u = "open\nnext': 'u = "    \nnext', '/* open': '       ',
        }
        for text, want in stripped.items():
            self.assertEqual(strip_code(text), want, repr(text))


if __name__ == "__main__":
    unittest.main()
