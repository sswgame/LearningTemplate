#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/StatusBoard.py

최상단 README 의 '지금 진행 상황' 표를 데이터에서 만든다. 명령(`dev/RunStatusBoard.py`)과 게이트(`CheckStatusBoard`)가 같이 쓴다.

입력은 셋이다: 계획 문서 머리 줄(`common/Plans.py`), 로드맵의 `## 지금 순서` 번호 목록, 백로그 상태별 개수(`common/Backlog.py`).

로드맵 형식 약속: `## 지금 순서` 아래 번호 목록(`1. …`)의 각 항목에서 첫 계획 문서 링크 `[이름](이름.md)` 가 그 항목의 문서다.
링크가 없는 항목은 표에 `(문서 없음)` 한 줄이 된다. 링크가 가리키는 문서가 `docs/plans` 에 없으면 오류로 멈춘다(조용히 건너뛰지 않는다).
표에는 상태 `끝` · `참고` 문서를 싣지 않는다. 순위는 로드맵에 나온 번호 순서이고 나오지 않은 문서는 `-` 다.

README 안의 표는 두 표지 사이다: `<!-- status-board:start -->` ~ `<!-- status-board:end -->`. 표지 밖은 건드리지 않는다.
"""

from __future__ import annotations

import difflib
import re
from pathlib import Path

from common.Backlog import kListBacklogStatus, readBacklog
from common.Plans import PlanDoc, kListPlanStatus, kPlansRelDir, readPlans

kBoardStart = "<!-- status-board:start -->"
kBoardEnd = "<!-- status-board:end -->"
kRoadmapRelPath = f"{kPlansRelDir}/Roadmap.md"
kReadmeName = "README.md"

#: 표에서 뺄 상태 — 할 일이 없는 문서.
_kHiddenStatus: tuple[str, ...] = ("끝", "참고")

_kRoadmapHeading = "## 지금 순서"
_kNumberedRe = re.compile(r"^\s*\d+[.)] ")
_kPlanLinkRe = re.compile(r"\[[^\]]*\]\(([A-Za-z0-9_\-]+)\.md(?:#[^)]*)?\)")
_kNoDocument = "(문서 없음)"


class StatusBoardError(ValueError):
    """로드맵 · 표지 · 문서 이름이 약속과 다르다. 메시지는 `파일: 내용` 이다."""


def parseRoadmapOrder(text: str, fileName: str = "Roadmap.md") -> list[str | None]:
    """`## 지금 순서` 아래 번호 항목마다 첫 계획 문서 링크의 이름 줄기(없으면 None). 절이 없으면 오류."""
    listOrder: list[str | None] = []
    bInSection = False
    bFound = False
    listLine = text.splitlines()
    index = 0
    while index < len(listLine):
        line = listLine[index]
        if line.startswith("## "):
            bInSection = line.strip() == _kRoadmapHeading
            bFound = bFound or bInSection
        elif bInSection and _kNumberedRe.match(line):
            # 항목은 다음 번호 줄 · 제목 줄 전까지의 들여 쓴 이어지는 줄을 포함한다.
            listEntry = [line]
            while index + 1 < len(listLine) and listLine[index + 1].startswith(" ") and not _kNumberedRe.match(listLine[index + 1]):
                index += 1
                listEntry.append(listLine[index])
            match = _kPlanLinkRe.search("\n".join(listEntry))
            listOrder.append(match.group(1) if match else None)
        index += 1
    if not bFound:
        raise StatusBoardError(f"{fileName}: `{_kRoadmapHeading}` 절이 없습니다")
    return listOrder


def renderBoard(listPlan: list[PlanDoc], listOrder: list[str | None], mapBacklogCount: dict[str, int]) -> str:
    """표 + 백로그 상태별 개수 한 줄. 로드맵이 가리킨 문서가 계획 문서에 없으면 `StatusBoardError`."""
    mapPlan = {plan.fileName.removesuffix(".md"): plan for plan in listPlan}
    listRow: list[tuple[str, PlanDoc | None]] = []
    setRanked: set[str] = set()
    for rank, stem in enumerate(listOrder, start=1):
        if stem is None:
            listRow.append((str(rank), None))
        elif stem not in mapPlan:
            raise StatusBoardError(f"{kRoadmapRelPath}: 순위 {rank} 의 문서 `{stem}.md` 가 {kPlansRelDir} 에 없습니다")
        else:
            setRanked.add(stem)
            if mapPlan[stem].status not in _kHiddenStatus:
                listRow.append((str(rank), mapPlan[stem]))
    listRest = sorted((plan for stem, plan in mapPlan.items() if stem not in setRanked and plan.status not in _kHiddenStatus),
                      key=lambda plan: (kListPlanStatus.index(plan.status), plan.fileName))
    listRow += [("-", plan) for plan in listRest]

    listLine = ["| 순위 | 업무(계획 문서) | 상태 | 진행 | 선행 | 영역 | 백로그 |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for rank, plan in listRow:
        if plan is None:
            listLine.append(f"| {rank} | {_kNoDocument} | | | | | |")
            continue
        title = plan.title.replace("|", "\\|")
        listLine.append(f"| {rank} | [{title}]({kPlansRelDir}/{plan.fileName}) | {plan.status} | {plan.progress} | {plan.prerequisite.replace('|', chr(92) + '|')} "
                        f"| {plan.area} | {', '.join(plan.listBacklogId) or '없음'} |")
    count = " · ".join(f"{status} {mapBacklogCount.get(status, 0)}" for status in kListBacklogStatus)
    listLine += ["", f"백로그 {sum(mapBacklogCount.values())}건: {count}"]
    return "\n".join(listLine)


def buildBoard(repositoryRoot: Path) -> str:
    """저장소의 계획 문서 · 로드맵 · 백로그에서 표를 만든다. 로드맵 파일이 없으면 오류."""
    roadmapPath = repositoryRoot / kRoadmapRelPath
    if not roadmapPath.is_file():
        raise StatusBoardError(f"{kRoadmapRelPath}: 파일이 없습니다")
    listOrder = parseRoadmapOrder(roadmapPath.read_text(encoding="utf-8"), kRoadmapRelPath)
    listItem, _ = readBacklog(repositoryRoot)
    mapCount = {status: sum(1 for item in listItem if item.status == status) for status in kListBacklogStatus}
    return renderBoard(readPlans(repositoryRoot), listOrder, mapCount)


def splitAtMarkers(text: str, fileName: str) -> tuple[str, str, str]:
    """글을 (표지 앞 + 시작 표지, 표지 사이, 끝 표지 + 뒤)로 가른다. 표지가 없거나 순서가 틀리면 오류."""
    start = text.find(kBoardStart)
    end = text.find(kBoardEnd)
    if start < 0 or end < 0 or end < start or text.count(kBoardStart) != 1 or text.count(kBoardEnd) != 1:
        raise StatusBoardError(f"{fileName}: `{kBoardStart}` ~ `{kBoardEnd}` 표지가 하나씩 이 순서로 있어야 합니다")
    return text[:start + len(kBoardStart)], text[start + len(kBoardStart):end], text[end:]


def replaceBoard(text: str, board: str, fileName: str) -> str:
    """표지 사이를 `board` 로 바꾼 글."""
    head, _, tail = splitAtMarkers(text, fileName)
    return f"{head}\n{board}\n{tail}"


def diffBoard(text: str, board: str, fileName: str) -> list[str]:
    """표지 사이와 `board` 의 차이(unified diff 줄). 같으면 빈 목록."""
    _, between, _ = splitAtMarkers(text, fileName)
    return list(difflib.unified_diff(between.strip("\n").splitlines(), board.splitlines(), f"{fileName} (현재)", f"{fileName} (생성)", lineterm=""))
