"""
commit_message 회귀 검사 — 커밋 메시지는 첫 줄 요약 + 본문 `원인:` → `조치:` → `결과:` 세 단락(병합 · 되돌리기 예외).

실행: python -X utf8 Scripts/lint/tests/test_commit_message.py (CTest `Scripts.CommitMessage`). 저장소를 읽지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.commit_message import find_problems  # noqa: E402

GOOD = """HZB 전이를 그래프로 옮긴다

원인: 손 전이라 그래프가 상태를 몰랐다.
조치: XML 에 출력으로 적고 손 전이 6 곳을 지웠다.
결과: 레벨 콜백이 한 번에 낸다.

Co-Authored-By: Someone <a@b.c>
"""


class CommitMessageTest(unittest.TestCase):
    def test_three_sections_in_order_pass(self) -> None:
        self.assertEqual([], find_problems(GOOD))

    def test_multi_line_sections_pass(self) -> None:
        message = "요약\n\n- 원인:\n  여러 줄 원인\n- 조치: 하나\n  둘\n- 결과: 됨\n"
        self.assertEqual([], find_problems(message))

    def test_missing_section_is_reported(self) -> None:
        problems = find_problems("요약\n\n원인: a\n조치: b\n")
        self.assertTrue(any("`결과:`" in problem for problem in problems))

    def test_subject_only_is_reported(self) -> None:
        self.assertEqual(1, len(find_problems("요약만 있다\n")))

    def test_wrong_order_is_reported(self) -> None:
        problems = find_problems("요약\n\n조치: b\n원인: a\n결과: c\n")
        self.assertTrue(any("차례" in problem for problem in problems))

    def test_empty_section_is_reported(self) -> None:
        problems = find_problems("요약\n\n원인:\n조치: b\n결과: c\n")
        self.assertTrue(any("빈 단락" in problem for problem in problems))

    def test_duplicated_section_is_reported(self) -> None:
        problems = find_problems("요약\n\n원인: a\n원인: a2\n조치: b\n결과: c\n")
        self.assertTrue(any("두 번" in problem for problem in problems))

    def test_footer_and_comment_lines_do_not_count(self) -> None:
        message = "요약\n\nCo-Authored-By: x <y>\n# 원인: 주석\n"
        problems = find_problems(message)
        self.assertTrue(any("없는 단락" in problem for problem in problems))

    def test_merge_and_revert_are_exempt(self) -> None:
        for subject in ("Merge branch 'w-x'", "Revert \"y\"", "fixup! z", "squash! z"):
            self.assertEqual([], find_problems(subject + "\n"), subject)

    def test_empty_message_is_reported(self) -> None:
        self.assertEqual(["첫 줄(요약)이 비었다"], find_problems("\n\n# 주석\n"))


if __name__ == "__main__":
    unittest.main()
