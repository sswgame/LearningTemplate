#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file ListBacklog.py
@brief 영역별 백로그(`docs/backlog/*.md`)의 항목을 상태 · 영역 · 번호로 골라 보입니다(고치지 않는다).

    py -3 -m Scripts backlog                  # 상태별 · 영역별 개수 표
    py -3 -m Scripts backlog --status 결함    # 그 상태의 항목 목록(번호 · 영역 · 제목)
    py -3 -m Scripts backlog --area editor    # 그 파일의 항목 목록
    py -3 -m Scripts backlog --id C-28        # 그 항목의 본문만 — 에이전트가 파일을 통째로 읽지 않고 자기 항목만 읽는다

항목 모양과 상태의 뜻은 `docs/backlog/README.md` 가 원본이다. 개수는 이 명령이 세므로 문서에 숫자를 적지 않는다.
"""

from __future__ import annotations

import argparse
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot  # noqa: E402
from common.Backlog import BacklogItem, kListBacklogStatus, readBacklog  # noqa: E402


def formatCountTable(listItem: list[BacklogItem]) -> str:
    """상태(행) × 영역(열) 개수 표. 빈 상태 행도 남긴다 — "0 건" 이 보여야 한다."""
    listArea = sorted({item.area for item in listItem})
    listRow = [["상태", *listArea, "합계"]]
    for status in kListBacklogStatus:
        listCount = [sum(1 for item in listItem if item.status == status and item.area == area) for area in listArea]
        listRow.append([status, *map(str, listCount), str(sum(listCount))])
    listRow.append(["합계", *(str(sum(1 for item in listItem if item.area == area)) for area in listArea), str(len(listItem))])
    listWidth = [max(displayWidthInternal(row[column]) for row in listRow) for column in range(len(listRow[0]))]

    def padInternal(cell: str, width: int, bRight: bool) -> str:
        padding = " " * (width - displayWidthInternal(cell))
        return padding + cell if bRight else cell + padding

    return "\n".join("  ".join(padInternal(cell, width, index > 0) for index, (cell, width) in enumerate(zip(row, listWidth))).rstrip()
                     for row in listRow)


def displayWidthInternal(text: str) -> int:
    """터미널에 찍힌 폭 — 한글 같은 전각 글자는 두 칸이다."""
    return sum(2 if unicodedata.east_asian_width(ch) in "WF" else 1 for ch in text)


def formatItemList(listItem: list[BacklogItem]) -> str:
    """항목마다 한 줄: 번호 · [상태] · 영역 · 제목."""
    return "\n".join(f"{item.itemId:<7} [{item.status}] {item.area:<22} {item.title}" for item in listItem)


def selectItems(listItem: list[BacklogItem], status: str | None, area: str | None) -> list[BacklogItem]:
    """상태 · 영역으로 거른다. 상태 순서(일을 고르는 순서) → 원래 순서로 늘어놓는다."""
    listSelected = [item for item in listItem if (status is None or item.status == status) and (area is None or item.area == area)]
    return sorted(listSelected, key=lambda item: kListBacklogStatus.index(item.status) if item.status in kListBacklogStatus else len(kListBacklogStatus))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="영역별 백로그(docs/backlog)의 항목을 상태 · 영역 · 번호로 골라 보인다.")
    parser.add_argument("--status", choices=kListBacklogStatus, help="이 상태의 항목 목록")
    parser.add_argument("--area", help="이 영역(파일 이름 줄기, 예: editor)의 항목 목록")
    parser.add_argument("--id", dest="itemId", help="이 번호(예: C-28)의 항목 본문만 찍는다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    listItem, listProblem = readBacklog(args.root or getProjectRoot())
    for problem in listProblem:
        print(f"[backlog] 경고: {problem}", file=sys.stderr)

    if args.itemId:
        listMatch = [item for item in listItem if item.itemId.lower() == args.itemId.lower()]
        if not listMatch:
            print(f"[backlog] `{args.itemId}` 항목이 없습니다 — 끝나서 지웠으면 `git log -S` 로 찾습니다", file=sys.stderr)
            return 1
        for item in listMatch:
            print(f"<!-- docs/backlog/{item.area}.md:{item.lineNumber} -->")
            print(item.text)
        return 0

    if args.status or args.area:
        if args.area and all(item.area != args.area for item in listItem):
            print(f"[backlog] 영역 '{args.area}' 가 없습니다: {' · '.join(sorted({item.area for item in listItem}))}", file=sys.stderr)
            return 1
        listSelected = selectItems(listItem, args.status, args.area)
        if listSelected:
            print(formatItemList(listSelected))
        print(f"{len(listSelected)} 건")
        return 0

    print(formatCountTable(listItem))
    return 0


if __name__ == "__main__":
    sys.exit(main())
