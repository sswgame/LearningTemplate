"""
린트 표 형식(`lint.rule_tables`) 회귀 검사 — 경로 짓기, 머리 칸 · 키 모양, 사유 예외 표(까닭 빈 줄 · 낡은 줄 · 쓴 키 기록), 래칫 기준선(늘면 실패 · 줄면 낮춤 · 받기 · 한 줄 쓰기).
규칙 모듈마다 손으로 짜던 표 검사를 여기 하나로 모았다 — 규칙 모듈의 자체 검사는 판정만 본다.

실행: python -X utf8 Scripts/lint/tests/test_rule_tables.py (CTest `Scripts.RuleTables`). 저장소 표를 읽는 것은 머리 검사 하나다.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from lint.rule_tables import TABLE_FOLDER, Baseline, ExceptionTable, check_head, get_table_path, read_table  # noqa: E402

HEAD = {"_comment": "시험 표"}


class TablePathTest(unittest.TestCase):
    def test_rule_id_becomes_pascal_case(self) -> None:
        self.assertEqual("Config/Rules/Lint/ThreadOwners.json", get_table_path("thread-owners"))
        self.assertEqual("Config/Rules/Lint/MagicLiteralsBaseline.json", get_table_path("magic-literals", "Baseline"))
        self.assertEqual("Config/Rules/Lint/NamingWordsBannedNames.json", get_table_path("naming-words", "BannedNames"))


class HeadTest(unittest.TestCase):
    def test_comment_is_required(self) -> None:
        with self.assertRaises(ValueError):
            check_head("x.json", {"_items": {}})
        with self.assertRaises(ValueError):
            check_head("x.json", {"_comment": " ", "_items": {}})

    def test_keys_are_underscore_camel_case(self) -> None:
        check_head("x.json", {"_comment": "c", "_devCommands": {}, "_logIDs": {}})
        for bad in ("items", "_Items", "_dev_commands", "_description2x-"):
            with self.assertRaises(ValueError, msg=bad):
                check_head("x.json", {"_comment": "c", bad: {}})

    def test_every_lint_table_in_repo_has_the_head(self) -> None:
        root = get_project_root()
        list_path = sorted((root / TABLE_FOLDER).glob("*.json"))
        self.assertTrue(list_path)
        for path in list_path:
            read_table(root, path.relative_to(root).as_posix())

class ExceptionTableTest(unittest.TestCase):
    def make(self, **sections: dict) -> ExceptionTable:
        return ExceptionTable("x.json", {**HEAD, **sections}, tuple(sections))

    def test_missing_section_stops(self) -> None:
        with self.assertRaises(ValueError):
            ExceptionTable("x.json", HEAD, ("_owners",))

    def test_used_keys_are_not_stale(self) -> None:
        table = self.make(_owners={"Source/A": "까닭", "Source/B": "까닭"})
        self.assertTrue(table.allows("_owners", "Source/A"))
        self.assertFalse(table.allows("_owners", "Source/C"))
        problems = table.list_problems(whole_repo=True)
        self.assertEqual(1, len(problems))
        self.assertIn("낡은 예외 _owners 'Source/B'", problems[0])

    def test_stale_lines_need_the_whole_repo(self) -> None:
        table = self.make(_owners={"Source/A": "까닭"})
        self.assertEqual([], table.list_problems(whole_repo=False))

    def test_stale_check_takes_chosen_sections(self) -> None:
        table = self.make(_owners={"Source/A": "까닭"}, _paths={"Source/": "까닭"})
        self.assertEqual(1, len(table.list_problems(whole_repo=True, stale_sections=("_owners",))))

    def test_empty_reason_is_a_problem_even_for_a_partial_run(self) -> None:
        table = self.make(_owners={"Source/A": " "}, _limits={"A.h": {"_limit": 3, "_reason": ""}, "B.h": {"_limit": 2, "_reason": "까닭"}})
        problems = table.list_problems(whole_repo=False)
        self.assertEqual(2, len(problems))
        self.assertTrue(all("까닭이 비었다" in line for line in problems))

    def test_prefix_keys_record_use(self) -> None:
        table = self.make(_paths={"Source/Editor/": "까닭", "Source/Programs/": "까닭"})
        self.assertTrue(table.allows_prefix("_paths", "Source/Editor/A.cpp"))
        self.assertFalse(table.allows_prefix("_paths", "Source/Engine/A.cpp"))
        self.assertEqual(["Source/Programs/"], [line.split("'")[1] for line in table.list_problems(whole_repo=True)])


class BaselineTest(unittest.TestCase):
    def make(self, files: dict) -> Baseline:
        return Baseline("x.json", {**HEAD, "_files": files})

    def compare(self, baseline: Baseline, actual: dict, list_path: list[str], map_old: dict | None = None) -> list[str]:
        return baseline.compare(actual, list_path, map_old or {}, ("float", "integer"), lambda kind: f"{kind} 를 옮긴다", "lint x --ratchet")

    def test_ratchet(self) -> None:
        baseline = self.make({"Source/A.cpp": {"float": 2}, "Source/Old.cpp": {"integer": 1}})
        self.assertEqual([], self.compare(baseline, {"Source/A.cpp": {"float": 2}}, ["Source/A.cpp"]))
        self.assertIn("늘었다: float 를 옮긴다", self.compare(baseline, {"Source/A.cpp": {"float": 3}}, ["Source/A.cpp"])[0])
        self.assertIn("줄었다: `lint x --ratchet`", self.compare(baseline, {"Source/A.cpp": {"float": 1}}, ["Source/A.cpp"])[0])
        self.assertEqual(1, len(self.compare(baseline, {"Source/New.cpp": {"integer": 1}}, ["Source/New.cpp"])), "새 파일은 0 에서 시작한다")
        self.assertEqual([], self.compare(baseline, {"Source/Moved.cpp": {"integer": 1}}, ["Source/Moved.cpp"], {"Source/Moved.cpp": "Source/Old.cpp"}), "옮긴 파일은 옛 기준선을 잇는다")
        self.assertEqual(["Source/Old.cpp: 없는 파일의 기준선 — `--ratchet` 으로 지운다"], baseline.list_missing({"Source/A.cpp"}))

    def test_lower_keeps_smaller_counts_and_drops_missing_files(self) -> None:
        baseline = self.make({"Source/A.cpp": {"float": 2, "integer": 1}, "Source/Gone.cpp": {"integer": 1}})
        baseline.lower({"Source/A.cpp": {"float": 3}}, {"Source/A.cpp"})
        self.assertEqual({"Source/A.cpp": {"float": 2}}, baseline.files)

    def test_write_keeps_the_comment_and_one_line_per_file(self) -> None:
        baseline = self.make({"Source/B.cpp": {"float": 1}, "Source/A.cpp": {"integer": 2}, "Source/Zero.cpp": {}})
        baseline.accept("Source/C.cpp", {"float": 4})
        text = baseline.dump()
        self.assertEqual({"_comment": "시험 표", "_files": {"Source/A.cpp": {"integer": 2}, "Source/B.cpp": {"float": 1}, "Source/C.cpp": {"float": 4}}}, json.loads(text))
        self.assertEqual(['    "Source/A.cpp": {"integer": 2},', '    "Source/B.cpp": {"float": 1},', '    "Source/C.cpp": {"float": 4}'], text.splitlines()[3:6])
        with tempfile.TemporaryDirectory() as folder:
            baseline.path = "b.json"
            baseline.write(Path(folder))
            self.assertEqual(json.loads(text)["_files"], Baseline("b.json", read_table(Path(folder), "b.json")).files)


if __name__ == "__main__":
    unittest.main()
