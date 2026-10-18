#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListPlans.py
@brief 계획 문서(`docs/plans/*.md`)를 머리 줄의 상태 · 영역으로 골라 보입니다(고치지 않는다).

    py -3 -m Scripts plans                      # 표(문서, 상태, 진행, 영역, 선행, 옛번호, 제목)
    py -3 -m Scripts plans --status 구현중      # 그 상태의 문서만
    py -3 -m Scripts plans --area editor        # 그 영역의 문서만
    py -3 -m Scripts plans --file Vfx           # 그 문서의 머리 줄과 절 제목 목록(`Vfx.md` 도 된다)
    py -3 -m Scripts plans --file Vfx --section V2   # 제목에 `V2` 가 든 절의 본문만(다음 같은 수준 제목 전까지)
    py -3 -m Scripts plans --find C-30          # 옛 번호가 어느 문서 · 절로 옮겨졌는지(이름이면 위치)
    py -3 -m Scripts plans --check              # 로드맵의 문서 링크 · 파일 · #앵커가 맞는지(틀리면 종료 1)

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
from common.Plans import PlanDoc, PlanFormatError, kListPlanArea, kListPlanStatus, kPlansRelDir, readPlans  # noqa: E402
from common.MarkdownSections import extractSection  # noqa: E402
from common.StatusBoard import checkRoadmap  # noqa: E402


def displayWidthInternal(text: str) -> int:
    """터미널에 찍힌 폭 — 한글 같은 전각 글자는 두 칸이다."""
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def formatPlanTable(listPlan: list[PlanDoc]) -> str:
    """문서마다 한 줄: 문서 · 상태 · 진행 · 영역 · 선행 · 옛번호 · 제목. 열 폭을 맞춘다."""
    listRow = [["문서", "상태", "진행", "영역", "선행", "옛번호", "제목"]]
    for plan in listPlan:
        listRow.append([plan.stem, plan.status, plan.progress, plan.area, plan.prerequisite, ",".join(plan.listOldNumber) or "없음", plan.title])
    listWidth = [max(displayWidthInternal(row[column]) for row in listRow) for column in range(len(listRow[0]))]
    return "\n".join("  ".join(cell + " " * (width - displayWidthInternal(cell)) for cell, width in zip(row, listWidth)).rstrip() for row in listRow)


def selectPlans(listPlan: list[PlanDoc], status: str | None, area: str | None) -> list[PlanDoc]:
    """상태 · 영역으로 거른다. 상태 순서(설계 → 끝) → 문서 이름 순으로 늘어놓는다."""
    listSelected = [plan for plan in listPlan if (status is None or plan.status == status) and (area is None or plan.area == area)]
    return sorted(listSelected, key=lambda plan: (kListPlanStatus.index(plan.status), plan.fileName))


def formatPlanDetail(plan: PlanDoc) -> str:
    """한 문서의 머리 줄과 절 제목 목록."""
    return "\n".join([f"<!-- docs/plans/{plan.fileName} -->", plan.headerLine, *plan.listHeading])


def findPlace(listPlan: list[PlanDoc], query: str) -> list[str]:
    """옛 번호(`C-30`)나 이름(문서 `Vfx` · 단위 `Vfx.V2` · 후보 `Wayland`)이 있는 곳. 모르면 빈 목록."""
    listPlace: list[str] = []
    upper = query.upper()
    for plan in listPlan:
        if upper in plan.listOldNumber:
            listPlace.append(f"{kPlansRelDir}/{plan.fileName} (머리 줄 옛번호)")
        listPlace += [f"{kPlansRelDir}/{plan.fileName}:{candidate.lineNumber} (후보 절 `{candidate.name}` — {candidate.title})"
                      for candidate in plan.listCandidate if upper in candidate.listOldNumber or query == candidate.name]
        if query == plan.stem:
            listPlace.append(f"{kPlansRelDir}/{plan.fileName}")
        elif query.startswith(plan.stem + ".") and len(query) > len(plan.stem) + 1:
            listPlace.append(f"{kPlansRelDir}/{plan.fileName} (단위 `{query[len(plan.stem) + 1:]}` 는 문서 안에서 찾는다)")
    return listPlace


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="계획 문서(docs/plans)를 머리 줄의 상태 · 영역으로 골라 보인다.")
    parser.add_argument("--status", choices=kListPlanStatus, help="이 상태의 문서만")
    parser.add_argument("--area", choices=kListPlanArea, help="이 영역의 문서만")
    parser.add_argument("--file", dest="fileName", help="이 문서(이름 줄기, 예: Vfx)의 머리 줄과 절 제목 목록")
    parser.add_argument("--section", metavar="TITLE", help="--file 과 함께: 제목에 TITLE 이 든 절의 본문만(없거나 여럿에 맞으면 오류와 후보)")
    parser.add_argument("--find", metavar="QUERY", help="옛 번호(C-30) 또는 이름(Vfx · Vfx.V2 · 후보 이름)이 있는 곳")
    parser.add_argument("--check", action="store_true", help="로드맵의 문서 링크 · 파일 · #앵커가 맞는지 본다(틀리면 종료 1)")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    try:
        listPlan = readPlans(args.root or getProjectRoot())
    except PlanFormatError as error:
        print(f"[plans] 오류: {error}", file=sys.stderr)
        return 1

    if args.find:
        listPlace = findPlace(listPlan, args.find)
        if not listPlace:
            print(f"[plans] `{args.find}` 는 옛 번호로도 이름으로도 없습니다", file=sys.stderr)
            return 1
        print("\n".join(listPlace))
        return 0

    if args.check:
        listViolation = checkRoadmap(args.root or getProjectRoot())
        print("\n".join(listViolation))
        print(f"로드맵 위반 {len(listViolation)} 건")
        return 1 if listViolation else 0

    if args.fileName:
        stem = args.fileName.removesuffix(".md").lower()
        listMatch = [plan for plan in listPlan if plan.stem.lower() == stem]
        if not listMatch:
            print(f"[plans] `{args.fileName}` 문서가 없습니다: {' · '.join(plan.fileName for plan in listPlan)}", file=sys.stderr)
            return 1
        if args.section:
            planPath = (args.root or getProjectRoot()) / kPlansRelDir / listMatch[0].fileName
            try:
                print("\n".join(extractSection(planPath.read_text(encoding="utf-8").splitlines(), args.section)))
            except ValueError as error:
                print(f"[plans] 오류: {error}", file=sys.stderr)
                return 1
            return 0
        print(formatPlanDetail(listMatch[0]))
        return 0
    if args.section:
        print("[plans] 오류: --section 은 --file 과 함께 씁니다", file=sys.stderr)
        return 1

    listSelected = selectPlans(listPlan, args.status, args.area)
    print(formatPlanTable(listSelected))
    print(f"{len(listSelected)} 건")
    return 0


if __name__ == "__main__":
    sys.exit(main())
