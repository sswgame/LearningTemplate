#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckAcronymSpelling.py

약어 철자 게이트 — Pascal 낱말로 쓴 약어(`UiSystem` · `updateUi`)와 이어 붙은 대문자 약어(`RHIUIPass`)를 막습니다
(AGENTS.md "Function names" 규칙 2 · 3 · 4, 목록과 철자 판정은 `Scripts/lint/AcronymRegistry.py` 한 자리).

**강제는 약어 하나씩이다.** 트리에는 아직 바꾸지 않은 약어 철자가 수만 곳 있다. 그것을 모두 예외로 적지 않는다 —
등록부의 `kEnforced` 에 오른 약어(그 약어를 트리 전체에서 바꾼 커밋이 올린다)와 `--enforce` 로 넘긴 약어만 위반이고,
나머지는 요약 줄의 숫자로만 보인다(`--verbose` 면 약어별 숫자). 그래서 평소 종료 코드는 0 이다.

보는 것:
  - 식별자: 문자열 · 주석 · `#include` 줄 · `gv_` · `SW_*` · 서드파티 이름은 빼고 코드모드와 같은 판정(`respellName`)
  - 파일 · 폴더 이름: `kLintTargetRelDirs` 아래 경로의 각 조각(`Resource/` 는 소문자 규칙이 따로 있어 보지 않는다)
  - 이어 붙은 대문자 약어(규칙 4): 강제 약어가 낀 것만

사용법:
  python Scripts/lint/gate/CheckAcronymSpelling.py [--files <path> ...] [--enforce UI,GPU] [--verbose]
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · AcronymRegistry

import AcronymRegistry as registry  # noqa: E402
from common import kLintTargetRelDirs, kNotOurCodeDirNames  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 대문자 셋 이상이 이어진 이름 — 규칙 4 후보를 찾는 앞 거르기.
_kUpperRunRe = re.compile(r"[A-Z]{3,}")


def lineOfInternal(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def expandNameInternal(text: str, start: int, end: int) -> tuple[int, int]:
    while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
        start -= 1
    while end < len(text) and (text[end].isalnum() or text[end] == "_"):
        end += 1
    return start, end


class CheckAcronymSpellingGate(LintGate):
    description = "약어 철자 — 강제 약어(kEnforced · --enforce)의 Pascal 철자와 이어 붙은 대문자 약어를 막는다"
    buildComment = "Checking acronym spelling (enforced acronyms only)..."
    timeoutSeconds = 120
    violationHeader = "약어 철자 위반"
    hint = "고치기: py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --acronym <약어> --files <파일> (규칙: Scripts/lint/AcronymRegistry.py)"
    preCommitPattern = ("Source/*", "Test/*", "Tools/*")
    preCommitFileArgument = "--files"
    maxViolationShown = 200
    selfTestCases = [
        {"name": "강제 약어의 Pascal 타입 이름", "args": ["--enforce", "UI"],
         "files": {"Source/Probe/Probe.h": "class UiProbe {};\n"}},
        {"name": "강제 약어의 Pascal 함수 이름", "args": ["--enforce", "Gpu"],
         "files": {"Source/Probe/Probe.cpp": "void syncGpu() {}\n"}},
        {"name": "강제 약어의 파일 이름", "args": ["--enforce", "UI"],
         "files": {"Source/Probe/UiProbe.h": "int x = 0;\n"}},
        {"name": "이어 붙은 대문자 약어", "args": ["--enforce", "UI"],
         "files": {"Source/Probe/Probe.h": "class RHIUIPass {};\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        parser.add_argument("--enforce", action="append", default=None,
                            help="이 약어도 강제한다(UI · Gpu …, 쉼표로 여럿, `all` 은 전부) — 등록부의 kEnforced 에 더한다")
        parser.add_argument("--verbose", action="store_true", help="강제하지 않는 약어의 남은 철자 수를 약어별로 찍는다")
        parser.add_argument("--enforce-only", action="append", default=None,
                            help="등록부의 kEnforced 대신 이 약어만 강제한다(시험용 — 등록부 상태와 무관하게 '강제 전엔 보고만' 을 본다)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listProblem = registry.checkRegistry()
        if listProblem:
            raise GateError("등록부: " + " · ".join(listProblem))
        listEnforceArgument = [value for value in (args.enforce or []) if value.strip().lower() != "all"]
        try:
            setBase = set(registry.resolveAcronyms(args.enforce_only)) if args.enforce_only else set(registry.kEnforced)
            setEnforced = set(registry.kAcronym) if any(value.strip().lower() == "all" for value in (args.enforce or [])) \
                else setBase | (set(registry.resolveAcronyms(listEnforceArgument)) if listEnforceArgument else set())
        except ValueError as error:
            raise GateError(str(error)) from error

        listFile = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=registry.kCodeSuffix,
                                          excludedDirNames=kNotOurCodeDirNames)
        listViolation: list[str] = []
        counterPending: Counter = Counter()
        setSeenPath: set[str] = set()
        for path, text in self.readFiles(listFile):
            relPath = path.resolve().relative_to(repositoryRoot.resolve()).as_posix()
            listViolation += self.checkPathInternal(relPath, setEnforced, counterPending, setSeenPath)
            listViolation += self.checkTextInternal(relPath, text, setEnforced, counterPending)

        listNote = [f"{registry.toPascal(acronym)}: {count:,}곳" for acronym, count in counterPending.most_common()] if args.verbose else []
        enforcedText = ", ".join(sorted(setEnforced)) or "없음"
        summary = (f"{len(listFile)} files · 강제 약어 {enforcedText} · 강제 전 남은 Pascal 철자 {sum(counterPending.values()):,}곳"
                   f"(약어 {len(counterPending)}개 — 보고만)")
        return GateResult(listViolation=listViolation, listNote=listNote, summary=summary)

    @staticmethod
    def checkPathInternal(relPath: str, setEnforced: set[str], counterPending: Counter, setSeenPath: set[str]) -> list[str]:
        """경로 조각(폴더 · 파일 이름) — 폴더는 한 번만 센다."""
        listViolation: list[str] = []
        parts = relPath.split("/")
        for index, part in enumerate(parts):
            prefix = "/".join(parts[:index + 1])
            if prefix in setSeenPath:
                continue
            setSeenPath.add(prefix)
            for acronym in registry.changedAcronyms(part.partition(".")[0]):
                if acronym in setEnforced:
                    listViolation.append(f"{prefix}: 이름 '{part}' → '{registry.respellPathPart(part, (acronym,))}' (약어 {acronym})")
                else:
                    counterPending[acronym] += 1
        return listViolation

    @staticmethod
    def checkTextInternal(relPath: str, text: str, setEnforced: set[str], counterPending: Counter) -> list[str]:
        listViolation: list[str] = []
        masked = registry.maskCode(text)
        for site in registry.iterateNameSites(text, registry.kAcronym, masked=masked):
            if registry.isExternalName(site.name, site.qualifier):
                continue
            for acronym in registry.changedAcronyms(site.name):
                if acronym in setEnforced:
                    listViolation.append(f"{relPath}:{lineOfInternal(text, site.start)}: '{site.name}' → "
                                         f"'{registry.respellName(site.name, (acronym,))}' (약어 {acronym})")
                else:
                    counterPending[acronym] += 1
        if setEnforced:
            setSeenStart: set[int] = set()
            for match in _kUpperRunRe.finditer(masked):
                start, end = expandNameInternal(masked, match.start(), match.end())
                if start in setSeenStart:
                    continue
                setSeenStart.add(start)
                name = masked[start:end]
                if registry.isExternalName(name):
                    continue
                for run in registry.findAdjacentAcronyms(name):
                    if setEnforced & set(registry.splitIntoAcronyms(run)):
                        listViolation.append(f"{relPath}:{lineOfInternal(text, start)}: '{name}' — 대문자 약어 {run} 이 이어 붙었습니다"
                                             f"(한 쪽을 풀어 쓰거나 사이에 낱말을 둡니다, 규칙 4)")
        return listViolation


main = CheckAcronymSpellingGate.run


if __name__ == "__main__":
    sys.exit(main())
