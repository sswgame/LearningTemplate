#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
시나리오의 `<ExpectLog>` 가 가리키는 로그가 소스에 아직 있는지 봅니다.

`<ExpectLog contains="문구" count="0"/>`("이 로그가 나오면 안 된다")는 소스에서 문구를 바꾸면 **조용히 거짓 통과**한다 — 바뀐 문구는 영원히
나오지 않으니 줄 수가 0 이다. `atLeast` 쪽은 실행에서 실패하지만 그것도 시나리오를 올린 실기동(GPU)에서야 안다. 구조로는 두 길을 막는다.
1. 로그 id 가 있는 줄(`SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID`)은 `id="…"` 로 지정한다 — 문구가 바뀌어도 id 는 남는다. `contains` 는 id 없는 줄에만 쓴다.
2. `contains` 가 남는 곳은 이 린트가 그 문구가 소스에 실제로 있는지 본다.

검사 규칙:
- `ExpectLog/Attribute`: `id` 와 `contains` 중 정확히 하나(실행기의 읽기 오류와 같은 규칙을 CI 에서 먼저 본다).
- `ExpectLog/UnknownID`: `id` 가 `SW_LOG_*_ID( "id", …)` 의 id 로 소스에 없다.
- `ExpectLog/StalePhrase`: `contains` 문구가 어떤 로그 호출(`SW_LOG_*`)의 글에도, 번역 문자열(`Resource/**/localization/`)에도 없다.
- `ExpectLog/UseID`: `contains` 문구가 id 가 있는 로그에서만 나온다 — `id` 로 지정한다.
- `ExpectLog/LevelTag`: `contains` 가 `[Error]` · `[Warning]` 같은 수준 꼬리표다. 실행기가 세는 글은 로그 **본문**이라 수준 꼬리표는 들어 있지 않다
  (`count="0"` 이 항상 참이다). 처리하지 않은 Warning · Error 는 로그 게이트(`allowedlogs.json`)가 이미 실패로 센다.

문구 맞추기: 로그 호출의 글은 `%` 서식(`%#` · `%d` · `%s` …)을 구멍으로 보고, 문구는 **구멍을 뺀 고정 조각이 순서대로 맞는지**로 찾는다
(문구에 실행 때 값이 들어 있어도 소스의 서식 자리와 맞춘다). 문구가 호출 글의 한 부분이면 된다(`contains` 는 부분 문자열이다).

  python Scripts/lint/gate/CheckExpectLogPhrases.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import collections
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Paths import normalizePath  # noqa: E402
from common.Search import collectRepositoryFiles, collectSourceFiles  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402

_kCppSuffixes = {".cpp", ".h", ".inl"}
_kScanRoots = ("Source",)
_kTranslationSuffixes = (".xml", ".json", ".po", ".csv", ".tsv")

#: 로그 호출의 시작. 인자는 `;` 까지다(CheckLogLanguage 와 같은 덩어리).
_kLogCallRe = re.compile(r"\bSW_LOG_(?:ERROR|WARNING|INFO|TRACE)(_ID)?\s*\(")
_kCppStringRe = re.compile(r'"(?:\\.|[^"\\\n])*"')
_kStringOrCommentRe = re.compile(r'"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*.*?\*/', re.S)
#: 서식 구멍. `%#`(이 프로젝트의 만능 서식)와 printf 서식. `%%` 는 먼저 글자 `%` 로 바꾼다.
_kHoleRe = re.compile(r"%(?:[-+ 0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXfFeEgGcsp]|#)")
_kEscapeRe = re.compile(r"\\(.)")
_kEscapeMap = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", '"': '"', "\\": "\\", "'": "'"}
_kLevelTagRe = re.compile(r"^\s*\[(?:Error|Warning|Info|Trace)\]")
#: 문구의 4글자 조각으로 후보 호출을 줄인다(전수 대조는 호출 수 × 문구 수 × 자동자 크기라 느리다).
_kGramSize = 4
_kPercentSentinel = "\x01"
#: 구멍이 낀 맞춤에서 문구 중 고정 글자로 맞아야 하는 최소 글자 수(문구가 더 짧으면 전부). 낮추면 구멍이 문구를 삼켜 낡은 문구를 놓친다.
_kMinFixedMatch = 12
#: 둘로 나눠 찾을 때 각 조각의 최소 글자 수. 너무 짧으면 아무 문구나 두 조각으로 갈라 맞는다.
_kMinPartLength = 6


class LogTemplate:
    """로그 호출 하나의 글 — 고정 조각들(구멍 사이)과 id."""

    __slots__ = ("pieces", "logID", "location")

    def __init__(self, pieces: list[str], logID: str, location: str) -> None:
        self.pieces = pieces
        self.logID = logID
        self.location = location


def unescapeLiteral(literal: str) -> str:
    """C++ 문자열 리터럴(따옴표 포함)을 값으로 푼다. 모르는 이스케이프는 그 글자 그대로 둔다."""
    return _kEscapeRe.sub(lambda match: _kEscapeMap.get(match.group(1), match.group(1)), literal[1:-1])


def stripComments(text: str) -> str:
    """C++ 주석을 지운다(문자열 안의 `//` 는 남긴다)."""
    return _kStringOrCommentRe.sub(lambda match: match.group(0) if match.group(0).startswith('"') else " ", text)


def splitPieces(message: str) -> list[str]:
    """서식 글을 구멍으로 쪼갠 고정 조각들. 구멍이 없으면 조각 하나."""
    return [piece.replace(_kPercentSentinel, "%") for piece in _kHoleRe.split(message.replace("%%", _kPercentSentinel))]


def collectLogTemplates(relative: str, text: str) -> tuple[list[LogTemplate], list[LogTemplate]]:
    """
    한 파일의 글 모음 — (로그 호출 `SW_LOG_*` 의 글, 로그 호출 밖 문장의 글). id 호출은 첫 리터럴이 id 다.
    로그 밖 문장은 `reply = "netplay stats from " + to_string( n ) + " process(es)"` 처럼 로그 글을 따로 만들어 넘기는 곳이다 —
    문장 안의 리터럴 사이가 공백뿐이면 이어 붙인 글이고, 다른 식이 끼면 구멍이다.
    """
    listLog: list[LogTemplate] = []
    listGeneric: list[LogTemplate] = []
    noComment = stripComments(text)
    listLogSpan: list[tuple[int, int]] = []
    for match in _kLogCallRe.finditer(noComment):
        end = noComment.find(";", match.end())
        listLogSpan.append((match.start(), end if end != -1 else len(noComment)))
        listLiteral = [unescapeLiteral(literal) for literal in _kCppStringRe.findall(noComment[match.end():listLogSpan[-1][1]])]
        if not listLiteral:
            continue
        logID = ""
        if match.group(1):
            logID, listLiteral = listLiteral[0], listLiteral[1:]
        line = noComment.count("\n", 0, match.start()) + 1
        listLog.append(LogTemplate(splitPieces("".join(listLiteral)), logID, f"{relative}:{line}"))
    spanIndex = 0
    listPiece: list[str] = []      # 지금 문장에서 모은 고정 조각 — 리터럴 사이가 공백뿐이면 잇고, 아니면 구멍으로 새 조각
    previousEnd = -1
    statementStart = 0

    def flush(at: int) -> None:
        if listPiece:
            listGeneric.append(LogTemplate(_joinWithHoles(listPiece), "", f"{relative}:{noComment.count(chr(10), 0, at) + 1}"))
        listPiece.clear()

    for token in re.finditer(r'"(?:\\.|[^"\\\n])*"|[;{}]', noComment):
        if token.group(0) in (";", "{", "}"):
            flush(token.start())
            previousEnd = -1
            continue
        while spanIndex < len(listLogSpan) and listLogSpan[spanIndex][1] < token.start():
            spanIndex += 1
        if spanIndex < len(listLogSpan) and listLogSpan[spanIndex][0] <= token.start():
            continue   # 로그 호출 안의 리터럴은 위에서 셌다
        literal = unescapeLiteral(token.group(0))
        if previousEnd >= 0 and noComment[previousEnd:token.start()].strip() == "" and listPiece:
            listPiece[-1] += literal
        else:
            listPiece.append(literal)
        previousEnd = token.end()
    flush(len(noComment))
    return listLog, listGeneric


def _joinWithHoles(listPiece: list[str]) -> list[str]:
    """문장의 조각들(사이는 구멍) → 서식 구멍을 더 쪼갠 고정 조각들."""
    listFixed: list[str] = []
    for piece in listPiece:
        listFixed += splitPieces(piece)
    return listFixed


def phraseFitsPieces(phrase: str, pieces: list[str]) -> bool:
    """
    `phrase` 가 `piece0 ∗ piece1 ∗ … pieceN`(∗ = 아무 글) 모양의 어떤 글의 **부분 문자열**일 수 있는가.
    자동자로 본다: 상태 = (조각 k, 그 안의 자리 o) 또는 구멍 k. 어느 상태에서 시작해도 되고, 문구를 다 먹으면 맞은 것이다.
    """
    if not phrase:
        return True
    if len(pieces) == 1:
        return phrase in pieces[0]
    lastPiece = len(pieces) - 1

    def close(current: dict[tuple[int, int], int]) -> dict[tuple[int, int], int]:
        """조각 끝 → 뒤의 구멍, 구멍 → 다음 조각 시작(빈 조각이 이어져도 끝까지). 상태마다 가장 많이 맞춘 고정 글자 수를 든다."""
        closed = dict(current)
        pending = list(current)
        while pending:
            k, o = pending.pop()
            nextState = None
            if o == -1:
                nextState = (k + 1, 0)
            elif o == len(pieces[k]) and k < lastPiece:
                nextState = (k, -1)
            if nextState is not None and closed.get(nextState, -1) < closed[(k, o)]:
                closed[nextState] = closed[(k, o)]
                pending.append(nextState)
        return closed

    # 어느 상태에서 시작해도 되지만 첫 글자를 먹을 수 있는 상태만 남기면 된다(구멍은 무엇이든 먹는다).
    states: dict[tuple[int, int], int] = {}   # (k, o): o >= 0 은 조각 k 의 o 번째 앞, o == -1 은 조각 k 뒤의 구멍 → 맞춘 고정 글자 수
    first = phrase[0]
    for k, piece in enumerate(pieces):
        at = piece.find(first)
        while at != -1:
            states[(k, at + 1)] = 1
            at = piece.find(first, at + 1)
        if k < lastPiece:
            states.setdefault((k, -1), 0)
    states = close(states)
    for ch in phrase[1:]:
        advanced: dict[tuple[int, int], int] = {}
        for (k, o), matched in states.items():
            if o == -1:
                advanced[(k, -1)] = max(advanced.get((k, -1), 0), matched)
            elif o < len(pieces[k]) and pieces[k][o] == ch:
                advanced[(k, o + 1)] = max(advanced.get((k, o + 1), 0), matched + 1)
        if not advanced:
            return False
        states = close(advanced)
    # 구멍이 문구 거의 전부를 삼킨 맞춤("A %# B" 가 아무 글이나 받는다)은 맞춘 것이 아니다 — 고정 글자가 충분히 맞아야 한다.
    return max(states.values()) >= min(_kMinFixedMatch, len(phrase))


class TemplateIndex:
    """로그 글 모음 — 4글자 조각 색인으로 문구가 맞을 만한 호출만 자동자에 돌린다."""
    def __init__(self, listTemplate: list[LogTemplate]) -> None:
        self.listTemplate = listTemplate
        self.corpus = chr(0).join(piece for template in listTemplate for piece in template.pieces)
        self.mapGram: dict[str, set[int]] = collections.defaultdict(set)
        for index, template in enumerate(listTemplate):
            for piece in template.pieces:
                for at in range(len(piece) - _kGramSize + 1):
                    self.mapGram[piece[at:at + _kGramSize]].add(index)

    def findFitting(self, phrase: str, *, bFirstOnly: bool = False) -> list[LogTemplate]:
        """문구가 맞는 호출들. @p bFirstOnly 면 하나만 찾고 멈춘다(있는지만 볼 때)."""
        if len(phrase) < _kGramSize:
            return [template for template in self.listTemplate if phraseFitsPieces(phrase, template.pieces)]
        if bFirstOnly and phrase in self.corpus:
            return [self.listTemplate[0]]   # 구멍 없이 한 조각 안에 그대로 든다 — 있는지만 보는 길의 빠른 답(조각 사이는 NUL 이라 걸치지 않는다)
        # 문구의 4글자 조각이 여럿 든 호출만 자동자에 돌린다.
        uniqueGram = {phrase[at:at + _kGramSize] for at in range(len(phrase) - _kGramSize + 1)}
        counter: collections.Counter[int] = collections.Counter()
        for gram in uniqueGram:
            counter.update(self.mapGram.get(gram, ()))
        # 맞는 호출은 문구의 고정 글자가 12자 가까이 맞아야 한다 — 문구의 4글자 조각 중 적어도 둘(길면 5분의 1)이 그 호출의 고정 조각에 든다. shortcut: 실행 때 값이 문구의 대부분이면 놓친다.
        needed = min(max(2, len(uniqueGram) // 5), len(uniqueGram))
        listCandidate = sorted(index for index, hitCount in counter.items() if hitCount >= needed)
        listFound: list[LogTemplate] = []
        for index in listCandidate:
            if phraseFitsPieces(phrase, self.listTemplate[index].pieces):
                listFound.append(self.listTemplate[index])
                if bFirstOnly:
                    break
        return listFound


def collectTranslationText(repositoryRoot: Path) -> str:
    """`Resource/**/localization/` 의 문자열 표(번역 파일) 글을 한 덩어리로."""
    listPath = [path for path in collectRepositoryFiles(repositoryRoot, ("Resource",), suffixes=_kTranslationSuffixes)
                if "/localization/" in normalizePath(str(path.relative_to(repositoryRoot)))]
    return "\n".join(text for _, text in LintGate.readFiles(listPath))


def collectScenarioLogSteps(repositoryRoot: Path) -> list[tuple[str, dict[str, str], str]]:
    """(시나리오 위치, `<ExpectLog>` 속성, 그 시나리오가 직접 쓰는 글) — 깨진 XML 은 에셋 검증 규칙이 알린다."""
    listStep: list[tuple[str, dict[str, str], str]] = []
    for path in (path for path in collectRepositoryFiles(repositoryRoot, ("Resource",), suffixes=(".xml",)) if path.name.endswith(".scenario.xml")):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError:
            continue
        # 시나리오가 `<DevCommand line="log …">` 처럼 스스로 내는 로그의 글 — `<ExpectLog>` 밖 엘리먼트의 속성값이다.
        ownText = "\n".join(value for element in root.iter() if element.tag != "ExpectLog" for value in element.attrib.values())
        listStep.extend((relative, dict(element.attrib), ownText) for element in root.iter("ExpectLog"))
    return listStep


def fitsAsTwoParts(phrase: str, index: TemplateIndex, genericIndex: TemplateIndex) -> bool:
    """
    문구가 둘로 나뉘어 각각 소스에 있는가 — `"Catalog %#"` 에 `describe( "Edited", … )` 가 들어가듯 로그 글이 호출 · 함수 경계를 넘어 이어지는 곳.
    shortcut: 함수 경계를 넘는 조립은 따라가지 못한다. 앞뒤 조각이 각각 소스에 있으면 통과로 본다(두 조각이 따로 낡는 것은 잡고, 조립 순서가 바뀐 것은 놓친다).
    """
    for cut in range(_kMinPartLength, len(phrase) - _kMinPartLength + 1):
        head, tail = phrase[:cut], phrase[cut:]
        if (index.findFitting(head, bFirstOnly=True) or genericIndex.findFitting(head, bFirstOnly=True)) and (index.findFitting(tail, bFirstOnly=True) or genericIndex.findFitting(tail, bFirstOnly=True)):
            return True
    return False


def findExpectLogViolations(listStep: list[tuple[str, dict[str, str], str]], index: TemplateIndex, genericIndex: TemplateIndex, translationText: str) -> list[str]:
    """`<ExpectLog>` 마다 규칙 다섯을 본다. 위반 글 목록."""
    uniqueID = {template.logID for template in index.listTemplate if template.logID}
    listViolation: list[str] = []
    for relative, attrib, ownText in listStep:
        logID, phrase = attrib.get("id"), attrib.get("contains")
        shown = f"id='{logID}'" if logID is not None else f"contains='{phrase}'"
        if (logID is None) == (phrase is None):
            listViolation.append(f"[ExpectLog/Attribute] {relative}: <ExpectLog {shown}> 은 id 와 contains 중 정확히 하나여야 합니다")
            continue
        if logID is not None:
            if logID not in uniqueID:
                listViolation.append(f"[ExpectLog/UnknownID] {relative}: 로그 id '{logID}' 를 쓰는 SW_LOG_*_ID 가 소스에 없습니다")
            continue
        assert phrase is not None
        if _kLevelTagRe.match(phrase):
            listViolation.append(f"[ExpectLog/LevelTag] {relative}: contains='{phrase}' — 센 글은 로그 본문이라 수준 꼬리표가 없습니다(항상 0 줄). 지우거나 로그 id 로 지정합니다")
            continue
        listFitting = index.findFitting(phrase)
        # 로그 호출 밖에서 만든 글(문장 안의 리터럴 · 번역 파일 · 시나리오가 직접 내는 글)에 있으면 로그 id 가 있는 호출은 그 글을 그대로 받은 것일 수 있다.
        bElsewhere = phrase in translationText or phrase in ownText or bool(genericIndex.findFitting(phrase, bFirstOnly=True))
        if not listFitting and not bElsewhere and fitsAsTwoParts(phrase, index, genericIndex):
            continue
        if not listFitting and not bElsewhere:
            listViolation.append(f"[ExpectLog/StalePhrase] {relative}: contains='{phrase}' 인 로그가 소스에 없습니다 — 문구가 바뀌었다면 새 문구로, 로그 id 가 있으면 id 로 고칩니다")
            continue
        if listFitting and all(template.logID for template in listFitting) and not bElsewhere:
            listViolation.append(f"[ExpectLog/UseID] {relative}: contains='{phrase}' 는 id 가 있는 로그({listFitting[0].logID} · {listFitting[0].location})입니다 — id=\"{listFitting[0].logID}\" 로 지정합니다")
    return listViolation


class CheckExpectLogPhrasesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "시나리오 <ExpectLog> 가 가리키는 로그(id · 문구)가 소스에 있는지 검사"
    buildComment = "Checking that scenario ExpectLog ids and phrases still exist in the source..."
    timeoutSeconds = 120
    # 로그(소스)와 시나리오(리소스)가 서로 다른 파일이라 staged 부분집합으로는 볼 수 없다 — CI 린트 잡(RunLintSuite)과 ctest -L lint 가 트리 전체로 돌린다.
    preCommitSkipReason = "트리 전체 대조(소스의 로그 호출 × 시나리오) — CI 린트 잡(RunLintSuite)과 ctest -L lint 가 push 마다 본다"
    violationHeader = "소스에 없거나 id 로 지정해야 하는 <ExpectLog>"
    hint = (
        "  문구가 바뀌었다: 시나리오의 contains 를 새 문구로 고칩니다(%d 같은 서식 자리는 실행 때 값 그대로 적어도 됩니다).\n"
        "  id 가 있는 로그다: contains 대신 id=\"영역.사건\" 으로 지정합니다 — 문구가 바뀌어도 단언이 따라옵니다.\n"
        "  한계(shortcut): 로그 글이 호출 안에서 리터럴로 이어지지 않고 변수 · 함수로 만들어지면 이 린트가 찾지 못합니다 — 그 줄에는 SW_LOG_*_ID 를 붙여 id 로 지정합니다."
    )
    selfTestCases = [
        {
            "name": "바뀐 문구를 가리키는 count=0",
            "expectedViolation": "[ExpectLog/StalePhrase] Resource/engine/automation/probe.scenario.xml: contains='Probe refused the thing'",
            "files": {
                "Source/Engine/Probe.cpp": "void f() { SW_LOG_WARNING( \"Probe declined the thing\" ); }\n",
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><At frame=\"1\"><ExpectLog contains=\"Probe refused the thing\" count=\"0\"/></At></Scenario>\n",
            },
        },
        {
            "name": "실행 때 값이 낀 문구는 서식 자리와 맞춘다(맞는 쪽은 잡지 않고 틀린 고정 조각만 잡는다)",
            "expectedViolation": "[ExpectLog/StalePhrase] Resource/engine/automation/probe.scenario.xml: contains='Moved 'a/b.txt' to the trash'",
            "files": {
                "Source/Engine/Probe.cpp": "void f() { SW_LOG_INFO( \"Moved '%#' to the recycle bin\", path ); }\n",
                "Resource/engine/automation/probe.scenario.xml": (
                    "<Scenario><At frame=\"1\"><ExpectLog contains=\"Moved 'a/b.txt' to the recycle bin\" count=\"1\"/>"
                    "<ExpectLog contains=\"Moved 'a/b.txt' to the trash\" count=\"1\"/></At></Scenario>\n"),
            },
        },
        {
            "name": "id 가 있는 로그를 문구로 지정한다",
            "expectedViolation": "[ExpectLog/UseID] Resource/engine/automation/probe.scenario.xml: contains='sprite clip clamped' 는 id 가 있는 로그(Probe.Clamped",
            "files": {
                "Source/Engine/Probe.cpp": "void f() { SW_LOG_WARNING_ID( \"Probe.Clamped\", \"Probe: sprite clip clamped (%#)\", n ); }\n",
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><At frame=\"1\"><ExpectLog contains=\"sprite clip clamped\" atLeast=\"1\"/></At></Scenario>\n",
            },
        },
        {
            "name": "없는 로그 id",
            "expectedViolation": "[ExpectLog/UnknownID] Resource/engine/automation/probe.scenario.xml: 로그 id 'Probe.Clmaped'",
            "files": {
                "Source/Engine/Probe.cpp": "void f() { SW_LOG_WARNING_ID( \"Probe.Clamped\", \"clamped\" ); }\n",
                "Resource/engine/automation/probe.scenario.xml": (
                    "<Scenario><At frame=\"1\"><ExpectLog id=\"Probe.Clamped\" atLeast=\"1\"/>"
                    "<ExpectLog id=\"Probe.Clmaped\" count=\"0\"/></At></Scenario>\n"),
            },
        },
        {
            "name": "수준 꼬리표 문구",
            "expectedViolation": "[ExpectLog/LevelTag] Resource/engine/automation/probe.scenario.xml: contains='[Error]'",
            "files": {
                "Source/Engine/Probe.cpp": "void f() { SW_LOG_ERROR( \"[Error] in a literal\" ); }\n",
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><At frame=\"1\"><ExpectLog contains=\"[Error]\" count=\"0\"/></At></Scenario>\n",
            },
        },
        {
            "name": "id 와 contains 를 함께 쓴다",
            "expectedViolation": "[ExpectLog/Attribute] Resource/engine/automation/probe.scenario.xml:",
            "files": {
                "Source/Engine/Probe.cpp": "void f() { SW_LOG_WARNING_ID( \"Probe.Clamped\", \"clamped\" ); }\n",
                "Resource/engine/automation/probe.scenario.xml": "<Scenario><At frame=\"1\"><ExpectLog id=\"Probe.Clamped\" contains=\"clamped\" atLeast=\"1\"/></At></Scenario>\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listStep = collectScenarioLogSteps(repositoryRoot)
        listTemplate: list[LogTemplate] = []
        listGeneric: list[LogTemplate] = []
        listPath = collectSourceFiles([repositoryRoot / root for root in _kScanRoots if (repositoryRoot / root).is_dir()], _kCppSuffixes)
        for path, text in LintGate.readFiles(listPath, mustContain='"'):
            listLog, listOutside = collectLogTemplates(normalizePath(str(path.relative_to(repositoryRoot))), text)
            listTemplate += listLog
            listGeneric += listOutside
        if listStep and not listTemplate:
            raise GateError("소스에서 로그 호출을 하나도 찾지 못했습니다 — SW_LOG_* 모양이 바뀌었나요?")
        listViolation = findExpectLogViolations(listStep, TemplateIndex(listTemplate), TemplateIndex(listGeneric), collectTranslationText(repositoryRoot))
        return GateResult(listViolation=sorted(set(listViolation)),
                          summary=f"<ExpectLog> {len(listStep)}곳, 소스의 로그 호출 {len(listTemplate)}개")


main = CheckExpectLogPhrasesGate.run


if __name__ == "__main__":
    sys.exit(main())
