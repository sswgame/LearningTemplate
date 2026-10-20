#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
파일마다 보는 소스 게이트들의 **공유 훑기** — 트리를 한 번 걷고, 파일을 한 번 읽고, 주석 · 리터럴을 한 번 가린다.

게이트 하나가 프로세스 하나로 돌며 트리를 따로 걷고 읽고 가리면, 같은 C++ 수천 파일에 같은 일이 게이트 수만큼 되풀이된다
(`blankCommentsAndLiterals` 를 열다섯 게이트가 각자 돌렸다). 그래서 실행 단위를 게이트가 아니라 **파일**로 둔다: 파일 하나를 읽어
`SourceFile` 로 만들고, 그 파일을 보는 게이트들의 `checkSourceFile` 을 차례로 부른다. 가린 글(`masked` · `noComment`)은 처음 묻는
게이트가 만들고 나머지는 나눠 쓴다. 파일들은 코어 수만큼의 프로세스 덩어리로 나눈다(`common.Parallel.flatMapInProcesses` — 파일마다 파이썬
정규식을 돌리는 일이라 스레드로는 겹치지 않는다).

게이트가 이 길을 타는 법은 `LintGate.sourceScanRoots` 를 채우는 것이다(`LintGate.py` 의 "소스 게이트"). 게이트를 혼자 돌려도
(`main = XxxGate.run` · 커밋 훅) 같은 함수(`scanSourceGates`)를 게이트 하나로 부른다 — 혼자 돌 때와 함께 돌 때의 파일 고르기 ·
읽기 · 예외 기록이 갈리지 않는다. 함께 도는 진입점은 `RunSourceGates.py` 다.

**예외 기록.** 게이트는 `XxxGate.seeExemption` · `useExemption`(클래스 집합)을 부른다. 자식 프로세스의 집합은 부모로 오지 않으므로
파일마다 집합을 비우고 부른 뒤 `FileFindings` 에 담아 돌려주고, 부모가 합쳐 게이트 클래스에 다시 넣는다.

**추가한 줄.** 커밋 훅에서 이번 커밋이 추가한 줄만 세는 게이트는 그 범위를 `LineScope`(`parseAddedLines`)로 받는다 — 덩어리 머리 해석 ·
병합 커밋 건너뛰기 · 기준선 비교를 게이트마다 베끼지 않는다(`LintGate.BaselineSourceGate`).
"""

from __future__ import annotations

import argparse
import functools
import re
import sys
import time
from dataclasses import dataclass, field
from functools import cached_property
from pathlib import Path
from typing import Any, Iterable


from common.CodeText import blankComments, blankCommentsAndLiterals  # noqa: E402
from common.Parallel import flatMapInProcesses  # noqa: E402
from common.Search import collectRepositoryFiles, resolveFileArguments  # noqa: E402

#: `git diff -U0` 의 덩어리 머리 — 새 쪽 시작 줄과 줄 수(없으면 1).
_kHunkRe = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


@dataclass(eq=False)
class SourceFile:
    """
    게이트들이 나눠 쓰는 파일 하나. 글은 한 번 읽고, 가린 글 · 줄 목록은 처음 묻는 게이트가 한 번 만든다.

    - `relative`  : 저장소 기준 POSIX 경로(위반 줄의 머리)
    - `masked`    : 주석 · 문자열 · 문자 리터럴을 같은 길이 공백으로(`blankCommentsAndLiterals`)
    - `noComment` : 주석만 가린 글(`blankComments`)
    - `decodeError`: UTF-8 로 읽지 못한 까닭(읽을 수 있으면 빈 글). 그때 `text` 는 못 읽은 바이트를 U+FFFD 로 바꾼 글이다 —
                    엄격히 읽어야 하는 게이트는 이것을 보고 `FileFindings.error` 로 알린다.
    """

    path: Path
    relative: str
    text: str
    decodeError: str = ""

    @cached_property
    def masked(self) -> str:
        return blankCommentsAndLiterals(self.text)

    @cached_property
    def noComment(self) -> str:
        return blankComments(self.text)

    @cached_property
    def listLine(self) -> list[str]:
        return self.text.splitlines()

    @cached_property
    def listMaskedLine(self) -> list[str]:
        return self.masked.splitlines()


@dataclass
class FileFindings:
    """
    게이트 하나가 파일 하나에서 찾은 것. 자식 프로세스에서 부모로 피클해 보낸다.

    - `listViolation` · `listNote`: `GateResult` 의 같은 칸에 파일 순서대로 이어 붙는다
    - `value`  : 게이트가 끝맺음(`finishSourceScan`)에서 모을 값(파일 사이의 관계를 보는 게이트) — 피클할 수 있어야 한다
    - `error`  : 검사가 성립하지 않는 까닭(읽을 수 없는 파일). 있으면 그 게이트가 종료 2 로 끝난다(`LintGate.finishSourceScanChecked`)
    - 예외 기록 · 걸린 초는 훑기가 채운다
    """

    listViolation: list[str] = field(default_factory=list)
    listNote: list[str] = field(default_factory=list)
    value: Any = None
    error: str = ""
    setSeenExemption: set[str] = field(default_factory=set)
    setUsedExemption: set[str] = field(default_factory=set)
    seconds: float = 0.0


@dataclass
class SourceScanResult:
    """게이트 하나의 훑기 결과 — 본 파일 수와 파일마다의 `FileFindings`(찾은 것이 없는 파일도 칸이 있다, 파일 순서)."""

    fileCount: int
    listFindings: list[FileFindings]

    @property
    def seconds(self) -> float:
        """이 게이트의 `checkSourceFile` 이 쓴 초의 합(프로세스를 가리지 않는다)."""
        return sum(findings.seconds for findings in self.listFindings)


@dataclass(frozen=True)
class SourceTarget:
    """읽을 파일 하나와 그 파일을 보는 게이트들(작업 목록의 자리)."""

    path: Path
    relative: str
    tupleJobIndex: tuple[int, ...]


def selectGateFilesInternal(repositoryRoot: Path, gate: Any, args: argparse.Namespace, mapWalk: dict[tuple, list[Path]]) -> list[Path]:
    """
    게이트 하나가 볼 파일. `--files` 면 `resolveFileArguments`, 아니면 뿌리 · 제외 폴더가 같은 걷기를 나눠 쓴다 —
    `LintGate.selectTargetFiles` 와 같은 결과다(확장자만 걸은 뒤 거른다). 빈 `--files`(인자 없이 준 것)는 "볼 파일 없음" 이다 — 전체 훑기로 읽지 않는다.
    """
    listFileArgument = getattr(args, "files", None)
    if listFileArgument is not None:
        return resolveFileArguments(repositoryRoot, listFileArgument, listScanRoot=gate.sourceScanRoots, suffixes=gate.sourceSuffixes,
                                    excludedDirNames=gate.sourceExcludedDirNames)
    key = (tuple(gate.sourceScanRoots), frozenset(gate.sourceExcludedDirNames))
    if key not in mapWalk:
        mapWalk[key] = collectRepositoryFiles(repositoryRoot, key[0], excludedDirNames=key[1], bAnySuffix=True)
    setSuffix = {suffix.lower() for suffix in gate.sourceSuffixes}
    return [path for path in mapWalk[key] if path.suffix.lower() in setSuffix]


def collectSourceTargets(repositoryRoot: Path, listJob: list[tuple[Any, argparse.Namespace]]) -> list[SourceTarget]:
    """작업마다 볼 파일을 골라, 파일 하나에 그 파일을 보는 작업들을 묶는다. 경로 순서다."""
    mapWalk: dict[tuple, list[Path]] = {}
    mapPathToJob: dict[Path, list[int]] = {}
    for jobIndex, (gate, args) in enumerate(listJob):
        for path in selectGateFilesInternal(repositoryRoot, gate, args, mapWalk):
            mapPathToJob.setdefault(path, []).append(jobIndex)
    return [SourceTarget(path, path.relative_to(repositoryRoot).as_posix(), tuple(listIndex)) for path, listIndex in sorted(mapPathToJob.items())]


def checkTargetInternal(listJob: list[tuple[Any, argparse.Namespace]], target: SourceTarget) -> list[tuple[int, FileFindings]]:
    """
    파일 하나를 읽어 그 파일을 보는 게이트들에 차례로 넘긴다(자식 프로세스에서도 돈다). 훑는 사이에 지워진 파일은 건너뛰고,
    그 밖의 읽기 실패는 `OSError` 로 올린다 — 못 읽은 파일을 말없이 빼면 검사가 그 파일을 본 척한다.
    """
    try:
        data = target.path.read_bytes()
    except FileNotFoundError:
        return []
    except OSError as exception:
        raise OSError(f"읽기 실패: {target.path}: {exception}") from exception
    decodeError = ""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exception:
        text = data.decode("utf-8", errors="replace")
        decodeError = f"UTF-8 인코딩 오류: {target.relative}: {exception}"
    # `read_text` 와 같은 글 — 줄 끝을 `\n` 하나로 맞춘다(보편 줄바꿈).
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    source = SourceFile(target.path, target.relative, text, decodeError)
    listResult: list[tuple[int, FileFindings]] = []
    for jobIndex in target.tupleJobIndex:
        gate, args = listJob[jobIndex]
        gateClass = type(gate)
        gateClass._setSeenExemption = set()
        gateClass._setUsedExemption = set()
        start = time.perf_counter()
        findings = gate.checkSourceFile(source, args) or FileFindings()
        findings.seconds = time.perf_counter() - start
        findings.setSeenExemption = gateClass._setSeenExemption
        findings.setUsedExemption = gateClass._setUsedExemption
        listResult.append((jobIndex, findings))
    return listResult


def scanSourceGates(repositoryRoot: Path, listJob: list[tuple[Any, argparse.Namespace]]) -> list[SourceScanResult]:
    """
    소스 게이트 작업들(`(게이트, 인자)`)을 한 번의 훑기로 돌려 작업마다 결과를 돌려줍니다. 게이트의 준비(`prepareSourceScan`)와
    끝맺음(`finishSourceScan`)은 부르는 쪽이 한다. 예외 기록은 게이트 클래스의 집합에 합쳐 둔다(훑기 전의 기록에 더한다).
    """
    listTarget = collectSourceTargets(repositoryRoot, listJob)
    listBeforeSeen = [set(type(gate)._setSeenExemption) for gate, _ in listJob]
    listBeforeUsed = [set(type(gate)._setUsedExemption) for gate, _ in listJob]
    listResult = [SourceScanResult(fileCount=0, listFindings=[]) for _ in listJob]
    for target in listTarget:
        for jobIndex in target.tupleJobIndex:
            listResult[jobIndex].fileCount += 1
    for jobIndex, findings in flatMapInProcesses(functools.partial(checkTargetInternal, listJob), listTarget):
        listResult[jobIndex].listFindings.append(findings)
        listBeforeSeen[jobIndex] |= findings.setSeenExemption
        listBeforeUsed[jobIndex] |= findings.setUsedExemption
    for jobIndex, (gate, _) in enumerate(listJob):
        type(gate)._setSeenExemption = listBeforeSeen[jobIndex]
        type(gate)._setUsedExemption = listBeforeUsed[jobIndex]
    return listResult


@dataclass(frozen=True)
class LineScope:
    """
    줄 단위로 세는 게이트(`LintGate.BaselineSourceGate`)가 볼 줄의 범위 — 판정 단위를 게이트마다 다시 짓지 않게 형식으로 둔다.

    - `mapAdded` 가 None 이면 파일 전체(전체 훑기 · `--whole-files`)
    - 아니면 파일(저장소 기준 POSIX 경로) → 이번 커밋이 추가한 줄 번호(`git diff --cached -U0` 의 + 줄, `parseAddedLines`).
      표에 없는 파일은 추가한 줄이 없다
    """

    mapAdded: dict[str, frozenset[int]] | None = None

    def touches(self, relative: str, firstLine: int, lastLine: int) -> bool:
        """`relative` 의 `firstLine` ~ `lastLine`(1 부터, 닫힌 구간)에 이 범위의 줄이 하나라도 있는지."""
        if self.mapAdded is None:
            return True
        setAdded = self.mapAdded.get(relative, frozenset())
        return any(number in setAdded for number in range(firstLine, lastLine + 1))


def parseAddedLines(diffText: str) -> LineScope:
    """`git diff --cached -U0` 출력 → 파일마다 추가된 줄의 `LineScope`."""
    mapAdded: dict[str, set[int]] = {}
    current = ""
    for line in diffText.splitlines():
        if line.startswith("+++ "):
            current = line[6:] if line.startswith("+++ b/") else ""
            if current:
                mapAdded.setdefault(current, set())
            continue
        hunk = _kHunkRe.match(line)
        if hunk and current:
            start = int(hunk.group(1))
            count = 1 if hunk.group(2) is None else int(hunk.group(2))
            mapAdded[current].update(range(start, start + count))
    return LineScope({relative: frozenset(setLine) for relative, setLine in mapAdded.items()})


def joinFindings(listFindings: Iterable[FileFindings]) -> tuple[list[str], list[str]]:
    """파일마다의 위반 · 참고를 파일 순서대로 이어 붙입니다."""
    listViolation: list[str] = []
    listNote: list[str] = []
    for findings in listFindings:
        listViolation += findings.listViolation
        listNote += findings.listNote
    return listViolation, listNote


__all__ = ["FileFindings", "LineScope", "SourceFile", "SourceScanResult", "SourceTarget", "collectSourceTargets", "joinFindings", "parseAddedLines",
           "scanSourceGates"]
