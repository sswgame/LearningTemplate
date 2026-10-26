#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckEditorSizeLiterals.py

에디터 코드(`Source/Editor/**` · `Source/GameFramework/PlugIn/Editor/**`)가 **창 · 항목 · child · table 크기를 픽셀 숫자 리터럴로 정하지 않게** 한다.
크기는 글자 칸 · 줄 수로 적는다 — `EditorTextExtent`(`EditorChrome::setNextPanelSize` · `setNextWindowExtent` · `measureExtent`)와
`EditorChrome::setNextItemWidthColumns`. 픽셀로 적은 크기는 글꼴 · UI 배율 · 창 높이와 어긋나 낮은 창에서 본문이 한두 줄만 남았다.

왜 형식(타입)으로 못 막고 게이트를 두는가: 패널이 `<imgui.h>` 를 직접 포함하므로 `ImGui::SetNextItemWidth(120.0f)` 같은 원 API 호출 자체를 컴파일로 막을 수 없다.
막을 수 있는 쪽(생성자가 칸 · 줄 수뿐인 `EditorTextExtent`, 창 크기 입구)은 이미 형식으로 막았고, 이 게이트는 원 API 로 돌아가는 길만 본다.

위반(주석 · 문자열은 가린 글에서 본다):
  - `ImGui::SetNextItemWidth(` 인자에 0 · 1 이 아닌 숫자 리터럴이 있고 글꼴 기준(measureExtent · GetFontSize · CalcTextSize · GetFrameHeight · GetContentRegionAvail · GetStyle)이 없다
  - `ImGui::SetNextWindowSize(` 직접 호출(EditorChrome.cpp 와 SelfTest 제외)
  - `ImGui::BeginChild(` · `ImGui::BeginTable(` 의 크기 인자에 0 이 아닌 숫자 리터럴이 있고 글꼴 기준이 없다

기준선은 현재 건수다(`rules/CheckEditorSizeLiterals.toml`). 커밋 훅은 추가한 줄만, 전체 훑기는 기준선을 넘을 때만 실패한다.

  python Scripts/lint/gate/CheckEditorSizeLiterals.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--baseline N] [--list]
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankCommentsAndLiterals  # noqa: E402
from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402


_kScanRoot = ("Source/Editor", "Source/GameFramework/PlugIn/Editor")
_kSuffixes = (".h", ".hpp", ".inl", ".cc", ".cpp", ".cxx")
#: 크기를 ImGui 로 넘기는 구현 파일(칸 · 줄 수를 픽셀로 바꾸는 곳)과 테스트가 만드는 가짜 창(재현성을 위해 픽셀 고정) — 이 앞머리는 보지 않는다.
_kExemptPrefix = ("Source/Editor/Common/GUI/EditorChrome.", "Source/Editor/Common/GUI/EditorTextExtentImGui.", "Source/Editor/SelfTest/")
_kNumberRe = re.compile(r"(?<![\w.])(?:\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuUlL]*|\.\d+[fF]?)(?![\w.])")
_kFontBasisRe = re.compile(r"measureExtent|GetFontSize|CalcTextSize|GetFrameHeight|GetTextLineHeight|GetContentRegionAvail|GetStyle|GetWindowWidth|GetWindowHeight")
_kRuleHead: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("SetNextItemWidth", re.compile(r"\bImGui::SetNextItemWidth\s*\(")),
    ("SetNextWindowSize", re.compile(r"\bImGui::SetNextWindowSize\s*\(")),
    ("BeginChild", re.compile(r"\bImGui::BeginChild\s*\(")),
    ("BeginTable", re.compile(r"\bImGui::BeginTable\s*\(")),
)


@dataclass
class SizeLiteral:
    """위반 하나."""

    firstLine: int
    lastLine: int
    kind: str


def findCallEndInternal(masked: str, open_: int) -> int:
    """`masked[open_]` 가 `(` 일 때 짝 `)` 의 위치(없으면 -1). 게이트끼리 import 하지 않으려고(층 규칙) 작은 함수를 따로 둔다."""
    depth = 0
    for index in range(open_, len(masked)):
        if masked[index] in "([{":
            depth += 1
        elif masked[index] in ")]}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def splitArgumentsInternal(inner: str) -> list[str]:
    listArg: list[str] = []
    depth = 0
    begin = 0
    for index, char in enumerate(inner):
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            listArg.append(inner[begin:index])
            begin = index + 1
    listArg.append(inner[begin:])
    return listArg


def hasPixelLiteralInternal(text: str) -> bool:
    """0 · 1 이 아닌 숫자 리터럴이 있고 글꼴 기준이 없으면 true(0 은 채움 · 1 은 -1 같은 ImGui 규칙값)."""
    if _kFontBasisRe.search(text):
        return False
    return any(float(re.sub(r"[fFuUlL]+$", "", match.group(0))) not in (0.0, 1.0) for match in _kNumberRe.finditer(text))


def findSizeLiterals(text: str, masked: str | None = None) -> list[SizeLiteral]:
    masked = blankCommentsAndLiterals(text) if masked is None else masked
    if len(masked) != len(text):
        return []
    listFound: list[SizeLiteral] = []
    for kind, pattern in _kRuleHead:
        for match in pattern.finditer(masked):
            lineBegin = masked.rfind("\n", 0, match.start()) + 1
            if masked[lineBegin:match.start()].lstrip().startswith("#"):
                continue
            open_ = masked.find("(", match.start())
            close = findCallEndInternal(masked, open_)
            if close < 0:
                continue
            listArg = splitArgumentsInternal(masked[open_ + 1:close])
            if kind == "SetNextWindowSize":
                bViolation = True
            elif kind == "SetNextItemWidth":
                bViolation = hasPixelLiteralInternal(listArg[0])
            else:
                # BeginChild( id, size, ...) · BeginTable( id, column, flags, outer_size, ...) — 크기 인자만 본다.
                sizeIndex = 1 if kind == "BeginChild" else 3
                bViolation = len(listArg) > sizeIndex and hasPixelLiteralInternal(listArg[sizeIndex])
            if bViolation:
                listFound.append(SizeLiteral(text.count("\n", 0, match.start()) + 1, text.count("\n", 0, close) + 1, kind))
    return sorted(listFound, key=lambda item: item.firstLine)


class CheckEditorSizeLiteralsGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "에디터 코드의 픽셀 리터럴 크기(SetNextItemWidth · SetNextWindowSize · BeginChild/BeginTable)는 글자 칸 · 줄 수(EditorTextExtent)로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that editor code sizes windows and items in text columns and lines..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    sourceScanRoots = _kScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "에디터 코드의 픽셀 리터럴 크기"
    countedNoun = "size literals"
    baselineExceededHint = "새 크기는 EditorTextExtent 로"
    hint = ("  창은 EditorChrome::setNextPanelSize / setNextWindowExtent, 항목 폭은 EditorChrome::setNextItemWidthColumns,\n"
            "  child · table 은 measureExtent( EditorTextExtent::ofText( 칸, 줄 ) ) 를 쓴다. 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "픽셀 폭 · 창 크기", "expectedViolation": "2  Source/Editor/Panels/A.cpp", "files": {"Source/Editor/Panels/A.cpp": "void f()\n{\n    ImGui::SetNextItemWidth( 120.0f * dpi );\n    ImGui::SetNextWindowSize( ImVec2( 1, 2 ) );\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "child 픽셀 크기 · 글꼴 기준과 0 은 통과", "expectedViolation": "1  Source/Editor/Panels/B.cpp", "files": {"Source/Editor/Panels/B.cpp": "void f()\n{\n    ImGui::BeginChild( \"a\", ImVec2{ 220.0f, 0.0f } );\n    ImGui::BeginChild( \"b\", ImVec2{ 0.0f, 0.0f } );\n    ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );\n    ImGui::SetNextItemWidth( -1.0f );\n}\n"},
         "args": ["--baseline", "0"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        if source.relative.startswith(_kExemptPrefix):
            return None
        listLine = source.text.split("\n")
        return [CountedFinding("total", source.relative, literal.firstLine, literal.lastLine,
                               f"{literal.kind} 에 픽셀 리터럴 — 글자 칸 · 줄 수(EditorTextExtent)로 적는다", listLine[literal.firstLine - 1].strip())
                for literal in findSizeLiterals(source.text, source.masked)]


main = CheckEditorSizeLiteralsGate.run

if __name__ == "__main__":
    sys.exit(main())
