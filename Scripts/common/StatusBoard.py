#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/StatusBoard.py

최상단 README 의 '지금 진행 상황' 표를 데이터에서 만들고, 로드맵의 링크가 맞는지 본다. 명령(`dev/RunStatusBoard.py` · `dev/ListPlans.py --check`)과
게이트(`CheckStatusBoard`)가 같이 쓴다.

입력은 계획 문서 머리 줄과 후보 절(`common/Plans.py`), 로드맵의 `## 지금 순서` 번호 목록이다.

로드맵 형식 약속: `## 지금 순서` 아래 최상위 번호 목록(`1. …`)이 항목이고, 들여 쓴 줄(하위 불릿)은 그 항목에 속한다.
항목 안의 첫 계획 문서 링크 `[이름](이름.md)` 가 그 항목의 문서다. 링크가 없으면 항목의 첫 줄 글을 업무 칸에 보인다.
링크가 가리키는 문서가 `docs/plans` 에 없으면 오류로 멈춘다(조용히 건너뛰지 않는다).
표에는 상태 `끝` · `참고` · `후보` 문서를 싣지 않는다(후보는 표 아래 한 줄에 개수만). 순위는 로드맵에 나온 번호 순서이고 나오지 않은 문서는 `-` 다.

README 안의 표는 두 표지 사이다: `<!-- status-board:start -->` ~ `<!-- status-board:end -->`. 표지 밖은 건드리지 않는다.
"""

from __future__ import annotations

import difflib
import re
from dataclasses import dataclass
from pathlib import Path

from common.MarkdownAnchor import collectAnchors
from common.Plans import PlanDoc, kCandidateStatus, kListPlanStatus, kPlansRelDir, readPlans

kBoardStart = "<!-- status-board:start -->"
kBoardEnd = "<!-- status-board:end -->"
kRoadmapRelPath = f"{kPlansRelDir}/Roadmap.md"
kReadmeName = "README.md"

#: 표에서 뺄 상태 — 할 일이 없는 문서와 시작 전 후보.
_kHiddenStatus: tuple[str, ...] = ("끝", "참고", kCandidateStatus)

#: 업무 칸에 보이는 문서 없는 항목 글의 최대 길이.
_kLeadMaxLength = 80

_kRoadmapHeading = "## 지금 순서"
_kNumberedRe = re.compile(r"^\d+[.)] ")
_kPlanLinkRe = re.compile(r"\[[^\]]*\]\(([A-Za-z0-9_\-]+)\.md(?:#[^)]*)?\)")
_kLinkRe = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
_kFenceRe = re.compile(r"^\s*(```|~~~)")


class StatusBoardError(ValueError):
    """로드맵 · 표지 · 문서 이름이 약속과 다르다. 메시지는 `파일: 내용` 이다."""


@dataclass
class RoadmapEntry:
    """로드맵 항목 하나 — 문서 이름 줄기(없으면 None) · 첫 줄 글 · 시작 줄 번호."""

    stem: str | None
    lead: str
    lineNumber: int


def parseRoadmap(text: str, fileName: str = "Roadmap.md") -> list[RoadmapEntry]:
    """`## 지금 순서` 아래 최상위 번호 항목들. 절이 없으면 오류."""
    listEntry: list[RoadmapEntry] = []
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
            first = index
            listBlock = [line]
            while index + 1 < len(listLine) and listLine[index + 1].startswith(" "):
                index += 1
                listBlock.append(listLine[index])
            match = _kPlanLinkRe.search("\n".join(listBlock))
            listEntry.append(RoadmapEntry(match.group(1) if match else None, leadTextInternal(line), first + 1))
        index += 1
    if not bFound:
        raise StatusBoardError(f"{fileName}: `{_kRoadmapHeading}` 절이 없습니다")
    return listEntry


def leadTextInternal(line: str) -> str:
    """번호 목록 첫 줄에서 번호 · 강조 · 백틱을 걷고 80자로 자른 글."""
    text = _kNumberedRe.sub("", line, count=1).replace("**", "").replace("`", "")
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text).strip()
    return text if len(text) <= _kLeadMaxLength else text[:_kLeadMaxLength - 1].rstrip() + "…"


def escapeCellInternal(text: str) -> str:
    return text.replace("|", "\\|")


def renderBoard(listPlan: list[PlanDoc], listEntry: list[RoadmapEntry]) -> str:
    """표 + 후보 개수 한 줄. 로드맵이 가리킨 문서가 계획 문서에 없으면 `StatusBoardError`."""
    mapPlan = {plan.stem: plan for plan in listPlan}
    listRow: list[tuple[str, str, PlanDoc | None]] = []
    setRanked: set[str] = set()
    for rank, entry in enumerate(listEntry, start=1):
        if entry.stem is None:
            listRow.append((str(rank), entry.lead, None))
        elif entry.stem not in mapPlan:
            raise StatusBoardError(f"{kRoadmapRelPath}:{entry.lineNumber}: 순위 {rank} 의 문서 `{entry.stem}.md` 가 {kPlansRelDir} 에 없습니다")
        else:
            setRanked.add(entry.stem)
            if mapPlan[entry.stem].status not in _kHiddenStatus:
                listRow.append((str(rank), "", mapPlan[entry.stem]))
    listRest = sorted((plan for plan in listPlan if plan.stem not in setRanked and plan.status not in _kHiddenStatus),
                      key=lambda plan: (kListPlanStatus.index(plan.status), plan.fileName))
    listRow += [("-", "", plan) for plan in listRest]

    listLine = ["| 순위 | 업무(계획 문서) | 상태 | 진행 | 선행 | 영역 | 옛번호 |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for rank, lead, plan in listRow:
        if plan is None:
            listLine.append(f"| {rank} | {escapeCellInternal(lead)} | | | | | |")
            continue
        listLine.append(f"| {rank} | [{plan.stem}]({kPlansRelDir}/{plan.fileName}) — {escapeCellInternal(plan.title)} | {plan.status} | {plan.progress} "
                        f"| {escapeCellInternal(plan.prerequisite)} | {plan.area} | {', '.join(plan.listOldNumber) or '없음'} |")
    listCandidateDoc = [plan for plan in listPlan if plan.status == kCandidateStatus]
    listLine += ["", f"후보 문서 {len(listCandidateDoc)}개 · 후보 절 {sum(len(plan.listCandidate) for plan in listCandidateDoc)}개"]
    return "\n".join(listLine)


def buildBoard(repositoryRoot: Path) -> str:
    """저장소의 계획 문서 · 로드맵에서 표를 만든다. 로드맵 파일이 없으면 오류."""
    roadmapPath = repositoryRoot / kRoadmapRelPath
    if not roadmapPath.is_file():
        raise StatusBoardError(f"{kRoadmapRelPath}: 파일이 없습니다")
    listEntry = parseRoadmap(roadmapPath.read_text(encoding="utf-8"), kRoadmapRelPath)
    return renderBoard(readPlans(repositoryRoot), listEntry)


def checkRoadmap(repositoryRoot: Path) -> list[str]:
    """로드맵을 점검한다: (a) `## 지금 순서` 의 문서 링크가 계획 문서를 가리키는가 (b) 모든 마크다운 링크의 파일 · `#앵커` 가 있는가
    (c) 진행 `n/n` 인데 구현 중인 문서 · 상태 `끝` 인데 진행 `n/m` 인 문서가 없는가.
    위반 줄(`파일:줄: 내용`)들. 계획 문서 머리 줄이 틀리면 `PlanFormatError`."""
    roadmapPath = repositoryRoot / kRoadmapRelPath
    if not roadmapPath.is_file():
        return [f"{kRoadmapRelPath}: 파일이 없습니다"]
    text = roadmapPath.read_text(encoding="utf-8")
    listViolation: list[str] = []
    setStem = {plan.stem for plan in readPlans(repositoryRoot)}
    for entry in parseRoadmap(text, kRoadmapRelPath):
        if entry.stem is not None and entry.stem not in setStem:
            listViolation.append(f"{kRoadmapRelPath}:{entry.lineNumber}: 항목의 문서 `{entry.stem}.md` 가 {kPlansRelDir} 에 계획 문서로 없습니다")
    listViolation += checkFinishedPlanLinksInternal(text, readPlans(repositoryRoot))
    listViolation += checkAllUnitsDoneStatusInternal(readPlans(repositoryRoot))
    listViolation += checkFinishedStatusProgressInternal(readPlans(repositoryRoot))
    mapAnchorCache: dict[Path, set[str]] = {}
    bFence = False
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        if _kFenceRe.match(line):
            bFence = not bFence
        if bFence:
            continue
        for target in _kLinkRe.findall(line):
            if re.match(r"^[a-z][a-z0-9+.\-]*:", target):
                continue
            pathText, _, anchor = target.partition("#")
            path = (roadmapPath.parent / pathText).resolve() if pathText else roadmapPath
            where = f"{kRoadmapRelPath}:{lineNumber}"
            if not path.exists():
                listViolation.append(f"{where}: 링크 `{target}` 의 파일이 없습니다")
            elif anchor and path.suffix == ".md":
                if path not in mapAnchorCache:
                    mapAnchorCache[path] = collectAnchors(path.read_text(encoding="utf-8"))
                if anchor.lower() not in mapAnchorCache[path]:
                    listViolation.append(f"{where}: 링크 `{target}` 의 제목(앵커)이 {path.name} 에 없습니다")
    return listViolation


def checkFinishedPlanLinksInternal(text: str, listPlan: list[PlanDoc]) -> list[str]:
    """`## 지금 순서` 안의 모든 줄에서 머리 줄 상태가 `끝` 인 계획 문서를 가리키는 링크를 위반으로 모은다(끝난 일을 로드맵에 남기면 같은 일이 다시 배정된다).
    전부 지워진 후보 절은 아래 앵커 검사가 잡는다."""
    setFinished = {plan.stem for plan in listPlan if plan.status == "끝"}
    listViolation: list[str] = []
    bInSection = False
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        if line.startswith("## "):
            bInSection = line.strip() == _kRoadmapHeading
        elif bInSection:
            for stem in dict.fromkeys(_kPlanLinkRe.findall(line)):
                if stem in setFinished:
                    listViolation.append(f"{kRoadmapRelPath}:{lineNumber}: 문서 `{stem}.md` 의 상태가 `끝` 입니다 — 끝난 단위는 로드맵에서 지웁니다")
    return listViolation


#: 단위를 아직 만드는 중이라는 뜻의 상태. 진행이 `n/n`(모든 단위가 끝)인데 이 상태면 문서가 낡은 것이다.
_kBuildingStatus: tuple[str, ...] = ("설계", "구현대기", "구현중")
_kAllDoneProgressRe = re.compile(r"^(\d+)/\1$")


def checkAllUnitsDoneStatusInternal(listPlan: list[PlanDoc]) -> list[str]:
    """진행이 `n/n` 인데 상태가 `설계` · `구현대기` · `구현중` 인 계획 문서를 위반으로 모은다.
    마지막 단위를 끝낸 사람이 상태를 올리지 않으면 끝난 일이 README 진행 표에 구현 중으로 쌓인다. 실행 확인(GPU · 측정)이 남았으면 `측정대기`, 다 끝났으면 `끝`."""
    return [f"{kPlansRelDir}/{plan.fileName}: 진행이 {plan.progress} 로 모든 단위가 끝났는데 상태가 `{plan.status}` 입니다 — 확인이 남았으면 `측정대기`, 끝났으면 `끝`"
            for plan in listPlan if plan.status in _kBuildingStatus and _kAllDoneProgressRe.match(plan.progress)]


_kProgressRe = re.compile(r"^(\d+)/(\d+)$")


def checkFinishedStatusProgressInternal(listPlan: list[PlanDoc]) -> list[str]:
    """상태가 `끝` 인데 진행이 `n/m`(n < m)인 계획 문서를 위반으로 모은다. 남은 단위가 있는 문서를 `끝` 으로 두면 로드맵에서 빠져 남은 단위를 아무도 집지 않는다.
    단위는 있는데 실행 확인만 남았으면 그 단위를 끝으로 세지 않고 상태를 `측정대기` 로 둔다."""
    listViolation: list[str] = []
    for plan in listPlan:
        match = _kProgressRe.match(plan.progress)
        if plan.status == "끝" and match is not None and match.group(1) != match.group(2):
            listViolation.append(f"{kPlansRelDir}/{plan.fileName}: 상태가 `끝` 인데 진행이 {plan.progress} 입니다 — 남은 단위를 끝내거나(실행 확인만 남았으면 상태를 `측정대기`)")
    return listViolation


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
