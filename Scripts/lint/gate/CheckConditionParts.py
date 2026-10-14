#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckConditionParts.py

`if` · `else if` · `while` 의 조건이 `&&` · `||` 로 세 부분 이상이면 먼저 이름 붙인 지역 변수에 담는다(AGENTS.md '분기문 규칙').

  나쁨:  if ( bArmed && bHasAmmo && bNotReloading )
  좋음:  const bool bCanFire = bArmed && bHasAmmo && bNotReloading;
         if ( bCanFire )

기준선은 0 이다(기존 위반을 모두 고쳤다). 모드가 둘이다.
  - 커밋 훅(`--files`): 이번 커밋이 **추가한 줄**(`git diff --cached -U0` 의 + 줄)이 속한 조건만 본다.
    병합 커밋(`MERGE_HEAD` 가 있다)은 건너뛴다 — 상대 쪽 줄은 그 브랜치 커밋 때 이미 검사됐다.
    `--whole-files` 를 더하면 추가한 줄이 아니라 파일 전체를 본다(고치는 중 확인용).
  - 전체 훑기(`--files` 없음, `ctest -L lint`): 건수를 세어 `rules/CheckConditionParts.toml` 의 `[baseline] total` 을 **넘으면** 실패한다(늘면 실패).
    줄어들면 기준선을 낮추라는 참고만 찍는다. `--list` 는 위반 전부를 찍는다.

판정(보수적으로 — 놓치는 쪽으로 틀린다):
  - 주석 · 문자열 · 문자 리터럴은 가린 글에서 본다. 조건은 `if` / `while` 뒤 `(` 부터 짝 `)` 까지(여러 줄 가능), 전처리기 줄(`#if`)은 뺀다.
  - 괄호 `( ) [ ] { }` 안의 `&&` · `||` 는 세지 않는다 — 괄호 안 부분식은 한 부분이다.
  - 꺾쇠(템플릿 인자)는 따라가지 않는다. 대신 `&&` 는 **앞이 피연산자의 끝(식별자 · 숫자 · `)` · `]` · 리터럴)이고 뒤가 `> , ) ;` 가 아닐 때만** 연산자로 본다 —
    `static_cast<T&&>( x )` · `Foo<T&&, U>` 의 오른값 참조를 건너뛰기 위해서다. `||` 는 늘 연산자다.
  - 부분 수 = 최상위 연산자 수 + 1. 3 개 이상이 위반.

  python Scripts/lint/gate/CheckConditionParts.py [--root <repo>] [--files a.cpp b.h] [--whole-files] [--list]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments, blankCommentsAndLiterals, kLintTargetRelDirs, normalizePath, runGit  # noqa: E402
from common.RuleData import kKindInteger  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
_kConditionHeadRe = re.compile(r"\b(if|while)\b\s*(?:constexpr\b\s*)?\(")
_kHunkRe = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
_kMinPartCount = 3
#: 앞쪽 피연산자가 끝나는 글자 — 이 뒤의 `&&` 만 논리 연산자다(오른값 참조 `T&&` 는 앞이 타입 이름이라 식별자 끝으로 보이지만 뒤가 `> , )` 로 막힌다).
_kOperandEnd = set("_)]\"'")
_kBaselineSchema = {"baseline": kKindInteger}


@dataclass
class LongCondition:
    """부분이 많은 조건 하나."""

    firstLine: int      #: `if` / `while` 의 줄(1 부터)
    lastLine: int       #: 닫는 `)` 의 줄
    keyword: str
    partCount: int


def _isOperandEndInternal(char: str) -> bool:
    return char.isalnum() or char in _kOperandEnd


def countConditionParts(masked: str, noComment: str, open_: int, close: int) -> int:
    """`masked[open_]` 가 `(`, `masked[close]` 가 짝 `)` 일 때 조건의 부분 수(최상위 `&&` · `||` 수 + 1)."""
    depth = 0
    operatorCount = 0
    index = open_ + 1
    while index < close:
        char = masked[index]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif depth == 0 and (masked.startswith("&&", index) or masked.startswith("||", index)):
            bOperator = True
            if char == "&":
                before = index - 1
                while before > open_ and noComment[before].isspace():
                    before -= 1
                after = index + 2
                while after < close and noComment[after].isspace():
                    after += 1
                bOperator = _isOperandEndInternal(noComment[before]) and not (after < close and noComment[after] in ">,);")
                bOperator = bOperator and after < close
            if bOperator:
                operatorCount += 1
            index += 2
            continue
        index += 1
    return operatorCount + 1


def findLongConditions(text: str) -> list[LongCondition]:
    """파일 하나에서 부분이 세 개 이상인 `if` · `while` 조건을 찾습니다."""
    masked = blankCommentsAndLiterals(text)
    noComment = blankComments(text)
    if len(masked) != len(text) or len(noComment) != len(text):
        return []
    listFound: list[LongCondition] = []
    for match in _kConditionHeadRe.finditer(masked):
        lineBegin = masked.rfind("\n", 0, match.start()) + 1
        if masked[lineBegin:match.start()].lstrip().startswith("#"):
            continue       # `#if ( … )` 는 전처리기 식이다
        open_ = match.end() - 1
        depth = 0
        close = -1
        for index in range(open_, len(masked)):
            char = masked[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    close = index
                    break
        if close < 0:
            continue
        partCount = countConditionParts(masked, noComment, open_, close)
        if partCount >= _kMinPartCount:
            listFound.append(LongCondition(text.count("\n", 0, match.start()) + 1, text.count("\n", 0, close) + 1, match.group(1), partCount))
    return listFound


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


class CheckConditionPartsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "if · while 조건이 && · || 로 세 부분 이상이면 이름 붙인 지역 변수로(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that if / while conditions with three or more parts are named first..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    preCommitFileArgument = "--files"
    violationHeader = "조건 부분이 3 개 이상인 if / while"
    ruleSchema = _kBaselineSchema
    maxNoteShown = 12
    hint = ("  const bool bCanFire = bArmed && bHasAmmo && bNotReloading;\n  if ( bCanFire )\n  {\n      fire();\n  }\n"
            "  괄호 안 부분식은 한 부분이다 — 묶어서 `bool` 하나로 이름 붙여도 된다. 전체를 보려면 --whole-files 또는 --list.")
    selfTestCases = [
        {"name": "한 줄 세 부분", "files": {"Source/Probe/A.cpp": "void f()\n{\n    if ( bArmed && bHasAmmo && bNotReloading )\n    {\n        fire();\n    }\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "여러 줄 + else if || 섞임", "files": {"Source/Probe/B.cpp": "void f()\n{\n    if ( a )\n    {\n    }\n    else if ( bX\n              || bY\n              && bZ )\n    {\n    }\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "while 세 부분", "files": {"Test/Probe/C.cpp": "void f()\n{\n    while ( a < 1 && b || !c )\n    {\n    }\n}\n"}, "args": ["--baseline", "0"]},
        {"name": "--files + --whole-files", "files": {"Source/Probe/D.cpp": "void f()\n{\n    if ( a && b && c )\n    {\n    }\n}\n"},
         "args": ["--files", "Source/Probe/D.cpp", "--whole-files"]},
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
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=_kSuffixes)
        listAll: list[tuple[str, LongCondition, str]] = []
        fileCount = 0
        for path, text in self.readFiles(listPath):
            fileCount += 1
            relative = normalizePath(str(path.relative_to(repositoryRoot)))
            listLine = text.split("\n")
            for condition in findLongConditions(text):
                if mapAdded is not None:
                    setAdded = mapAdded.get(relative, set())
                    if not any(condition.firstLine <= number <= condition.lastLine for number in setAdded):
                        continue
                listAll.append((relative, condition, listLine[condition.firstLine - 1].strip()))

        def describe(item: tuple[str, LongCondition, str]) -> str:
            relative, condition, line = item
            return f"{relative}:{condition.firstLine}: {condition.keyword} 조건이 {condition.partCount} 부분 — 먼저 이름 붙인 `const bool` 로 | {line}"

        summary = f"{fileCount} files, {len(listAll)} long conditions"
        if bFiles:
            return GateResult(listViolation=[describe(item) for item in listAll], summary=summary)

        baseline = args.baseline if args.baseline is not None else self.readRules(self.name, self.ruleSchema)["baseline"].get("total", 0)
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
            listViolation.append(f"전체 {len(listAll)}건이 기준선 {baseline} 을 넘었다 — 새 조건을 이름 붙이거나 (--list 로 위치 확인) 정말 줄었다면 기준선을 맞춘다")
        return GateResult(listViolation=listViolation, listNote=listNote, summary=summary)


main = CheckConditionPartsGate.run

if __name__ == "__main__":
    sys.exit(main())
