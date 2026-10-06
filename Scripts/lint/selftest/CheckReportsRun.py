#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/selftest/CheckReportsRun.py

`report/` 의 보고서가 아직 뜨는지 본다 — 보고서는 CI 가 돌리지 않아 import 단계에서 깨진 채 아무도 모를 수 있다.
보고서는 "맞는 출력" 이 정해져 있지 않아 조각을 들지 않는다. 대신 모든 보고서가 `LintReport` 하위 클래스이고(`main = XxxReport.run`)
`--help` 로 0 을 내는지 본다(`CheckLintsAreAlive` · `CheckFixersAreAlive` 의 짝).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · LintCatalog · LintReport
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import mapConcurrent, runProcess  # noqa: E402
from LintCatalog import discoverLintScripts  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402
from LintReport import findReportClass  # noqa: E402


class CheckReportsRunGate(LintGate):
    description = "report/ 의 보고서가 모두 LintReport 이고 --help 로 뜨는지"
    buildComment = "Checking that every report script still starts..."
    timeoutSeconds = 120
    violationHeader = "뜨지 않는 보고서"
    selfTestSkipReason = "린트를 보는 린트 — 대상은 report/ 폴더 자체다"
    preCommitSkipReason = "셀프테스트는 훅에서 돌지 않는다(ctest -L lint · CI)"

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listScript = discoverLintScripts("report")
        if not listScript:
            raise GateError("Scripts/lint/report/ 에 보고서가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")
        listViolation = [f"{script.relPath}: LintReport 하위 클래스가 없다 — report/ 의 스크립트는 보고서다(`main = XxxReport.run`)"
                         for script in listScript if findReportClass(script.module) is None]

        def runHelpInternal(script) -> str:
            result = runProcess([sys.executable, script.scriptPath, "--help"], timeoutSeconds=60)
            if result.bSucceeded:
                return ""
            return f"{script.relPath}: --help 가 {result.returnCode} 로 끝났다 — {result.stderr.strip()[-200:]}"

        listViolation += [message for message in mapConcurrent(runHelpInternal, listScript) if message]
        return GateResult(listViolation=listViolation, summary=f"{len(listScript)} reports start")


main = CheckReportsRunGate.run

if __name__ == "__main__":
    sys.exit(main())
