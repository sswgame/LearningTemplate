"""
린트 규칙의 기반(`lint_rule.py`) 회귀 검사 — 파일마다 한 번만 읽고, 규칙마다의 범위를 지키며, 규칙 발견이 저장소의 표와 맞는다.

실행: python -X utf8 Scripts/lint/tests/test_lint_rule.py (CTest `Scripts.LintRule`). 규칙 둘을 임시 폴더에서 돌린다.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Mapping
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint import lint_rule  # noqa: E402
from lint.lint_rule import LintRule, SourceFile, find_orphan_tables, find_rules, inspect_files  # noqa: E402
from lint.lint_scope import LintScope  # noqa: E402

PROJECT_ROOT = Path(__file__).resolve().parents[3]


class WordRule(LintRule):
    id, group, blocks, why_not_type = "word", "모양", "x", "y"

    def selects(self, path: str) -> bool:
        return path.endswith(".h")

    def inspect(self, file: SourceFile) -> int:
        return file.code.count("bad")

    def evaluate(self, scope: LintScope, found: Mapping[str, Any]) -> list[str]:
        return [f"{path}: {count}" for path, count in found.items()]


class LongRule(WordRule):
    id = "long"


class LintRuleTest(unittest.TestCase):
    def test_each_file_is_read_once_for_all_rules(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "a.h").write_text("bad // bad\n#define bad\n", encoding="utf-8")
            (root / "b.h").write_text("good\n", encoding="utf-8")
            map_rule = {rule.id: rule for rule in (WordRule(root), LongRule(root))}
            scope = LintScope(("a.h", "b.h", "c.txt"), (), True)
            with mock.patch.object(lint_rule, "find_rules", return_value=map_rule), mock.patch.object(lint_rule, "read_text", wraps=lint_rule.read_text) as read:
                found, _ = inspect_files(root, {map_rule["word"]: scope, map_rule["long"]: LintScope(("b.h",), (), False)})
        self.assertEqual(found, {"word": {"a.h": 1}, "long": {}})   # 주석 · 전처리 줄의 낱말은 세지 않는다
        self.assertEqual(sorted(Path(call.args[0]).name for call in read.call_args_list), ["a.h", "b.h"])

    def test_discovered_rules_own_every_table(self) -> None:
        map_rule = find_rules(PROJECT_ROOT)
        self.assertGreater(len(map_rule), 0)
        self.assertEqual(find_orphan_tables(PROJECT_ROOT), [])


if __name__ == "__main__":
    unittest.main()
