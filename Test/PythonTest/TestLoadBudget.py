#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
qa/Load 의 예산 판정 — 측정 기계가 아닌 러너(`--report-time-budgets`)에서 시간 예산은 알림, 정확성 · 메모리 예산은 실패로 남는지,
그리고 CiFailureReport load 가 통과한 조합의 넘은 시간 예산을 ::notice 로 올리는지 본다.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

kScriptsRoot = Path(__file__).resolve().parents[2] / "Scripts"
sys.path.insert(0, str(kScriptsRoot))

from dev import CiFailureReport  # noqa: E402
from qa import Load  # noqa: E402


class LoadBudgetTest(unittest.TestCase):
    def setUp(self) -> None:
        self.budget = {"converged": 1, "tick_p99_us": 16667, "hitch.over33ms": 5, "private_mb.max": 2048, "error_percent": 1.0}
        measured = {"converged": 0.0, "tick_p99_us": 17857.0, "hitch.over33ms": 9.0, "private_mb.max": 4096.0, "error_percent": 0.0}
        self.summary = {name: {"median": value, "min": value, "max": value} for name, value in measured.items()}

    def testMeasurementMachineJudgesEveryBudget(self) -> None:
        listFailure, listOverrun, _ = Load.judgeBudget(self.budget, self.summary)
        self.assertEqual(len(listFailure), 4)
        self.assertEqual(listOverrun, [])

    def testRunnerReportsTimeBudgetsAndJudgesTheRest(self) -> None:
        listFailure, listOverrun, _ = Load.judgeBudget(self.budget, self.summary, bJudgeTime=False)
        self.assertEqual(sorted(line.split(": ")[1].split(" ")[0] for line in listFailure), ["converged", "private_mb.max"])
        self.assertEqual(sorted(line.split(": ")[1].split(" ")[0] for line in listOverrun), ["hitch.over33ms", "tick_p99_us"])

    def testTimeMetricNames(self) -> None:
        for name in ("GT.Frame.p99_us", "tick_max_us", "startup_ms", "hitch.worst_us", "hitch.over100ms", "login.p99_us"):
            self.assertTrue(Load.isTimeMetric(name), name)
        for name in ("converged", "error_percent", "private_mb.max", "gpu_memory_mb", "alloc.per_frame", "private_mb_per_minute"):
            self.assertFalse(Load.isTimeMetric(name), name)

    def testReportPostsNoticeForPassingCombination(self) -> None:
        result = {"scenario": "replication", "profile": "Mid", "verdict": "pass", "time_budget_overruns": ["server tick (cluster): tick_p99_us 17857.0 > budget 16667"]}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "nightly.json"
            path.write_text(json.dumps({"results": [result]}), encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                CiFailureReport.reportLoad(path)
        self.assertIn("::notice title=load time budget replication x Mid::", output.getvalue())
        self.assertNotIn("::error", output.getvalue())


if __name__ == "__main__":
    unittest.main()
