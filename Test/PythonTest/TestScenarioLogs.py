#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev/ScenarioLogs(`scenario-logs`) — 보고 읽기, 낡은 항목 래칫, 처리하지 않은 로그 초안, 보고 없음은 판정 불가."""

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

from common.ExitCode import ExitCode  # noqa: E402
from dev import RunScenarioLogs as ScenarioLogs  # noqa: E402


def writeReport(folder: Path, fileName: str, name: str, exitCode: int, listUnhandled: list[str], listAllowed: list[str]) -> None:
    """시나리오 보고 한 편(`AutomationReportWriter` 의 모양에서 이 도구가 읽는 칸)을 쓴다."""
    data = {"name": name, "result": "X", "exitCode": exitCode, "frames": 10, "reason": "", "failures": [], "unconfirmed": [],
            "unhandledLogs": listUnhandled, "allowedLogs": listAllowed, "metrics": [], "records": []}
    (folder / fileName).write_text(json.dumps(data), encoding="utf-8")


def writeAllowList(path: Path, listEntry: list[dict[str, str]]) -> None:
    path.write_text(json.dumps({"allowed": listEntry}), encoding="utf-8")


def makeEntry(entryId: str, scenario: str) -> dict[str, str]:
    return {"id": entryId, "scenario": scenario, "reason": "known", "plan": "SomePlan"}


class ExitCodeTableTest(unittest.TestCase):
    def testScriptReadsCppExitCodeTable(self) -> None:
        """종료 코드 정본(`AutomationExitCode.h`)을 읽어 온다 — 표가 바뀌어도 숫자가 따로 어긋나지 않고, 줄 모양이 바뀌면 여기서 멈춘다."""
        self.assertEqual((0, 14, 15), ScenarioLogs.kRanToEndExitCodes)
        self.assertEqual(15, ScenarioLogs.kUnhandledLogExitCode)

    def testMissingCaseLineStopsLoudly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            header = Path(temporary) / "AutomationExitCode.h"
            header.write_text("case AutomationOutcome::Passed: return 0;", encoding="utf-8")
            with self.assertRaises(SystemExit):
                ScenarioLogs.readOutcomeExitCodes(header)


class ReportReadTest(unittest.TestCase):
    def testOldFormatAndForeignJsonAreNotReports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "old.json").write_text(json.dumps({"name": "a", "exitCode": 0, "failures": []}), encoding="utf-8")
            (folder / "other.json").write_text("[1, 2]", encoding="utf-8")
            (folder / "broken.json").write_text("{", encoding="utf-8")
            writeReport(folder, "new.json", "alpha", 0, [], ["Font.NoGlyph"])
            listReport = ScenarioLogs.findReports([folder])
            self.assertEqual([report.name for report in listReport], ["alpha"])
            self.assertEqual(listReport[0].listAllowedId, ("Font.NoGlyph",))


class RatchetTest(unittest.TestCase):
    def testEntryThatNoLongerAppearsIsStaleOnlyWhenTheScenarioRanToTheEnd(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            writeReport(folder, "alpha_dx12.json", "alpha", 0, [], ["Font.NoGlyph"])
            writeReport(folder, "alpha_vk.json", "alpha", 14, [], [])  # 한 백엔드에서만 나와도 항목은 산다
            writeReport(folder, "beta_dx12.json", "beta", 0, [], [])  # 끝까지 돌았는데 한 번도 안 나왔다
            writeReport(folder, "gamma_dx12.json", "gamma", 10, [], [])  # 실패한 실행은 로그 게이트까지 가지 못해 판단하지 않는다
            listEntry = [ScenarioLogs.AllowEntry("Font.NoGlyph", "alpha", ""), ScenarioLogs.AllowEntry("Font.NoGlyph", "beta", ""),
                         ScenarioLogs.AllowEntry("Gpu.Validation", "gamma", ""), ScenarioLogs.AllowEntry("Gpu.Validation", "", "RHITest"),
                         ScenarioLogs.AllowEntry("Font.NoGlyph", "delta", "")]
            listStale, listNotRun = ScenarioLogs.findStaleEntries(listEntry, ScenarioLogs.findReports([folder]))
            self.assertEqual([(entry.id, entry.scenario) for entry in listStale], [("Font.NoGlyph", "beta")])
            self.assertEqual(sorted(entry.scenario for entry in listNotRun), ["delta", "gamma"])  # 스위트 항목은 어느 쪽에도 들지 않는다

    def testCheckFailsOnStaleEntryAndOnUnhandledReport(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "reports"
            folder.mkdir()
            allowList = Path(temporary) / "allowedlogs.json"
            writeAllowList(allowList, [makeEntry("Font.NoGlyph", "alpha")])
            writeReport(folder, "alpha.json", "alpha", 0, [], ["Font.NoGlyph"])
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ScenarioLogs.main(["--check", "--reports", str(folder), "--allowlist", str(allowList)]), ExitCode.Passed)
                writeReport(folder, "alpha.json", "alpha", 0, [], [])
                self.assertEqual(ScenarioLogs.main(["--check", "--reports", str(folder), "--allowlist", str(allowList)]), ExitCode.Violation)
                writeReport(folder, "alpha.json", "alpha", 15, ["[Warning] [Ui] (no log id) something"], [])
                self.assertEqual(ScenarioLogs.main(["--check", "--reports", str(folder), "--allowlist", str(allowList)]), ExitCode.Violation)

    def testNoReportsCannotBeJudged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            allowList = Path(temporary) / "allowedlogs.json"
            writeAllowList(allowList, [])
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(ScenarioLogs.main(["--check", "--reports", temporary, "--allowlist", str(allowList)]), ExitCode.CheckInvalid)


class RegenTest(unittest.TestCase):
    def testDraftHasTodoFieldsAndSkipsKnownAndIdLessLines(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "reports"
            folder.mkdir()
            allowList = Path(temporary) / "allowedlogs.json"
            writeAllowList(allowList, [makeEntry("Font.NoGlyph", "alpha")])
            writeReport(folder, "alpha_dx12.json", "alpha", 15,
                        ["[Warning] [Font] (id=Font.NoGlyph) known", "[Error] [Ui] (id=UI.WidgetNameDuplicate) twice", "[Warning] [Ui] (no log id) plain text"], [])
            writeReport(folder, "alpha_vk.json", "alpha", 15, ["[Error] [Ui] (id=UI.WidgetNameDuplicate) twice"], [])  # 다른 백엔드의 같은 줄은 한 항목이다
            writeReport(folder, "beta.json", "beta", 0, [], [])
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(ScenarioLogs.main(["--regen", "--reports", str(folder), "--allowlist", str(allowList)]), ExitCode.Passed)
            data = json.loads(allowList.read_text(encoding="utf-8"))
            self.assertEqual([(entry["id"], entry["scenario"]) for entry in data["allowed"]], [("Font.NoGlyph", "alpha"), ("UI.WidgetNameDuplicate", "alpha")])
            draft = data["allowed"][1]
            self.assertEqual((draft["reason"], draft["plan"]), (ScenarioLogs.kPlaceholderText, ScenarioLogs.kPlaceholderText))
            self.assertIn("SW_LOG_WARNING_ID", output.getvalue())  # id 없는 줄은 목록에 넣지 않고 id 를 붙이라고 알린다
            self.assertNotIn("plain text", json.dumps(data))

    def testDryRunDoesNotWrite(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / "reports"
            folder.mkdir()
            allowList = Path(temporary) / "allowedlogs.json"
            writeAllowList(allowList, [])
            before = allowList.read_text(encoding="utf-8")
            writeReport(folder, "alpha.json", "alpha", 15, ["[Error] [Ui] (id=UI.X) boom"], [])
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ScenarioLogs.main(["--regen", "--dry-run", "--reports", str(folder), "--allowlist", str(allowList)]), ExitCode.Passed)
            self.assertEqual(allowList.read_text(encoding="utf-8"), before)


if __name__ == "__main__":
    unittest.main()
