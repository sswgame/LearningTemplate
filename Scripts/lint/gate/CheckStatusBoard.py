#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckStatusBoard.py

최상단 README.md 의 '지금 진행 상황' 표(`<!-- status-board:start -->` ~ `<!-- status-board:end -->` 사이)가 계획 문서 머리 줄 ·
로드맵 · 후보 문서에서 만든 결과와 같은지 검사합니다. 표는 `py -3 -m Scripts status-board --write README.md` 로 채웁니다.

게이트를 두는 이유: 생성물은 원본(머리 줄 · 로드맵 · 후보 문서)이 바뀐 뒤 다시 만들지 않으면 낡는데, 이것을 형식으로 막을 방법이 없다.
계획 문서 머리 줄이 틀리거나 로드맵이 가리키는 문서가 없을 때도 같은 오류로 멈춘다(건너뛰지 않는다).
같은 모양으로, 진행이 n/n 인데 상태가 구현중인 문서도 막는다(마지막 단위를 끝낸 사람이 상태를 올리지 않아 쌓이는 것을 `docs/plans/*` 커밋에서 잡는다).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Plans import PlanFormatError, readPlans  # noqa: E402
from common.StatusBoard import StatusBoardError, buildBoard, checkAllUnitsDoneStatus, diffBoard, kReadmeName  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402

_kPlanHeader = "# 계획\n\n> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 1/2\n"
_kRoadmap = "# 로드맵\n\n## 지금 순서\n\n1. [계획](A.md) 먼저.\n"
_kStaleReadme = "# R\n\n<!-- status-board:start -->\n| 낡은 표 |\n<!-- status-board:end -->\n"


class CheckStatusBoardGate(LintGate):
    """README.md 의 진행 상황 표가 계획 문서 머리 줄 · 로드맵 · 후보 문서에서 만든 결과와 같은지."""

    description = "README.md 의 '지금 진행 상황' 표가 계획 문서 · 로드맵 · 후보 문서에서 만든 결과와 같은지 검사"
    buildComment = "Checking README status board is up to date..."
    timeoutSeconds = 30
    preCommitPattern = ("README.md", "docs/plans/*")
    preCommitFileArgument = ""
    violationHeader = "진행 상황 표 위반"
    hint = "  표를 손으로 고치지 말고 `py -3 -m Scripts status-board --write README.md` 로 다시 만듭니다."
    selfTestCases = [
        {
            "name": "낡은 표",
            "expectedViolation": "README.md: 표가 낡았습니다",
            "files": {"README.md": _kStaleReadme, "docs/plans/A.md": _kPlanHeader, "docs/plans/Roadmap.md": _kRoadmap},
        },
        {
            "name": "머리 줄이 없는 계획 문서",
            "expectedViolation": "A.md: 둘째 줄이 머리 줄 모양이 아닙니다",
            "files": {"README.md": _kStaleReadme, "docs/plans/A.md": "# 계획\n\n본문\n", "docs/plans/Roadmap.md": _kRoadmap},
        },
        {
            "name": "모든 단위가 끝났는데 구현중",
            "expectedViolation": "모든 단위가 끝났는데 상태가 `구현중` 입니다",
            "files": {"README.md": _kStaleReadme, "docs/plans/A.md": _kPlanHeader.replace("1/2", "2/2"), "docs/plans/Roadmap.md": _kRoadmap},
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        readme = repositoryRoot / kReadmeName
        if not readme.is_file():
            return GateResult(listViolation=[f"{kReadmeName}: 파일이 없습니다"])
        try:
            listDiff = diffBoard(readme.read_text(encoding="utf-8"), buildBoard(repositoryRoot), kReadmeName)
            listDone = checkAllUnitsDoneStatus(readPlans(repositoryRoot))
        except (PlanFormatError, StatusBoardError) as error:
            return GateResult(listViolation=[str(error)])
        listViolation = [*listDone]
        if listDiff:
            listViolation += [f"{kReadmeName}: 표가 낡았습니다", *listDiff]
        if listViolation:
            return GateResult(listViolation=listViolation)
        return GateResult(summary="README 진행 상황 표가 최신, 모든 단위가 끝난 문서의 상태도 맞음")


main = CheckStatusBoardGate.run


if __name__ == "__main__":
    sys.exit(main())
