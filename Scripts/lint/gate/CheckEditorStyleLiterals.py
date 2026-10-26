#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckEditorStyleLiterals.py

에디터 코드(`Source/Editor/**` · `Source/GameFramework/PlugIn/Editor/**`)가 **숫자 리터럴로 색 · 스타일 값을 만들지 않게** 한다.
색 · 간격 · 모서리는 테마 토큰(`Resource/editor/theme/*.tokens.json`)이 원본이고, 코드는 `EditorThemeUtil` 접근자로 읽는다(docs/plans/EditorUX.md).
숫자로 박은 색은 테마를 바꿔도(라이트 · 고대비) 그대로 남아 읽을 수 없게 된다.

위반(주석 · 문자열은 가린 글에서 본다):
  - `IM_COL32(` · 첫 인자가 숫자인 `ImVec4(` / `ImColor(` / `Color{ <숫자>` / `Color( <숫자>` · `Color::fromBytes(`
  - `ImGui::PushStyleColor(` 의 값 인자에 숫자 리터럴이 있다
  - `ImGui::PushStyleVar(` 의 값 인자에 숫자 리터럴(`ImVec2(4, 4)` 포함)이 있다

테마 구현 파일(`rules/CheckEditorStyleLiterals.toml` 의 `[exemption]` 경로 앞머리)은 값을 만드는 곳이라 보지 않는다.

기준선은 현재 건수다(`rules/CheckEditorStyleLiterals.toml` 의 `[baseline] total`). 커밋 훅은 추가한 줄에 걸친 위반만, 전체 훑기는
기준선을 넘을 때만 실패한다 — 두 모드 · 병합 커밋 · `--whole-files` · `--list` 는 기반(`LintGate.BaselineSourceGate`)이 정한다.

  python Scripts/lint/gate/CheckEditorStyleLiterals.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--baseline N] [--list]
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
_kNumberRe = re.compile(r"(?<![\w.])(?:0[xX][0-9A-Fa-f]+|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuUlL]*|\.\d+[fF]?)(?![\w.])")
_kHeadNumberRe = r"\s*[-+]?(?:0[xX]|\d|\.\d)"
#: (이름, 머리 정규식, 인자 검사 방식) — "head": 머리만으로 위반, "second": 둘째 인자에 숫자 리터럴이 있으면 위반.
_kRuleHead: tuple[tuple[str, re.Pattern[str], str], ...] = (
    ("IM_COL32", re.compile(r"\bIM_COL32\s*\("), "head"),
    ("PushStyleColor", re.compile(r"\bImGui::PushStyleColor\s*\("), "second"),
    ("PushStyleVar", re.compile(r"\bImGui::PushStyleVar\s*\("), "second"),
    ("ImVec4", re.compile(r"\bImVec4\s*[({]" + _kHeadNumberRe), "head"),
    ("Color", re.compile(r"\b(?:Im)?Color\s*[({]" + _kHeadNumberRe), "head"),
    ("Color::fromBytes", re.compile(r"\bColor::fromBytes\s*\("), "head"),
)


@dataclass
class StyleLiteral:
    """위반 하나."""

    firstLine: int
    lastLine: int
    kind: str


def findCallEndInternal(masked: str, open_: int) -> int:
    """`masked[open_]` 가 `(` 일 때 짝 `)` 의 위치(없으면 -1)."""
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


def findStyleLiterals(text: str, masked: str | None = None) -> list[StyleLiteral]:
    """파일 하나에서 숫자 리터럴로 색 · 스타일 값을 만든 위치. 같은 줄은 하나로 센다(`masked` 는 이미 만든 `blankCommentsAndLiterals(text)`)."""
    masked = blankCommentsAndLiterals(text) if masked is None else masked
    if len(masked) != len(text):
        return []
    listFound: list[StyleLiteral] = []
    setLine: set[int] = set()
    for kind, pattern, mode in _kRuleHead:
        for match in pattern.finditer(masked):
            lineBegin = masked.rfind("\n", 0, match.start()) + 1
            if masked[lineBegin:match.start()].lstrip().startswith("#"):
                continue
            firstLine = text.count("\n", 0, match.start()) + 1
            lastLine = firstLine
            if mode == "second":
                open_ = masked.find("(", match.start())
                close = findCallEndInternal(masked, open_)
                if close < 0:
                    continue
                listArg = splitArgumentsInternal(masked[open_ + 1:close])
                if len(listArg) < 2 or not _kNumberRe.search(listArg[1]):
                    continue
                lastLine = text.count("\n", 0, close) + 1
            if firstLine in setLine:
                continue
            setLine.add(firstLine)
            listFound.append(StyleLiteral(firstLine, lastLine, kind))
    return sorted(listFound, key=lambda item: item.firstLine)


class CheckEditorStyleLiteralsGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "에디터 코드의 숫자 리터럴 색 · 스타일 값(IM_COL32 · ImVec4 · PushStyleColor/Var)은 테마 토큰으로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that editor code takes colors and style values from theme tokens..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    sourceScanRoots = _kScanRoot
    sourceSuffixes = _kSuffixes
    violationHeader = "에디터 코드의 숫자 리터럴 색 · 스타일 값"
    countedNoun = "style literals"
    baselineExceededHint = "새 값은 테마 토큰으로"
    hint = ("  테마 토큰(EditorThemeUtil 접근자)을 쓴다 — 색 · 간격 · 모서리의 원본은 Resource/editor/theme/*.tokens.json 이다.\n"
            "  없는 값이면 토큰을 더한다(docs/plans/EditorUX.md). 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "IM_COL32 와 숫자 ImVec4", "expectedViolation": "2  Source/Editor/Panels/A.cpp", "files": {"Source/Editor/Panels/A.cpp": "void f()\n{\n    auto c = IM_COL32(1, 2, 3, 255);\n    ImVec4 v = ImVec4(1.0f, 0.5f, 0, 1);\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "PushStyleColor 숫자 · PushStyleVar ImVec2", "expectedViolation": "1  Source/Editor/Panels/B.cpp", "files": {"Source/Editor/Panels/B.cpp": "void f()\n{\n    ImGui::PushStyleColor(ImGuiCol_Text,\n                          0xFF0000FFu);\n    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "--files + --whole-files (플러그인 쪽)", "expectedViolation": "Source/GameFramework/PlugIn/Editor/C.cpp:3: Color 에 숫자 리터럴", "files": {"Source/GameFramework/PlugIn/Editor/C.cpp": "void f()\n{\n    Color c = Color{ 255, 0, 0, 255 };\n}\n"},
         "args": ["--files", "Source/GameFramework/PlugIn/Editor/C.cpp", "--whole-files"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        for prefix in self.mapExemption:
            if source.relative.startswith(prefix):
                self.useExemption(prefix)
                return None
        listLine = source.text.split("\n")
        return [CountedFinding("total", source.relative, literal.firstLine, literal.lastLine,
                               f"{literal.kind} 에 숫자 리터럴 — 테마 토큰(EditorThemeUtil 접근자)을 쓴다", listLine[literal.firstLine - 1].strip())
                for literal in findStyleLiterals(source.text, source.masked)]


main = CheckEditorStyleLiteralsGate.run

if __name__ == "__main__":
    sys.exit(main())
