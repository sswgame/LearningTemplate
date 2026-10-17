#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckDataDrivenStructure.py

데이터 주도 구조를 어기는 줄이 새로 들어오지 못하게 막습니다(결정 기록 5-4 "유지보수성을 위한 데이터화" ①②, AGENTS.md Constants 절).
값의 정본은 데이터 파일이고 코드는 형식만 가진다. 필수 값이 없으면 조용히 기본값으로 가지 않고 오류로 멈춘다.

이 게이트는 최후의 안전망이다. 위반을 못 쓰게 하는 것은 형식(경로 형식 · Optional · nodiscard 로더 — docs/plans/DataDriven.md 2절)이 먼저이고,
형식으로 막힌 종류는 규칙 데이터에서 뺀다. 기준선은 0 으로 수렴해야 한다.

종류(정규식 · 범위 · 예외 · 기준선은 전부 `rules/CheckDataDrivenStructure.toml`):
  - `AssetPathLiteral`   : C++ 에 박힌 에셋 경로 리터럴(`"engine/shaders/forwardlit.hlsl"`).
  - `SilentFallback`     : 조용한 대체 — `*OrDefault(`, 폴백 인자, "using defaults" 로그, 삼킨 `except Exception`, `environ.get(…, "값")`, CMake `DEFAULT_VALUE`.
  - `DiscardedLoadResult`: `(void)xxx.load…(` 처럼 읽기 결과를 버리는 줄.
  - 보고만(`reportOnly`): `MagicNameLiteral`(게임의 `hashed_string( "…" )`, `{ "Default" }`), `LiteralGetDefault`(파이썬 `.get("키", 리터럴)`).

기준선은 `<종류>:<파일>` 마다 줄 수다. 파일 하나라도 넘으면 실패하고, 줄면 낮추라고 알린다. 커밋 훅(`--files`)은 넘긴 파일만 같은 기준선과 비교한다.
진척 표: `--list`(위반 줄 전부), `--summary`(영역 × 종류 표), `--print-baseline`(지금 트리로 [baseline] 을 다시 쓸 글).

한계: 정규식은 줄 하나만 본다. 여러 줄에 걸친 로그 문구, 실패 분기의 `Foo{}` 대입, CMake `ERROR_VARIABLE` 뒤 기본값 `set`, 같은 값의 이중 정본
(코드 상수 경로와 데이터 종류 등록표)은 잡지 못한다.

  python Scripts/lint/gate/CheckDataDrivenStructure.py [--root <repo>] [--files a.cpp] [--list] [--summary] [--print-baseline]
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments, normalizePath, runGit  # noqa: E402
from common.RuleData import kKindInteger, kKindText, kKindTextList  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

kRuleSchema = {"scanRoots": kKindTextList, "reportOnly": kKindTextList, "language": kKindText, "pattern": kKindText,
                "scope": kKindText, "bodyEscape": kKindText, "baseline": kKindInteger}
#: C++ 언어 이름 — 이 언어만 주석을 가린 글에서 본다(나머지는 `#` 줄을 뺀다).
_kMaskedLanguage = "cpp"


@dataclass
class Rule:
    """`[pattern]` 의 한 줄."""

    key: str
    kind: str
    language: str
    regex: re.Pattern[str]
    scope: str
    escape: re.Pattern[str] | None


@dataclass
class Finding:
    """위반 하나."""

    kind: str
    relative: str
    line: int
    label: str
    text: str


@dataclass
class RuleSet:
    """규칙 데이터를 읽어 컴파일한 것."""

    listScanRoot: tuple[str, ...]
    setReportOnly: set[str]
    mapLanguageEnding: dict[str, tuple[str, ...]]
    listRule: list[Rule]
    mapBaseline: dict[str, int]


def compileRegexInternal(key: str, text: str) -> re.Pattern[str]:
    try:
        return re.compile(text, re.MULTILINE)   # 파일 전체로 먼저 거를 때도 `^` · `$` 가 줄 단위로 맞게
    except re.error as exception:
        raise GateError(f"'{key}' 의 정규식을 읽을 수 없습니다 — {exception}") from exception


def buildRuleSet(data: dict) -> RuleSet:
    """규칙 데이터(`readRules` 결과)를 검사하고 컴파일합니다. 모양이 틀리면 `GateError`."""
    mapLanguageEnding = {name: tuple(text.split()) for name, text in data["language"].items()}
    listRule: list[Rule] = []
    for key, text in data["pattern"].items():
        listPart = key.split(":")
        if len(listPart) != 3 or not all(listPart):
            raise GateError(f"[pattern] '{key}' 는 '<종류>:<언어>:<이름>' 이어야 합니다")
        kind, language, _ = listPart
        if language not in mapLanguageEnding:
            raise GateError(f"[pattern] '{key}' 의 언어 '{language}' 가 [language] 에 없습니다")
        escapeText = data["bodyEscape"].get(key)
        listRule.append(Rule(key, kind, language, compileRegexInternal(key, text), data["scope"].get(key, ""),
                             compileRegexInternal(key, escapeText) if escapeText else None))
    for table in ("scope", "bodyEscape"):
        listUnknown = [key for key in data[table] if key not in data["pattern"]]
        if listUnknown:
            raise GateError(f"[{table}] 의 키가 [pattern] 에 없습니다: {', '.join(listUnknown)}")
    setKind = {rule.kind for rule in listRule}
    listUnknownReport = [kind for kind in data["reportOnly"] if kind not in setKind]
    if listUnknownReport:
        raise GateError(f"reportOnly 의 종류가 [pattern] 에 없습니다: {', '.join(listUnknownReport)}")
    return RuleSet(tuple(data["scanRoots"]), set(data["reportOnly"]), mapLanguageEnding, listRule, dict(data["baseline"]))


def findLanguageInternal(relative: str, mapLanguageEnding: dict[str, tuple[str, ...]]) -> str:
    for language, listEnding in mapLanguageEnding.items():
        if relative.endswith(listEnding):
            return language
    return ""


def hasBodyEscapeInternal(listLine: list[str], index: int, escape: re.Pattern[str]) -> bool:
    """`index` 줄 아래 더 깊이 들여 쓴 몸통에 `escape` 가 있는지."""
    indent = len(listLine[index]) - len(listLine[index].lstrip())
    for line in listLine[index + 1:]:
        if not line.strip():
            continue
        if len(line) - len(line.lstrip()) <= indent:
            return False
        if escape.search(line):
            return True
    return False


def findInText(relative: str, text: str, ruleSet: RuleSet) -> list[Finding]:
    """파일 하나의 위반을 찾습니다. 맞는 정규식이 하나도 없는 파일은 주석을 가리지 않고 바로 넘긴다(속도)."""
    language = findLanguageInternal(relative, ruleSet.mapLanguageEnding)
    listRule = [rule for rule in ruleSet.listRule
                if rule.language == language and (not rule.scope or fnmatch.fnmatchcase(relative, rule.scope)) and rule.regex.search(text)]
    if not listRule:
        return []
    masked = blankComments(text) if language == _kMaskedLanguage else text
    if len(masked) != len(text):
        return []
    listLine = masked.split("\n")
    listOriginal = text.split("\n")
    listFinding: list[Finding] = []
    for index, line in enumerate(listLine):
        if language != _kMaskedLanguage and line.lstrip().startswith("#"):
            continue
        for rule in listRule:
            if not rule.regex.search(line):
                continue
            if rule.escape is not None and hasBodyEscapeInternal(listLine, index, rule.escape):
                continue
            listFinding.append(Finding(rule.kind, relative, index + 1, rule.key.rsplit(":", 1)[1], listOriginal[index].strip()))
    return listFinding


def getArea(relative: str) -> str:
    """진척 표의 영역 — `Source/<모듈>`, 그 밖은 맨 앞 폴더."""
    listPart = relative.split("/")
    if listPart[0] == "Source" and len(listPart) > 2:
        return "/".join(listPart[:2])
    return listPart[0] if len(listPart) > 1 else "(root)"


def compareBaseline(mapCount: dict[str, int], mapBaseline: dict[str, int], setChecked: set[str] | None) -> tuple[list[str], list[str]]:
    """
    `<종류>:<파일>` → 줄 수를 기준선과 비교합니다. 넘은 것은 위반, 준 것은 참고다.
    `setChecked` 가 있으면(커밋 훅) 그 파일의 키만 본다.
    """
    listViolation: list[str] = []
    listNote: list[str] = []
    for key in sorted(set(mapCount) | set(mapBaseline)):
        if setChecked is not None and key.split(":", 1)[1] not in setChecked:
            continue
        count = mapCount.get(key, 0)
        baseline = mapBaseline.get(key, 0)
        if count > baseline:
            listViolation.append(f"{key}: {count}줄이 기준선 {baseline} 을 넘었다 — --list 로 줄을 찾아 데이터로 옮기거나 오류로 멈추게 고친다")
        elif count < baseline:
            listNote.append(f"{key}: 줄었다 — [baseline] 을 {count} 로 낮춘다" if count else f"{key}: 다 고쳤다 — [baseline] 에서 지운다")
    return listViolation, listNote


def formatSummaryInternal(listFinding: list[Finding], listKind: list[str]) -> list[str]:
    counter = Counter((getArea(finding.relative), finding.kind) for finding in listFinding)
    listArea = sorted({area for area, _ in counter})
    listRow = ["영역 | " + " | ".join(listKind)]
    for area in listArea:
        listRow.append(f"{area} | " + " | ".join(str(counter.get((area, kind), 0)) for kind in listKind))
    listRow.append("합계 | " + " | ".join(str(sum(counter.get((area, kind), 0) for area in listArea)) for kind in listKind))
    return listRow


class CheckDataDrivenStructureGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다(실패시키는 종류마다 하나 이상)."""

    description = "데이터 주도 구조 위반(코드의 에셋 경로 · 조용한 폴백 · 버린 읽기 결과)이 기준선을 넘지 못하게 막는다"
    buildComment = "Checking that asset paths and fallbacks stay out of code..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*", "Scripts/*", "cmake/*", "CMakeLists.txt")
    preCommitFileArgument = "--files"
    violationHeader = "데이터 주도 구조 위반"
    ruleSchema = kRuleSchema
    maxNoteShown = 30
    hint = ("  경로 · 값은 데이터 파일(XML · JSON · TOML)에 두고 코드는 형식만 가진다. 필수 값이 없으면 기본값으로 가지 말고 오류로 멈춘다\n"
            "  (AGENTS.md Constants 절, 결정 기록 5-4). 의도된 설계면 rules/CheckDataDrivenStructure.toml 의 [exemption] 에 \"<종류>:<경로>\" = \"이유\".")
    selfTestCases = [
        {"name": "코드에 박힌 에셋 경로", "expectedViolation": "AssetPathLiteral:Source/Probe/A.cpp: 1줄이 기준선 0 을 넘었다", "files": {"Source/Probe/A.cpp": "static const char* kPath = \"engine/shaders/probe.hlsl\";\n"},
         "args": ["--files", "Source/Probe/A.cpp"]},
        {"name": "OrDefault 조용한 대체", "expectedViolation": "SilentFallback:Source/Probe/B.cpp: 1줄이 기준선 0 을 넘었다", "files": {"Source/Probe/B.cpp": "void f()\n{\n    float c = readOrDefault( key, 1.0f );\n}\n"},
         "args": ["--files", "Source/Probe/B.cpp"]},
        {"name": "기본값으로 가는 로그", "expectedViolation": "SilentFallback:Source/Probe/C.cpp: 1줄이 기준선 0 을 넘었다", "files": {"Source/Probe/C.cpp": "void f()\n{\n    SW_LOG_WARNING( \"settings broken - using defaults\" );\n}\n"},
         "args": ["--files", "Source/Probe/C.cpp"]},
        {"name": "버린 읽기 결과", "expectedViolation": "DiscardedLoadResult:Source/Probe/D.cpp: 1줄이 기준선 0 을 넘었다", "files": {"Source/Probe/D.cpp": "void f()\n{\n    (void)_table.loadFromResource();\n}\n"},
         "args": ["--files", "Source/Probe/D.cpp"]},
        {"name": "삼킨 파이썬 예외", "expectedViolation": "SilentFallback:Scripts/probe/Probe.py: 1줄이 기준선 0 을 넘었다", "files": {"Scripts/probe/Probe.py": "def f() -> None:\n    try:\n        g()\n    except Exception:\n        pass\n"},
         "args": ["--files", "Scripts/probe/Probe.py"]},
        {"name": "CMake 기본값 인자", "expectedViolation": "SilentFallback:cmake/Probe.cmake: 1줄이 기준선 0 을 넘었다", "files": {"cmake/Probe.cmake": "function(sw_probe OUT_VAR DEFAULT_VALUE)\nendfunction()\n"},
         "args": ["--files", "cmake/Probe.cmake"]},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        parser.add_argument("--list", action="store_true", help="위반 줄을 종류 · 파일 순으로 전부 찍는다")
        parser.add_argument("--summary", action="store_true", help="영역 × 종류 표를 찍는다(구조 수정 진척 표)")
        parser.add_argument("--print-baseline", action="store_true", help="지금 트리로 [baseline] 을 다시 쓸 글을 찍는다")

    def listTargetFilesInternal(self, repositoryRoot: Path, ruleSet: RuleSet, listFileArgument: list[str] | None) -> list[Path]:
        """전체 훑기는 `git ls-files` 한 번으로 고른다(빌드 산출물 · 무시된 파일을 걷지 않는다). git 이 아니면(자가 테스트 트리) 걷는다."""
        listEnding = tuple(ending for listItem in ruleSet.mapLanguageEnding.values() for ending in listItem)
        if not listFileArgument and (repositoryRoot / ".git").exists():
            result = runGit(["ls-files", "--", *ruleSet.listScanRoot], cwd=repositoryRoot)
            if result.returnCode != 0:
                raise GateError(f"git ls-files 실패 — {result.stderr.strip()}")
            return [repositoryRoot / line for line in result.stdout.splitlines() if line.endswith(listEnding)]
        listDirRoot = [root for root in ruleSet.listScanRoot if not (repositoryRoot / root).is_file()]
        listPath = self.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=listDirRoot or ("",), bAnySuffix=True)
        if not listFileArgument:
            listPath += [repositoryRoot / root for root in ruleSet.listScanRoot if (repositoryRoot / root).is_file()]
        return [path for path in listPath if path.as_posix().endswith(listEnding)]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        ruleSet = buildRuleSet(self.readRules(self.name, self.ruleSchema))
        listPath = self.listTargetFilesInternal(repositoryRoot, ruleSet, args.files)
        mapExemptionGlob = {key: key.split(":", 1) for key in self.mapExemption}
        listCounted: list[Finding] = []
        setChecked: set[str] = set()
        for path, text in self.readFiles(listPath):
            relative = normalizePath(str(path.relative_to(repositoryRoot)))
            setChecked.add(relative)
            for key, (_, glob) in mapExemptionGlob.items():
                if fnmatch.fnmatchcase(relative, glob):
                    self.seeExemption(key)
            for finding in findInText(relative, text, ruleSet):
                key = self.findExemptionKey(f"{finding.kind}:{relative}")
                if key is not None:
                    self.useExemption(key)
                    continue
                listCounted.append(finding)

        listKind = list(dict.fromkeys(rule.kind for rule in ruleSet.listRule))
        mapCount = dict(Counter(f"{finding.kind}:{finding.relative}" for finding in listCounted if finding.kind not in ruleSet.setReportOnly))
        listViolation, listNote = compareBaseline(mapCount, ruleSet.mapBaseline, setChecked if args.files else None)
        listTotal = [f"{kind} {sum(1 for finding in listCounted if finding.kind == kind)}" for kind in listKind]
        summary = f"{len(setChecked)} files · " + " · ".join(listTotal)
        if not args.files:
            for kind in listKind:
                count = sum(1 for finding in listCounted if finding.kind == kind)
                if kind in ruleSet.setReportOnly:
                    listNote.append(f"{kind}: {count}줄(보고만 — 실패시키지 않는다)")
                else:
                    baseline = sum(value for key, value in ruleSet.mapBaseline.items() if key.startswith(f"{kind}:"))
                    listNote.append(f"{kind}: 기준선 {baseline} · 지금 {count}")
        if args.summary:
            self.maxNoteShown = 0
            listNote += formatSummaryInternal(listCounted, listKind)
        if args.list:
            self.maxNoteShown = 0
            listCounted.sort(key=lambda finding: (finding.kind, finding.relative, finding.line))
            listNote += [f"{finding.relative}:{finding.line}: {finding.kind} '{finding.label}' | {finding.text}" for finding in listCounted]
        if args.print_baseline:
            # 그대로 붙여 넣을 글이라 참고 목록의 머리표 없이 찍는다.
            print("\n".join(["[baseline]"] + [f"\"{key}\" = {value}" for key, value in sorted(mapCount.items())]))
        return GateResult(listViolation=listViolation, listNote=listNote, summary=summary)


main = CheckDataDrivenStructureGate.run

if __name__ == "__main__":
    sys.exit(main())
