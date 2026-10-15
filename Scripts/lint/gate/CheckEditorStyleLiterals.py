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

테마 구현 파일(`_kThemeSourcePrefix`)은 값을 만드는 곳이라 보지 않는다.

기준선은 현재 건수다. 모드가 둘이다.
  - 커밋 훅(`--files`): 이번 커밋이 **추가한 줄**(`git diff --cached -U0` 의 + 줄)에 걸친 위반만 본다. 병합 커밋(`MERGE_HEAD`)은 건너뛴다.
    `--whole-files` 를 더하면 파일 전체를 본다.
  - 전체 훑기(`--files` 없음, `ctest -L lint`): 건수를 `rules/CheckEditorStyleLiterals.toml` 의 `[baseline] total` 과 비교해 **늘면** 실패한다.
    줄면 기준선을 낮추라는 참고만 찍는다. `--list` 는 위반 전부를 찍는다.

  python Scripts/lint/gate/CheckEditorStyleLiterals.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--baseline N] [--list]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankCommentsAndLiterals, normalizePath, runGit  # noqa: E402
from common.RuleData import kKindInteger  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kScanRoot = ("Source/Editor", "Source/GameFramework/PlugIn/Editor")
_kSuffixes = (".h", ".hpp", ".inl", ".cc", ".cpp", ".cxx")
#: 테마 값을 만드는 파일 — 이 경로 앞머리에 맞으면 검사하지 않는다.
_kThemeSourcePrefix = ("Source/Editor/Common/GUI/EditorThemeUtil.", "Source/Editor/Common/GUI/EditorColorUtil.", "Source/Editor/Common/GUI/EditorThemeTokens.")
_kHunkRe = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
_kBaselineSchema = {"baseline": kKindInteger}
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


def findStyleLiterals(text: str) -> list[StyleLiteral]:
    """파일 하나에서 숫자 리터럴로 색 · 스타일 값을 만든 위치. 같은 줄은 하나로 센다."""
    masked = blankCommentsAndLiterals(text)
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


def parseAddedLines(diffText: str) -> dict[str, set[int]]:
    """`git diff --cached -U0` 출력 → 파일(저장소 기준) → 추가된 줄 번호 집합."""
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
    return mapAdded


class CheckEditorStyleLiteralsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "에디터 코드의 숫자 리터럴 색 · 스타일 값(IM_COL32 · ImVec4 · PushStyleColor/Var)은 테마 토큰으로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that editor code takes colors and style values from theme tokens..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "에디터 코드의 숫자 리터럴 색 · 스타일 값"
    ruleSchema = _kBaselineSchema
    maxNoteShown = 12
    hint = ("  테마 토큰(EditorThemeUtil 접근자)을 쓴다 — 색 · 간격 · 모서리의 원본은 Resource/editor/theme/*.tokens.json 이다.\n"
            "  없는 값이면 토큰을 더한다(docs/plans/EditorUX.md). 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "IM_COL32 와 숫자 ImVec4", "files": {"Source/Editor/Panels/A.cpp": "void f()\n{\n    auto c = IM_COL32(1, 2, 3, 255);\n    ImVec4 v = ImVec4(1.0f, 0.5f, 0, 1);\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "PushStyleColor 숫자 · PushStyleVar ImVec2", "files": {"Source/Editor/Panels/B.cpp": "void f()\n{\n    ImGui::PushStyleColor(ImGuiCol_Text,\n                          0xFF0000FFu);\n    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "--files + --whole-files (플러그인 쪽)", "files": {"Source/GameFramework/PlugIn/Editor/C.cpp": "void f()\n{\n    Color c = Color{ 255, 0, 0, 255 };\n}\n"},
         "args": ["--files", "Source/GameFramework/PlugIn/Editor/C.cpp", "--whole-files"]},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        parser.add_argument("--whole-files", action="store_true", help="--files 에서 추가한 줄이 아니라 파일 전체를 본다")
        parser.add_argument("--baseline", type=int, default=None, help="전체 훑기의 기준선을 이 값으로(기본은 rules 의 [baseline] total)")
        parser.add_argument("--list", action="store_true", help="전체 훑기에서 위반을 전부 찍는다")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        bFiles = bool(args.files)
        mapAdded: dict[str, set[int]] | None = None
        if bFiles and not args.whole_files:
            if (repositoryRoot / ".git" / "MERGE_HEAD").exists():
                return GateResult(summary="merge commit — skipped (the other side was checked on its own commit)")
            result = runGit(["diff", "--cached", "-U0", "--no-color", "--no-ext-diff", "--no-renames", "--diff-filter=ACM"], cwd=repositoryRoot)
            mapAdded = parseAddedLines(result.stdout) if result.returnCode == 0 else None   # git 이 답하지 못하면 파일 전체로 본다(놓치지 않는 쪽)
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=_kScanRoot, suffixes=_kSuffixes)
        listAll: list[tuple[str, StyleLiteral, str]] = []
        fileCount = 0
        for path, text in self.readFiles(listPath):
            relative = normalizePath(str(path.relative_to(repositoryRoot)))
            if relative.startswith(_kThemeSourcePrefix):
                continue
            fileCount += 1
            listLine = text.split("\n")
            for literal in findStyleLiterals(text):
                if mapAdded is not None:
                    setAdded = mapAdded.get(relative, set())
                    if not any(literal.firstLine <= number <= literal.lastLine for number in setAdded):
                        continue
                listAll.append((relative, literal, listLine[literal.firstLine - 1].strip()))

        def describe(item: tuple[str, StyleLiteral, str]) -> str:
            relative, literal, line = item
            return f"{relative}:{literal.firstLine}: {literal.kind} 에 숫자 리터럴 — 테마 토큰(EditorThemeUtil 접근자)을 쓴다 | {line}"

        summary = f"{fileCount} files, {len(listAll)} style literals"
        if bFiles:
            return GateResult(listViolation=[describe(item) for item in listAll], summary=summary)

        baseline = args.baseline if args.baseline is not None else self.readRules(self.name, self.ruleSchema)["baseline"]["total"]
        mapPerFile: dict[str, int] = {}
        for relative, _, _ in listAll:
            mapPerFile[relative] = mapPerFile.get(relative, 0) + 1
        listTop = sorted(mapPerFile.items(), key=lambda item: (-item[1], item[0]))[:10]
        listNote = [f"기준선 {baseline} · 지금 {len(listAll)} · 파일 {len(mapPerFile)}개"] + [f"{count:4d}  {relative}" for relative, count in listTop]
        if len(listAll) < baseline:
            listNote.append(f"줄었다 — rules/{self.name}.toml 의 [baseline] total 을 {len(listAll)} 로 낮춘다")
        if args.list:
            self.maxNoteShown = 0
            listNote += [describe(item) for item in listAll]
        listViolation: list[str] = []
        if len(listAll) > baseline:
            listViolation.append(f"전체 {len(listAll)}건이 기준선 {baseline} 을 넘었다 — 새 값은 테마 토큰으로 (--list 로 위치 확인), 정말 줄었다면 기준선을 맞춘다")
        return GateResult(listViolation=listViolation, listNote=listNote, summary=summary)


main = CheckEditorStyleLiteralsGate.run

if __name__ == "__main__":
    sys.exit(main())
