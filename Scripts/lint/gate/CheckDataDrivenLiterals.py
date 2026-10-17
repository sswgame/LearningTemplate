#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckDataDrivenLiterals.py

튜닝값 · 기본값이 코드로 다시 들어오지 못하게 막는다(결정 기록 5-4 "유지보수성을 위한 데이터화", AGENTS.md Constants 절).

규칙 둘:
  - `AttributeFallback`: 데이터 속성을 읽으며 리터럴 기본값을 채우거나(`getAttributeFloat( "speed", 3.0f )`) 범위를 조용히 자르는 줄
    (`MathUtil::max( 0, node.getAttributeInt( … ) )`). 스키마(리플렉션 Def 의 초기값 · `Required` · `Min`/`Max`)가 정하고
    `GameDataXML::readFields` 가 로드 시점에 검증한다. 대상은 Source 전체.
  - `TuningConstexpr`: 게임 · 키트 코드의 실수 `constexpr` 상수(`static constexpr float32 kSpeed = 3.5f;`) — 게임플레이 튜닝값은
    PROPERTY 나 키트 설정 · 카탈로그 칸이다. 대상은 `Source/Games` · `Source/GameFramework/Kits`. 불변식(수학 상수 · 형식 상한 등)이면
    예외 표에 이유와 함께 적는다.

모드가 둘이다(CheckConditionParts 와 같다).
  - 커밋 훅(`--files`): 이번 커밋이 **추가한 줄**만 본다. 예외 키는 `<규칙>:<경로>:<이름>`(`TuningConstexpr` 은 상수 이름, `AttributeFallback` 은 속성 이름).
  - 전체 훑기(`--files` 없음): 규칙마다 건수를 세어 `rules/CheckDataDrivenLiterals.toml` 의 `[baseline]` 을 **넘으면** 실패한다(줄면 낮추라는 참고).

  python Scripts/lint/gate/CheckDataDrivenLiterals.py [--root <repo>] [--files a.cpp] [--whole-files] [--list]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments, runGit  # noqa: E402
from common.RuleData import kKindInteger  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402
from SourceScan import FileFindings, SourceFile, SourceScanResult  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".cpp")
_kScanRoots = ("Source",)
_kTuningRoots = ("Source/Games/", "Source/GameFramework/Kits/")
_kLiteral = r'(?:-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuU]?|true|false|"[^"]*")'
_kFallbackRe = re.compile(r'getAttribute(?:Float|Int|Int64|Uint|Bool|Text)?\(\s*"(?P<name>[^"]+)"\s*,\s*' + _kLiteral + r'\s*\)')
_kClampRe = re.compile(r'MathUtil::(?:max|min|clamp|saturate)\s*\((?P<body>[^;]*?)getAttribute\w*\(\s*"(?P<name>[^"]+)"')
_kTuningRe = re.compile(r'\bconstexpr\s+(?:float32|float64|float|double)\s+(?P<name>k\w+)\s*(?:=|\{)\s*-?\d')
_kHunkRe = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
_kRuleSchema = {"baseline": kKindInteger}
_kListRule = ("AttributeFallback", "TuningConstexpr")


@dataclass
class Finding:
    """위반 하나."""

    rule: str
    relative: str
    line: int
    name: str
    text: str


def findInText(relative: str, text: str, masked: str | None = None) -> list[Finding]:
    """파일 하나의 위반 후보를 찾습니다(주석은 가린 글에서 본다 — `masked` 는 이미 만든 `blankComments(text)`)."""
    masked = blankComments(text) if masked is None else masked
    if len(masked) != len(text):
        return []
    listFinding: list[Finding] = []
    listLine = masked.split("\n")
    listOriginal = text.split("\n")
    bTuningScope = relative.startswith(_kTuningRoots)
    for index, line in enumerate(listLine):
        for match in _kFallbackRe.finditer(line):
            listFinding.append(Finding("AttributeFallback", relative, index + 1, match.group("name"), listOriginal[index].strip()))
        for match in _kClampRe.finditer(line):
            listFinding.append(Finding("AttributeFallback", relative, index + 1, match.group("name"), listOriginal[index].strip()))
        if bTuningScope:
            for match in _kTuningRe.finditer(line):
                listFinding.append(Finding("TuningConstexpr", relative, index + 1, match.group("name"), listOriginal[index].strip()))
    return listFinding


def parseAddedLines(diffText: str) -> dict[str, set[int]]:
    """`git diff --cached -U0` 출력 → 파일 → 추가된 줄 번호."""
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


class CheckDataDrivenLiteralsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "데이터 속성의 리터럴 기본값 · 조용한 범위 자르기와 게임 · 키트의 실수 튜닝 상수를 막는다(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that tuning values and attribute defaults stay in data..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoots)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kScanRoots
    sourceSuffixes = _kSuffixes
    violationHeader = "데이터로 옮길 리터럴"
    ruleSchema = _kRuleSchema
    maxNoteShown = 12
    hint = ("  속성 기본값 · 필수 · 범위는 리플렉션 Def 의 초기값 · PROPERTY( Required, Min, Max ) 로 적고 GameDataXML::readFields 로 읽는다.\n"
            "  게임플레이 튜닝값은 컴포넌트 PROPERTY(기본값은 생성자) 나 키트 설정 · 카탈로그 칸이다. 불변식이면 rules 의 [exemption] 에\n"
            "  \"<규칙>:<경로>:<이름>\" = \"이유\" 로 적는다.")
    selfTestCases = [
        {"name": "속성 리터럴 기본값", "files": {"Source/Probe/A.cpp": "void f( const XMLNode& node )\n{\n    float s = node.getAttributeFloat( \"speed\", 3.0f );\n}\n"},
         "args": ["--files", "Source/Probe/A.cpp", "--whole-files"]},
        {"name": "속성 조용한 자르기", "files": {"Source/Probe/B.cpp": "void f( const XMLNode& node )\n{\n    int c = MathUtil::max( 1, node.getAttributeInt( \"capacity\", c ) );\n}\n"},
         "args": ["--files", "Source/Probe/B.cpp", "--whole-files"]},
        {"name": "게임 실수 튜닝 상수", "files": {"Source/Games/Probe/C.cpp": "struct X\n{\n    static constexpr float32 kWalkSpeed = 3.5f;\n};\n"},
         "args": ["--files", "Source/Games/Probe/C.cpp", "--whole-files"]},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        parser.add_argument("--whole-files", action="store_true", help="--files 에서 추가한 줄이 아니라 파일 전체를 본다")
        parser.add_argument("--list", action="store_true", help="전체 훑기에서 위반을 전부 찍는다")

    def prepareSourceScan(self, repositoryRoot: Path, args: argparse.Namespace) -> None:
        args.bMergeSkip = False
        args.mapAdded = None
        if bool(args.files) and not args.whole_files:
            if (repositoryRoot / ".git" / "MERGE_HEAD").exists():
                args.bMergeSkip = True
                return
            result = runGit(["diff", "--cached", "-U0", "--no-color", "--no-ext-diff", "--no-renames", "--diff-filter=ACM"], cwd=repositoryRoot)
            args.mapAdded = parseAddedLines(result.stdout) if result.returnCode == 0 else None

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        if args.bMergeSkip:
            return None
        relative = source.relative
        listCounted: list[Finding] = []
        for finding in findInText(relative, source.text, source.noComment):
            key = f"{finding.rule}:{finding.relative}:{finding.name}"
            if key in self.mapExemption:
                self.useExemption(key)
                continue
            if args.mapAdded is not None and finding.line not in args.mapAdded.get(relative, set()):
                continue
            listCounted.append(finding)
        return FileFindings(value=listCounted) if listCounted else None

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        if args.bMergeSkip:
            return GateResult(summary="merge commit — skipped (the other side was checked on its own commit)")
        bFiles = bool(args.files)
        fileCount = result.fileCount
        listCounted: list[Finding] = [finding for findings in result.listFindings for finding in (findings.value or ())]

        def describe(finding: Finding) -> str:
            return f"{finding.relative}:{finding.line}: {finding.rule} '{finding.name}' | {finding.text}"

        summary = f"{fileCount} files, {len(listCounted)} literals"
        if bFiles:
            return GateResult(listViolation=[describe(finding) for finding in listCounted], summary=summary)

        mapBaseline = self.readRules(self.name, self.ruleSchema)["baseline"]
        listNote: list[str] = []
        listViolation: list[str] = []
        for rule in _kListRule:
            count = sum(1 for finding in listCounted if finding.rule == rule)
            baseline = mapBaseline[rule]   # 규칙마다 기준선을 적는다 — 없으면 KeyError(0 으로 읽으면 새 규칙이 처음부터 전부 위반이 된다)
            listNote.append(f"{rule}: 기준선 {baseline} · 지금 {count}")
            if count < baseline:
                listNote.append(f"  줄었다 — rules/{self.name}.toml 의 [baseline] {rule} 를 {count} 로 낮춘다")
            if count > baseline:
                listViolation.append(f"{rule} {count}건이 기준선 {baseline} 을 넘었다 — 새 줄을 데이터로 옮기거나(--list 로 위치 확인) 불변식이면 예외 표에 이유와 함께 적는다")
        if args.list:
            self.maxNoteShown = 0
            listNote += [describe(finding) for finding in listCounted]
        return GateResult(listViolation=listViolation, listNote=listNote, summary=summary)


main = CheckDataDrivenLiteralsGate.run

if __name__ == "__main__":
    sys.exit(main())
