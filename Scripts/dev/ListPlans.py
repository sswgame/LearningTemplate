#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListPlans.py
@brief 계획 문서(`docs/plans/*.md`)를 머리 줄의 상태 · 영역으로 골라 보입니다(고치지 않는다).

    py -3 -m Scripts plans                      # 표(문서, 상태, 영역, 선행, 백로그, 제목)
    py -3 -m Scripts plans --status 구현중      # 그 상태의 문서만
    py -3 -m Scripts plans --area editor        # 그 영역의 문서만
    py -3 -m Scripts plans --file Vfx           # 그 문서의 머리 줄과 절 제목 목록(`Vfx.md` 도 된다)

머리 줄 모양은 `common/Plans.py` 가 원본이다. 머리 줄이 없거나 틀린 문서가 있으면 건너뛰지 않고 파일 이름과 함께 멈춘다.
"""

from __future__ import annotations

import argparse
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot  # noqa: E402
from common.Plans import PlanDoc, PlanFormatError, kListPlanArea, kListPlanStatus, readPlans  # noqa: E402


def displayWidthInternal(text: str) -> int:
    """터미널에 찍힌 폭 — 한글 같은 전각 글자는 두 칸이다."""
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def formatPlanTable(listPlan: list[PlanDoc]) -> str:
    """문서마다 한 줄: 문서 · 상태 · 영역 · 선행 · 백로그 · 제목. 열 폭을 맞춘다."""
    listRow = [["문서", "상태", "영역", "선행", "백로그", "제목"]]
    for plan in listPlan:
        listRow.append([plan.fileName.removesuffix(".md"), plan.status, plan.area, plan.prerequisite, ",".join(plan.listBacklogId) or "없음", plan.title])
    listWidth = [max(displayWidthInternal(row[column]) for row in listRow) for column in range(len(listRow[0]))]
    return "\n".join("  ".join(cell + " " * (width - displayWidthInternal(cell)) for cell, width in zip(row, listWidth)).rstrip() for row in listRow)


def selectPlans(listPlan: list[PlanDoc], status: str | None, area: str | None) -> list[PlanDoc]:
    """상태 · 영역으로 거른다. 상태 순서(설계 → 끝) → 문서 이름 순으로 늘어놓는다."""
    listSelected = [plan for plan in listPlan if (status is None or plan.status == status) and (area is None or plan.area == area)]
    return sorted(listSelected, key=lambda plan: (kListPlanStatus.index(plan.status), plan.fileName))


def formatPlanDetail(plan: PlanDoc) -> str:
    """한 문서의 머리 줄과 절 제목 목록."""
    return "\n".join([f"<!-- docs/plans/{plan.fileName} -->", plan.headerLine, *plan.listHeading])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="계획 문서(docs/plans)를 머리 줄의 상태 · 영역으로 골라 보인다.")
    parser.add_argument("--status", choices=kListPlanStatus, help="이 상태의 문서만")
    parser.add_argument("--area", choices=kListPlanArea, help="이 영역의 문서만")
    parser.add_argument("--file", dest="fileName", help="이 문서(이름 줄기, 예: Vfx)의 머리 줄과 절 제목 목록")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    try:
        listPlan = readPlans(args.root or getProjectRoot())
    except PlanFormatError as error:
        print(f"[plans] 오류: {error}", file=sys.stderr)
        return 1

    if args.fileName:
        stem = args.fileName.removesuffix(".md").lower()
        listMatch = [plan for plan in listPlan if plan.fileName.removesuffix(".md").lower() == stem]
        if not listMatch:
            print(f"[plans] `{args.fileName}` 문서가 없습니다: {' · '.join(plan.fileName for plan in listPlan)}", file=sys.stderr)
            return 1
        print(formatPlanDetail(listMatch[0]))
        return 0

    listSelected = selectPlans(listPlan, args.status, args.area)
    print(formatPlanTable(listSelected))
    print(f"{len(listSelected)} 건")
    return 0


if __name__ == "__main__":
    sys.exit(main())
