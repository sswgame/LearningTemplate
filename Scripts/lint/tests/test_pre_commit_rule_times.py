"""
린트 규칙별 시간 줄(`pre_commit.format_rule_times`) — `--all` 은 느린 다섯과 합, 커밋 훅은 예산을 넘은 규칙만 경고.

실행: python -X utf8 Scripts/lint/tests/test_pre_commit_rule_times.py (CTest `Scripts.PreCommitRuleTimes`). git 을 띄우지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.pre_commit import format_rule_times, read_hook_budget_seconds  # noqa: E402


class RuleTimesTest(unittest.TestCase):
    TIMES = [(0.01, "a"), (0.30, "b"), (0.02, "c"), (0.06, "d"), (0.5, "e"), (0.04, "f"), (0.2, "g")]

    def test_whole_repo_lists_the_slowest_five_and_the_sum(self) -> None:
        line = format_rule_times(self.TIMES, True, 0.05)
        self.assertEqual(line, "[lint-time] 느린 규칙: e 0.50s · b 0.30s · g 0.20s · d 0.06s · f 0.04s (합 1.1s)")

    def test_commit_hook_lists_only_rules_over_the_budget(self) -> None:
        self.assertEqual(format_rule_times(self.TIMES, False, 0.05), "[lint-time] 경고: 규칙 하나가 50 ms 를 넘었다 — e 500 ms · b 300 ms · g 200 ms · d 60 ms")
        self.assertIsNone(format_rule_times([(0.01, "a")], False, 0.05))

    def test_budget_is_read_from_config(self) -> None:
        self.assertGreater(read_hook_budget_seconds(Path(__file__).resolve().parents[3]), 0.0)


if __name__ == "__main__":
    unittest.main()
