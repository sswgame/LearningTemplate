#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/common/Plans.py

계획 문서(`docs/plans/*.md`)의 머리 줄을 읽는다. 목록 명령(`dev/ListPlans.py`)과 진행 상황 표(`common/StatusBoard.py`)가 쓴다.
머리 줄 해석은 `parsePlanText` 한 곳에만 둔다.

문서의 둘째 비어 있지 않은 줄(제목 `# ...` 다음 줄)이 정확히 이 모양이다:

    > 상태: <설계|구현대기|구현중|측정대기|보류|참고|끝|후보> · 선행: <없음 또는 문서 이름/단위> · 영역: <영역> · 옛번호: <ID,ID 또는 없음> · 진행: <n/m 또는 해당없음>
    (n = 끝난 단위 수, m = 전체 단위 수. 상태 `후보` 는 진행이 반드시 `해당없음`)

일의 이름은 계획 문서 파일 이름(`ClusteredLighting`)이고, 문서 안 단위는 `<문서이름>.<단위기호>` 다. `옛번호` 는 코드 주석 · 결정 기록 · 커밋에
박힌 옛 번호(`C-30`)로, 어느 문서로 옮겨졌는지 찾는 데만 쓴다.

아직 시작하지 않은 후보는 영역별 `Candidates-<영역>.md`(상태 `후보`)에 ``### <PascalCase영어이름> — <한글 제목>`` 절로 모은다.
절 아래 첫 비어 있지 않은 줄은 ``옛번호: C-30, D2-13`` 이고 없으면 ``옛번호: 없음`` 이다.

머리 줄이나 후보 절이 틀리면 건너뛰지 않고 `PlanFormatError` 로 멈춘다(파일 이름을 담는다).
안내 · 규칙 문서(`kPlanNonDocumentName`)는 계획 문서가 아니다.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

#: 계획 폴더(저장소 기준).
kPlansRelDir = "docs/plans"

#: 계획 문서가 아닌 파일 — 안내 · 인계 · 규칙 · 로드맵.
kPlanNonDocumentName: tuple[str, ...] = ("README.md", "NEXT.md", "WorkerRules.md", "Roadmap.md")

#: 상태 값 — 진행 순서대로. `보류` 는 사용자가 나중으로 정한 것, `참고` 는 할 일 없는 참고 문서, `후보` 는 시작 전 후보를 모은 문서.
kListPlanStatus: tuple[str, ...] = ("설계", "구현대기", "구현중", "측정대기", "보류", "참고", "끝", "후보")

#: 영역 값.
kListPlanArea: tuple[str, ...] = ("engine", "graphics", "editor", "gameplay", "audio-ai-localization", "ui", "network", "quality", "project")

#: 후보 문서의 상태.
kCandidateStatus = "후보"

_kNone = "없음"
_kHeaderRe = re.compile(r"^> 상태: (.+?) · 선행: (.+?) · 영역: (.+?) · 옛번호: (.+?) · 진행: (.+)$")
_kProgressRe = re.compile(r"^(\d+)/(\d+)$")
_kNoProgress = "해당없음"
_kOldNumberRe = re.compile(r"^[A-Z]\d*-\d+(?:\.\d+)*$")
_kHeadingRe = re.compile(r"^(#{1,6}) +(.+?)\s*$")
_kFenceRe = re.compile(r"^\s*(```|~~~)")
_kCandidateHeadingRe = re.compile(r"^### ([A-Z][A-Za-z0-9]*) — (.+?)\s*$")
_kCandidateOldRe = re.compile(r"^옛번호: (.+?)\s*$")


class PlanFormatError(ValueError):
    """머리 줄이나 후보 절이 틀린 계획 문서. 메시지는 `파일: 내용` 이다."""


@dataclass
class Candidate:
    """후보 절 하나 — 영어 이름 · 한글 제목 · 옛 번호들 · 절 제목의 줄 번호."""

    name: str
    title: str
    listOldNumber: list[str]
    lineNumber: int


@dataclass
class PlanDoc:
    """계획 문서 하나 — 파일 이름 · 제목 · 머리 줄 값(진행은 `n/m` 또는 `해당없음` 그대로) · 머리 줄 원문 · 절 제목들 · 후보 절들."""

    fileName: str
    title: str
    status: str
    prerequisite: str
    area: str
    listOldNumber: list[str]
    progress: str
    headerLine: str
    listHeading: list[str]
    listCandidate: list[Candidate] = field(default_factory=list)

    @property
    def stem(self) -> str:
        return self.fileName.removesuffix(".md")


def parseOldNumbersInternal(value: str, where: str) -> list[str]:
    """`C-30,D2-13` 또는 `없음` 을 번호 목록으로(쉼표 뒤 공백은 받는다)."""
    if value == _kNone:
        return []
    listNumber = [part.strip() for part in value.split(",")]
    for number in listNumber:
        if not _kOldNumberRe.match(number):
            raise PlanFormatError(f"{where}: 옛번호 '{value}' 는 `C-30,D2-13` 모양 또는 `{_kNone}` 이어야 합니다")
    return listNumber


def parsePlanText(text: str, fileName: str) -> PlanDoc:
    """글 하나를 계획 문서로 읽는다. 머리 줄이 없거나 값이 틀리면 `PlanFormatError`."""
    listLine = text.splitlines()
    listNonBlank = [line for line in listLine if line.strip()]
    if len(listNonBlank) < 2 or not listNonBlank[0].startswith("# "):
        raise PlanFormatError(f"{fileName}: 첫 비어 있지 않은 줄이 `# 제목` 이 아닙니다")
    headerLine = listNonBlank[1]
    match = _kHeaderRe.match(headerLine)
    if match is None:
        raise PlanFormatError(f"{fileName}: 둘째 줄이 머리 줄 모양이 아닙니다 — `> 상태: … · 선행: … · 영역: … · 옛번호: … · 진행: …` 이어야 합니다: {headerLine}")
    status, prerequisite, area, oldNumberText, progress = match.groups()
    if status not in kListPlanStatus:
        raise PlanFormatError(f"{fileName}: 상태 '{status}' 는 허용값({' · '.join(kListPlanStatus)})이 아닙니다")
    if area not in kListPlanArea:
        raise PlanFormatError(f"{fileName}: 영역 '{area}' 는 허용값({' · '.join(kListPlanArea)})이 아닙니다")
    listOldNumber = parseOldNumbersInternal(oldNumberText, fileName)
    if status == kCandidateStatus and progress != _kNoProgress:
        raise PlanFormatError(f"{fileName}: 상태 `{kCandidateStatus}` 문서의 진행은 `{_kNoProgress}` 이어야 합니다: '{progress}'")
    if progress != _kNoProgress:
        progressMatch = _kProgressRe.match(progress)
        if progressMatch is None or not 0 < int(progressMatch.group(2)) >= int(progressMatch.group(1)):
            raise PlanFormatError(f"{fileName}: 진행 '{progress}' 는 `n/m`(0 ≤ n ≤ m, m > 0) 또는 `{_kNoProgress}` 이어야 합니다")
    listCandidate = parseCandidatesInternal(listLine, fileName) if status == kCandidateStatus else []
    return PlanDoc(fileName, listNonBlank[0][2:].strip(), status, prerequisite, area, listOldNumber, progress, headerLine,
                   listHeadingInternal(listLine), listCandidate)


def parseCandidatesInternal(listLine: list[str], fileName: str) -> list[Candidate]:
    """후보 문서의 `### 이름 — 제목` 절들. 절 아래 첫 비어 있지 않은 줄이 `옛번호: …` 가 아니면 오류."""
    listCandidate: list[Candidate] = []
    bFence = False
    for index, line in enumerate(listLine):
        if _kFenceRe.match(line):
            bFence = not bFence
            continue
        if bFence or not line.startswith("### "):
            continue
        where = f"{fileName}:{index + 1}"
        match = _kCandidateHeadingRe.match(line)
        if match is None:
            raise PlanFormatError(f"{where}: 후보 절 제목이 `### <PascalCase영어이름> — <한글 제목>` 모양이 아닙니다: {line}")
        listBody = [body for body in listLine[index + 1:] if body.strip()]
        oldMatch = _kCandidateOldRe.match(listBody[0]) if listBody else None
        if oldMatch is None:
            raise PlanFormatError(f"{where}: 후보 절 `{match.group(1)}` 아래 첫 줄이 `옛번호: …`(없으면 `옛번호: {_kNone}`) 이 아닙니다")
        listCandidate.append(Candidate(match.group(1), match.group(2), parseOldNumbersInternal(oldMatch.group(1), where), index + 1))
    return listCandidate


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
