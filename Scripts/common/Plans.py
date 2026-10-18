#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/Plans.py

계획 문서(`docs/plans/*.md`)의 머리 줄을 읽는다. 목록 명령(`dev/ListPlans.py`)이 쓴다. 머리 줄 해석은 `parsePlanText` 한 곳에만 둔다.

문서의 둘째 비어 있지 않은 줄(제목 `# ...` 다음 줄)이 정확히 이 모양이다:

    > 상태: <설계|구현대기|구현중|측정대기|보류|참고|끝> · 선행: <없음 또는 문서 이름/단위> · 영역: <영역> · 백로그: <ID,ID 또는 없음>

머리 줄이 없거나 모양이 틀리면 건너뛰지 않고 `PlanFormatError` 로 멈춘다(파일 이름을 담는다).
안내 · 규칙 문서(`kPlanNonDocumentName`)는 계획 문서가 아니다.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

#: 계획 폴더(저장소 기준).
kPlansRelDir = "docs/plans"

#: 계획 문서가 아닌 파일 — 안내 · 인계 · 규칙 · 로드맵.
kPlanNonDocumentName: tuple[str, ...] = ("README.md", "NEXT.md", "WorkerRules.md", "Roadmap.md")

#: 상태 값 — 진행 순서대로. `보류` 는 사용자가 나중으로 정한 것, `참고` 는 할 일 없는 참고 문서.
kListPlanStatus: tuple[str, ...] = ("설계", "구현대기", "구현중", "측정대기", "보류", "참고", "끝")

#: 영역 값.
kListPlanArea: tuple[str, ...] = ("engine", "graphics", "editor", "gameplay", "audio-ai-localization", "ui", "network", "quality", "project")

_kNone = "없음"
_kHeaderRe = re.compile(r"^> 상태: (.+?) · 선행: (.+?) · 영역: (.+?) · 백로그: (.+)$")
_kBacklogIdRe = re.compile(r"^[A-Z]\d*-\d+$")
_kHeadingRe = re.compile(r"^(#{1,6}) +(.+?)\s*$")
_kFenceRe = re.compile(r"^\s*(```|~~~)")


class PlanFormatError(ValueError):
    """머리 줄이 없거나 모양이 틀린 계획 문서. 메시지는 `파일: 내용` 이다."""


@dataclass
class PlanDoc:
    """계획 문서 하나 — 파일 이름 · 제목 · 머리 줄 값 · 머리 줄 원문 · 절 제목들."""

    fileName: str
    title: str
    status: str
    prerequisite: str
    area: str
    listBacklogId: list[str]
    headerLine: str
    listHeading: list[str]


def parsePlanText(text: str, fileName: str) -> PlanDoc:
    """글 하나를 계획 문서로 읽는다. 머리 줄이 없거나 값이 틀리면 `PlanFormatError`."""
    listLine = text.splitlines()
    listNonBlank = [line for line in listLine if line.strip()]
    if len(listNonBlank) < 2 or not listNonBlank[0].startswith("# "):
        raise PlanFormatError(f"{fileName}: 첫 비어 있지 않은 줄이 `# 제목` 이 아닙니다")
    headerLine = listNonBlank[1]
    match = _kHeaderRe.match(headerLine)
    if match is None:
        raise PlanFormatError(f"{fileName}: 둘째 줄이 머리 줄 모양이 아닙니다 — `> 상태: … · 선행: … · 영역: … · 백로그: …` 이어야 합니다: {headerLine}")
    status, prerequisite, area, backlog = match.groups()
    if status not in kListPlanStatus:
        raise PlanFormatError(f"{fileName}: 상태 '{status}' 는 허용값({' · '.join(kListPlanStatus)})이 아닙니다")
    if area not in kListPlanArea:
        raise PlanFormatError(f"{fileName}: 영역 '{area}' 는 허용값({' · '.join(kListPlanArea)})이 아닙니다")
    listBacklogId = [] if backlog == _kNone else backlog.split(",")
    for backlogId in listBacklogId:
        if not _kBacklogIdRe.match(backlogId):
            raise PlanFormatError(f"{fileName}: 백로그 '{backlog}' 는 `ID,ID`(공백 없이) 또는 `없음` 이어야 합니다")
    return PlanDoc(fileName, listNonBlank[0][2:].strip(), status, prerequisite, area, listBacklogId, headerLine, listHeadingInternal(listLine))


def listHeadingInternal(listLine: list[str]) -> list[str]:
    """절 제목 줄들(코드 울타리 안은 뺀다). 문서 제목 `# ...` 도 든다."""
    listHeading: list[str] = []
    bFence = False
    for line in listLine:
        if _kFenceRe.match(line):
            bFence = not bFence
        elif not bFence and _kHeadingRe.match(line):
            listHeading.append(line.rstrip())
    return listHeading


def listPlanFiles(repositoryRoot: Path) -> list[Path]:
    """계획 문서 파일 전부(안내 · 규칙 문서는 뺀다), 이름 순."""
    return sorted(path for path in (repositoryRoot / kPlansRelDir).glob("*.md") if path.name not in kPlanNonDocumentName)


def readPlans(repositoryRoot: Path) -> list[PlanDoc]:
    """모든 계획 문서를 읽는다. 하나라도 모양이 틀리면 `PlanFormatError`."""
    return [parsePlanText(path.read_text(encoding="utf-8"), path.name) for path in listPlanFiles(repositoryRoot)]
