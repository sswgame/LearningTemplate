#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""qa/TestCostCalibration(`test-costs`) — ctest 로그에서 케이스 · 샤드 시간을 읽고, 정책 표의 숫자만 줄끝을 보존해 고친다."""

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
from qa import TestCostCalibration as Calibration  # noqa: E402

kLog = """\
1/3 Testing: AppTest_HostOnly_Shard0
1/3 Test: AppTest_HostOnly_Shard0
Output:
----------------------------------------------------------
[       OK ] AppScenarioTest.EditorScenariosPassOnVulkan (100000.00 ms)
[       OK ] AppScenarioTest.nilecity.determinism.Vulkan (12000.50 ms)
<end of output>
Test time = 120.50 sec
----------------------------------------------------------
2/3 Testing: AppTest_HostOnly_Shard1
Output:
[       OK ] AppScenarioTest.RecordsMatchAcrossBackends (61000.00 ms)
[  FAILED  ] AppSmokeTest.EditorSelfTestsPassInsideTheEditor (14000.00 ms)
Test time = 80.00 sec
3/3 Testing: PythonTest_Other
Test time = 1.00 sec
"""

kPolicy = (
    # CoreTest 가 먼저 오고 같은 이름을 이미 맞는 값(100)으로 들고 있다 — 범위를 안 지키면 첫 줄(CoreTest 것)을 바꿔 AppTest 의 444 가 남는다.
    '{\r\n  "executables": {\r\n    "CoreTest": {\r\n      "caseCosts": {\r\n        "AppScenarioTest.EditorScenariosPassOnVulkan": 100\r\n      }\r\n    },\r\n'
    '    "AppTest": {\r\n      "hostCaseCosts": {\r\n'
    '        "AppScenarioTest.EditorScenariosPassOnVulkan": 444,\r\n'
    '        "AppScenarioTest.RecordsMatchAcrossBackends": 60,\r\n'
    '        "AppSmokeTest.EditorSelfTestsPassInsideTheEditor": 13,\r\n'
    '        "AppUITest.NotInTheLog": 16\r\n      },\r\n'
    '      "_hostCaseCostsReason": "문자열 안의 { 괄호 } 와 \\"따옴표\\" 가 있어도 범위가 맞는다"\r\n    }\r\n  }\r\n}\r\n'
)


class ReadLogTest(unittest.TestCase):
    def testCaseAndTestSecondsAreRead(self) -> None:
        reading = Calibration.readLogs([kLog])
        self.assertAlmostEqual(100.0, reading.mapCaseSeconds["AppScenarioTest.EditorScenariosPassOnVulkan"])
        self.assertAlmostEqual(14.0, reading.mapCaseSeconds["AppSmokeTest.EditorSelfTestsPassInsideTheEditor"])   # 실패한 케이스의 시간도 시간이다
        self.assertEqual({"AppTest_HostOnly_Shard0": 120.5, "AppTest_HostOnly_Shard1": 80.0, "PythonTest_Other": 1.0}, reading.mapTestSeconds)

    def testLargestValueWinsAcrossLogs(self) -> None:
        reading = Calibration.readLogs(["[       OK ] A.B (1000.00 ms)\n", "[       OK ] A.B (3000.00 ms)\n"])
        self.assertAlmostEqual(3.0, reading.mapCaseSeconds["A.B"])


class ProposalTest(unittest.TestCase):
    def testOnlyFarValuesAreProposedAndMissingOnesAreKept(self) -> None:
        mapPolicyCost = Calibration.readPolicyCosts(json.loads(kPolicy))
        reading = Calibration.readLogs([kLog])
        listChange, listMissing = Calibration.proposeCosts(mapPolicyCost, reading, 1.5, 3.0)
        self.assertEqual([("AppTest", "AppScenarioTest.EditorScenariosPassOnVulkan", 100)], [(p.executable, p.caseName, p.newSeconds) for p in listChange])
        self.assertEqual(["AppUITest.NotInTheLog"], [p.caseName for p in listMissing])   # 60 → 61, 13 → 14 는 흔들림이라 둔다

    def testShardOverlapIsSumOverLongest(self) -> None:
        listOverlap = Calibration.findShardOverlaps(Calibration.readLogs([kLog]).mapTestSeconds)
        self.assertEqual(1, len(listOverlap))
        self.assertEqual("AppTest_HostOnly", listOverlap[0].group)
        self.assertAlmostEqual(200.5 / 120.5, listOverlap[0].overlap)

    def testUnlistedSlowCasesExcludeTheTable(self) -> None:
        reading = Calibration.readLogs([kLog])
        listUnlisted = Calibration.findUnlistedSlowCases(reading, {"AppScenarioTest.EditorScenariosPassOnVulkan"}, 5)
        self.assertEqual("AppScenarioTest.RecordsMatchAcrossBackends", listUnlisted[0][0])


class ApplyTest(unittest.TestCase):
    def testOnlyTheNumberInTheExecutableBlockChangesAndLineEndingsStay(self) -> None:
        mapPolicyCost = Calibration.readPolicyCosts(json.loads(kPolicy))
        listChange, _ = Calibration.proposeCosts(mapPolicyCost, Calibration.readLogs([kLog]), 1.5, 3.0)
        newText, changedCount = Calibration.applyProposals(kPolicy, listChange)
        self.assertEqual(1, changedCount)
        self.assertEqual(kPolicy.replace("PassOnVulkan\": 444", "PassOnVulkan\": 100"), newText)
        self.assertNotIn("444", newText)
        self.assertEqual(newText.count("\r\n"), newText.count("\n"))
        json.loads(newText)

    def testMainDryRunWritesNothingAndApplyWritesBytes(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            logPath = Path(folder) / "LastTest.log"
            policyPath = Path(folder) / "TestPolicy.json"
            logPath.write_text(kLog, encoding="utf-8")
            policyPath.write_bytes(kPolicy.encode("utf-8"))
            with contextlib.redirect_stdout(io.StringIO()) as output:
                self.assertEqual(ExitCode.Passed, Calibration.main(["--log", str(logPath), "--policy", str(policyPath)]))
            self.assertIn("표 444 s → 잰 값 100.0 s", output.getvalue())
            self.assertEqual(kPolicy.encode("utf-8"), policyPath.read_bytes())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(ExitCode.Passed, Calibration.main(["--log", str(logPath), "--policy", str(policyPath), "--apply"]))
            self.assertIn(b"PassOnVulkan\": 100", policyPath.read_bytes())

    def testLogWithoutCaseLinesIsInvalid(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            logPath = Path(folder) / "LastTest.log"
            policyPath = Path(folder) / "TestPolicy.json"
            logPath.write_text("nothing\n", encoding="utf-8")
            policyPath.write_bytes(kPolicy.encode("utf-8"))
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(ExitCode.CheckInvalid, Calibration.main(["--log", str(logPath), "--policy", str(policyPath)]))

    def testRepositoryPolicyIsReadable(self) -> None:
        policyPath = Path(__file__).resolve().parents[2] / Calibration.kPolicyRelPath
        mapPolicyCost = Calibration.readPolicyCosts(json.loads(policyPath.read_text(encoding="utf-8")))
        self.assertIn("AppTest", mapPolicyCost)
        self.assertIn("AppScenarioTest.EditorScenariosPassOnVulkan", mapPolicyCost["AppTest"])


if __name__ == "__main__":
    unittest.main()
