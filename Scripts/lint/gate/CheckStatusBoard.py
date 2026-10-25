#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckStatusBoard.py

저장소 루트 StatusBoard.md 의 '지금 진행 상황' 표(`<!-- status-board:start -->` ~ `<!-- status-board:end -->` 사이)가 계획 문서 머리 줄 ·
로드맵 · 후보 문서에서 만든 결과와 같은지 검사합니다. 표는 `py -3 -m Scripts status-board --write` 로 채웁니다(통합 담당만).
표가 README 에 있던 때는 계획 문서를 고친 모든 브랜치가 README 를 다시 만들어 병합마다 충돌했습니다. 지금은 계획 문서 커밋에서 로드맵 점검만 하고,
표 신선도는 표 파일이 staged 됐을 때와 전체 검사에서만 봅니다.

게이트를 두는 이유: 생성물은 원본(머리 줄 · 로드맵 · 후보 문서)이 바뀐 뒤 다시 만들지 않으면 낡는데, 이것을 형식으로 막을 방법이 없다.
같은 게이트가 `plans --check` 의 로드맵 점검(링크 · 앵커, 진행 `n/n` 인데 구현 중, 상태 `끝` 인데 진행 `n/m`, 상태 `끝` 인 문서가 남음)도 한다 —
계획 문서를 고치는 커밋에서 낡은 상태가 바로 잡힌다.
계획 문서 머리 줄이 틀리거나 로드맵이 가리키는 문서가 없을 때도 같은 오류로 멈춘다(건너뛰지 않는다).
같은 모양으로, 진행이 n/n 인데 상태가 구현중인 문서도 막는다(마지막 단위를 끝낸 사람이 상태를 올리지 않아 쌓이는 것을 `docs/plans/*` 커밋에서 잡는다).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Plans import PlanFormatError  # noqa: E402
from common.StatusBoard import StatusBoardError, buildBoard, checkRoadmap, diffBoard, kBoardFileName  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402

_kPlanHeader = "# 계획\n\n> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 1/2\n"
_kRoadmap = "# 로드맵\n\n## 지금 순서\n\n1. [계획](A.md) 먼저.\n"
_kStaleBoard = "# R\n\n<!-- status-board:start -->\n| 낡은 표 |\n<!-- status-board:end -->\n"


class CheckStatusBoardGate(LintGate):
    """StatusBoard.md 의 진행 상황 표가 계획 문서 머리 줄 · 로드맵 · 후보 문서에서 만든 결과와 같은지."""

    description = "StatusBoard.md 의 '지금 진행 상황' 표가 계획 문서 · 로드맵 · 후보 문서에서 만든 결과와 같은지 검사"
    buildComment = "Checking StatusBoard.md is up to date..."
    timeoutSeconds = 30
    preCommitPattern = ("StatusBoard.md", "docs/plans/*")
    preCommitFileArgument = "--files"
    violationHeader = "진행 상황 표 위반"
    hint = "  표를 손으로 고치지 말고 `py -3 -m Scripts status-board --write` 로 다시 만듭니다(통합 담당만)."
    selfTestCases = [
        {
            "name": "낡은 표",
            "expectedViolation": "StatusBoard.md: 표가 낡았습니다",
            "files": {"StatusBoard.md": _kStaleBoard, "docs/plans/A.md": _kPlanHeader, "docs/plans/Roadmap.md": _kRoadmap},
        },
        {
            "name": "머리 줄이 없는 계획 문서",
            "expectedViolation": "A.md: 둘째 줄이 머리 줄 모양이 아닙니다",
            "files": {"StatusBoard.md": _kStaleBoard, "docs/plans/A.md": "# 계획\n\n본문\n", "docs/plans/Roadmap.md": _kRoadmap},
        },
        {
            "name": "진행 n/n 인데 구현중",
            "expectedViolation": "진행이 2/2 로 모든 단위가 끝났는데 상태가 `구현중` 입니다",
            "files": {"StatusBoard.md": _kStaleBoard, "docs/plans/A.md": _kPlanHeader.replace("1/2", "2/2"), "docs/plans/Roadmap.md": _kRoadmap},
        },
        {
            "name": "상태 끝인데 진행 n/m",
            "expectedViolation": "상태가 `끝` 인데 진행이 1/2 입니다",
            "files": {"StatusBoard.md": _kStaleBoard, "docs/plans/A.md": _kPlanHeader.replace("구현중", "끝"), "docs/plans/Roadmap.md": "# 로드맵\n\n## 지금 순서\n\n1. 항목.\n"},
        },
        {
            "name": "상태 끝인 문서가 남음",
            "expectedViolation": "상태가 `끝` 인 계획 문서는 지웁니다",
            "files": {"StatusBoard.md": _kStaleBoard, "docs/plans/A.md": _kPlanHeader.replace("구현중", "끝").replace("1/2", "2/2"), "docs/plans/Roadmap.md": "# 로드맵\n\n## 지금 순서\n\n1. 항목.\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "커밋 훅이 넘기는 staged 파일 (생략 시 표 신선도까지 전부 검사)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 훅이 계획 문서만 staged 라고 넘기면 표 신선도는 보지 않는다 — 표는 병합 뒤 통합 담당이 다시 만들므로 작업자가 만들게 하면
        # 브랜치마다 같은 파일이 바뀌어 병합이 충돌한다. 표 파일이 staged 됐거나 전체 검사(`ctest -L lint`)일 때만 낡음을 막는다.
        listStaged = getattr(args, "files", None) or []
        bCheckBoard = not listStaged or any(Path(name).name == kBoardFileName for name in listStaged)
        board = repositoryRoot / kBoardFileName
        if bCheckBoard and not board.is_file():
            return GateResult(listViolation=[f"{kBoardFileName}: 파일이 없습니다"])
        try:
            listDiff = diffBoard(board.read_text(encoding="utf-8"), buildBoard(repositoryRoot), kBoardFileName) if bCheckBoard else []
            listRoadmapViolation = checkRoadmap(repositoryRoot)
        except (PlanFormatError, StatusBoardError) as error:
            return GateResult(listViolation=[str(error)])
        listViolation = list(listRoadmapViolation)
        if listDiff:
            listViolation += [f"{kBoardFileName}: 표가 낡았습니다", *listDiff]
        if listViolation:
            return GateResult(listViolation=listViolation)
        return GateResult(summary="StatusBoard.md 진행 상황 표가 최신, 로드맵 점검 통과")


main = CheckStatusBoardGate.run


if __name__ == "__main__":
    sys.exit(main())
