"""
next-done 회귀 검사 — 꼬리말이 가리킨 줄이 그대로면 남은 것이고, 지웠거나 고쳐 쓴 줄은 지나간다. 맞는 줄이 하나가 아니면 알리기만 한다.

실행: python -X utf8 Scripts/dev/tests/test_next_done.py (CTest `Scripts.NextDone`). git 을 띄우지 않는다.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev.next_done import find_remaining  # noqa: E402
from lint.commit_message import find_problems  # noqa: E402

BASE = """# 할 일
렌더러 규격 설명 글은 목록이 아니다
- `[코드]` **렌더러 규격:** 일시 버퍼, 뷰 히스토리.
- `[코드]` 물리: 캐릭터 쌍 전부 훑기.
- `[코드]` 물리: 공간 색인.
"""
MESSAGE = "요약\n\n원인: a\n조치: b\n결과: c\n\nNEXT: {} 끝\nCo-Authored-By: x <x@y>\n"


class NextDoneTest(unittest.TestCase):
    def test_unchanged_line_is_remaining(self) -> None:
        remaining, warning = find_remaining([MESSAGE.format("렌더러 규격")], BASE, BASE)
        self.assertEqual(remaining, ["- `[코드]` **렌더러 규격:** 일시 버퍼, 뷰 히스토리."])
        self.assertEqual(warning, [])

    def test_removed_or_rewritten_line_passes(self) -> None:
        removed = BASE.replace("- `[코드]` **렌더러 규격:** 일시 버퍼, 뷰 히스토리.\n", "")
        rewritten = BASE.replace("일시 버퍼, 뷰 히스토리", "뷰 히스토리")
        for head in (removed, rewritten):
            self.assertEqual(find_remaining([MESSAGE.format("렌더러 규격")], BASE, head), ([], []))

    def test_words_matching_no_line_or_many_are_only_reported(self) -> None:
        remaining, warning = find_remaining([MESSAGE.format("물리"), MESSAGE.format("없는 줄"), "NEXT 줄 이야기 — 꼬리말이 아니다 끝"], BASE, BASE)
        self.assertEqual(remaining, [])
        self.assertEqual(len(warning), 2)

    def test_the_trailer_is_accepted_by_the_commit_message_hook(self) -> None:
        self.assertEqual(find_problems(MESSAGE.format("렌더러 규격")), [])


if __name__ == "__main__":
    unittest.main()
