#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/Backlog.py

백로그(`docs/backlog/<영역>.md`)를 항목 단위로 읽는다. 목록 명령(`dev/ListBacklog.py`)과 게이트(`lint/gate/CheckBacklog.py`)가 같은 규칙으로 읽도록 한 곳에 둔다.

항목은 ``- `ID` [상태] **제목.** …`` 으로 시작하는 줄과, 그 뒤의 두 칸 들여 쓴 줄들이다. 들여 쓰지 않은 줄(빈 줄 · 제목)이 오면 항목이 끝난다.
안내 문서(`README.md`)는 항목 파일이 아니다. 형식과 상태의 뜻은 `docs/backlog/README.md` 가 원본이다.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

#: 백로그 폴더(저장소 기준).
kBacklogRelDir = "docs/backlog"

#: 상태 태그 — 일을 고르는 순서대로. 옛 분류 문자 A ~ H 와 같은 순서다.
kListBacklogStatus: tuple[str, ...] = ("결함", "검증대기", "정리", "기능", "관찰", "결정대기", "조건부", "보류")

_kItemStartRe = re.compile(r"^- `([A-Z]\d*-\d+)` ")
_kItemHeadRe = re.compile(r"^- `([A-Z]\d*-\d+)` \[([^\]]*)\] (.*)$")
_kTitleRe = re.compile(r"\*\*(.+?)\*\*")


@dataclass
class BacklogItem:
    """항목 하나 — 번호 · 상태 · 영역(파일 이름 줄기) · 제목 · 본문 줄 · 첫 줄 번호."""

    itemId: str
    status: str
    area: str
    title: str
    listLine: list[str]
    lineNumber: int

    @property
    def text(self) -> str:
        return "\n".join(self.listLine)


def parseBacklogText(text: str, area: str) -> tuple[list[BacklogItem], list[str]]:
    """글 하나를 항목들로 읽는다. 두 번째 값은 모양 문제(`줄 번호: 내용`) — 상태 태그가 없거나 허용값이 아닌 항목."""
    listItem: list[BacklogItem] = []
    listProblem: list[str] = []
    current: BacklogItem | None = None
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        if _kItemStartRe.match(line):
            match = _kItemHeadRe.match(line)
            if match is None:
                itemId = _kItemStartRe.match(line).group(1)
                listProblem.append(f"{lineNumber}: `{itemId}` 첫 줄에 [상태] 태그가 없습니다")
                current = BacklogItem(itemId, "", area, "", [line], lineNumber)
            else:
                itemId, status, rest = match.groups()
                if status not in kListBacklogStatus:
                    listProblem.append(f"{lineNumber}: `{itemId}` 상태 '{status}' 는 허용값({' · '.join(kListBacklogStatus)})이 아닙니다")
                titleMatch = _kTitleRe.search(rest)
                current = BacklogItem(itemId, status, area, titleMatch.group(1) if titleMatch else rest, [line], lineNumber)
            listItem.append(current)
        elif current is not None and line.startswith("  "):
            current.listLine.append(line)
        else:
            current = None
    return listItem, listProblem


def listBacklogFiles(repositoryRoot: Path) -> list[Path]:
    """항목 파일 전부(안내 문서 `README.md` 는 뺀다), 이름 순."""
    return sorted(path for path in (repositoryRoot / kBacklogRelDir).glob("*.md") if path.name != "README.md")


def readBacklog(repositoryRoot: Path) -> tuple[list[BacklogItem], list[str]]:
    """모든 항목 파일을 읽는다. 문제 줄은 `파일:줄: 내용` 이다."""
    listItem: list[BacklogItem] = []
    listProblem: list[str] = []
    for path in listBacklogFiles(repositoryRoot):
        listFileItem, listFileProblem = parseBacklogText(path.read_text(encoding="utf-8"), path.stem)
        listItem += listFileItem
        relPath = path.relative_to(repositoryRoot).as_posix()
        listProblem += [f"{relPath}:{problem}" for problem in listFileProblem]
    return listItem, listProblem
