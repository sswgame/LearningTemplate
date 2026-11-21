"""
테스트 이력(`test_history.py`) — 실행 종류 · 표지(--since) · 흔들림 판정 · 느려짐 경고 · 단계 시간 · changed 단계(자체 검사).

실행: python -X utf8 Scripts/dev/tests/test_test_history.py (CTest `Scripts.TestHistory`). 빌드 · git 을 띄우지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.test_history import run_self_check  # noqa: E402


class TestHistoryTest(unittest.TestCase):
    def test_self_check(self) -> None:
        run_self_check()


if __name__ == "__main__":
    unittest.main()
