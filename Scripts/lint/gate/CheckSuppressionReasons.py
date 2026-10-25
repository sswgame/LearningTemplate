#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
코드 안의 경고 · 분석기 억제는 **무엇을 왜** 끄는지 든다.

  1) clang-tidy `NOLINT` · `NOLINTNEXTLINE` · `NOLINTBEGIN` 은 검사 이름을 괄호로 적는다(`NOLINT` 맨이름은 모든 검사를 끈다).
  2) 위 셋과 `#pragma clang|GCC diagnostic ignored "…"` 는 이유를 든다 — 같은 줄 끝(`— 이유` · `// 이유`)이나 **바로 윗줄의 `//` 주석**.
     `NOLINTEND` 는 짝의 이유를 따른다.
  3) CMake 의 경고 끄기(`-Wno-…` · `/wd…`)는 같은 줄에 `#` 이유를 든다(`cmake/Modules/Compiler/`).

억제를 지우는 것보다 남기는 것이 쉬운 쪽으로 기울면 한 번 맞았던 이유가 낡은 채 남는다 — `RHIDxgiFormat.h` 의 `-Wswitch-enum` pragma 는
CMake 가 이미 전역으로 끈 경고를 다시 끄고 있었다. 이유를 적게 하면 다음 사람이 이유가 아직 맞는지 볼 수 있다. 두 플랫폼은 같은 경고 집합(`-Wall -Wextra`,
clang-cl 에는 `-clang:` 으로 넘긴다)을 쓴다 — pragma 를 더하기 전에 `Clang.cmake` 로 그 경고가 켜져 있는지 본다.

  python Scripts/lint/gate/CheckSuppressionReasons.py [--root <repo>] [--files a.cpp b.cmake]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, SourceScanSet, joinFindings  # noqa: E402

_kCppSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
_kListCmakeRoot = ("cmake/Modules/Compiler",)

_kNolintRe = re.compile(r"//\s*(?P<kind>NOLINT(?:NEXTLINE|BEGIN|END)?)\b(?P<checks>\([^)]*\))?(?P<tail>.*)$")
_kPragmaRe = re.compile(r"^\s*#\s*pragma\s+(?:clang|GCC)\s+diagnostic\s+ignored\s+\"[^\"]+\"(?P<tail>.*)$")
_kCmakeWarningOffRe = re.compile(r"(?<![\w-])(?:-Wno-[\w+-]+|/wd\d+)\b")
#: 줄 끝 이유 — 대시 · 쌍점 · `//` 뒤에 글자가 있다.
_kTailReasonRe = re.compile(r"^\s*(?:—|-|:|//)\s*\S")


def hasReasonInternal(listLine: list[str], index: int, tail: str) -> bool:
    """줄 끝 꼬리나 바로 윗줄 주석에 이유가 있는가."""
    if _kTailReasonRe.match(tail):
        return True
    if index == 0:
        return False
    above = listLine[index - 1].strip()
    return above.startswith("//") and "NOLINT" not in above and "#pragma" not in above


def findCppViolations(relPath: str, text: str) -> tuple[list[str], int]:
    """C++ 파일 하나의 NOLINT · pragma 억제를 보고 (위반, 억제 수)를 돌려줍니다."""
    listViolation: list[str] = []
    count = 0
    listLine = text.splitlines()
    for index, line in enumerate(listLine):
        nolint = _kNolintRe.search(line)
        if nolint is not None:
            count += 1
            if nolint["kind"] == "NOLINTEND":
                continue
            if not nolint["checks"]:
                listViolation.append(f"{relPath}:{index + 1}: {nolint['kind']} 에 검사 이름이 없습니다 — {nolint['kind']}(검사-이름)")
            elif not hasReasonInternal(listLine, index, nolint["tail"]):
                listViolation.append(f"{relPath}:{index + 1}: {nolint['kind']} 에 이유가 없습니다 — 줄 끝 '— 이유' 나 바로 윗줄 주석")
            continue
        pragma = _kPragmaRe.match(line)
        if pragma is not None:
            count += 1
            if not hasReasonInternal(listLine, index, pragma["tail"]):
                listViolation.append(f"{relPath}:{index + 1}: 경고를 끄는 pragma 에 이유가 없습니다 — 바로 윗줄 주석")
    return listViolation, count


def findCmakeViolations(relPath: str, text: str) -> tuple[list[str], int]:
    """CMake 파일 하나의 경고 끄기 줄을 보고 (위반, 줄 수)를 돌려줍니다."""
    listViolation: list[str] = []
    count = 0
    for index, line in enumerate(text.splitlines()):
        code, _, comment = line.partition("#")
        if _kCmakeWarningOffRe.search(code) is None:
            continue
        count += 1
        if not comment.strip():
            listViolation.append(f"{relPath}:{index + 1}: 경고 끄기에 이유가 없습니다 — 같은 줄에 '# 이유'")
    return listViolation, count


class CheckSuppressionReasonsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "NOLINT · diagnostic ignored pragma · CMake 경고 끄기가 검사 이름과 이유를 드는지 검사"
    buildComment = "Checking that warning and analyzer suppressions carry a check name and a reason..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs) + tuple(f"{root}/*" for root in _kListCmakeRoot)
    preCommitFileArgument = "--files"
    violationHeader = "이유 없는 억제"
    hint = ("  // NOLINTNEXTLINE(bugprone-branch-clone) — 순서가 규칙이다\n"
            "  -Wno-padded # 64비트 정렬 패딩 허용\n"
            "  이유가 더는 맞지 않으면 억제를 지웁니다.")
    selfTestCases = [
        {"name": "검사 이름 없는 NOLINT", "expectedViolation": "Source/Probe/A.cpp:1: NOLINT 에 검사 이름이 없습니다", "files": {"Source/Probe/A.cpp": "int a = 0; // NOLINT\n"}},
        {"name": "이유 없는 NOLINTNEXTLINE", "expectedViolation": "Source/Probe/B.cpp:3: NOLINTNEXTLINE 에 이유가 없습니다",
         "files": {"Source/Probe/B.cpp": "int f()\n{\n    // NOLINTNEXTLINE(bugprone-branch-clone)\n    return 0;\n}\n"}},
        {"name": "이유 없는 pragma", "expectedViolation": "Source/Probe/C.h:3: 경고를 끄는 pragma 에 이유가 없습니다", "files": {"Source/Probe/C.h": "#pragma once\nint x;\n#pragma clang diagnostic ignored \"-Wswitch-enum\"\n"}},
        {"name": "이유 없는 CMake 경고 끄기", "expectedViolation": "cmake/Modules/Compiler/Probe.cmake:2: 경고 끄기에 이유가 없습니다", "files": {"cmake/Modules/Compiler/Probe.cmake": "target_compile_options(x INTERFACE\n\t-Wno-padded\n)\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    sourceScanSets = (SourceScanSet("cpp", kLintTargetRelDirs, _kCppSuffixes), SourceScanSet("cmake", _kListCmakeRoot, (".cmake",)))

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        find = findCmakeViolations if source.setName == "cmake" else findCppViolations
        listViolation, count = find(source.relative, source.text)
        return FileFindings(listViolation=listViolation, value=(source.setName, count))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        mapCount = {"cpp": 0, "cmake": 0}
        for findings in result.listFindings:
            setName, count = findings.value
            mapCount[setName] += count
        return GateResult(listViolation=listViolation, summary=f"C++ 억제 {mapCount['cpp']} · CMake 경고 끄기 {mapCount['cmake']}")


main = CheckSuppressionReasonsGate.run

if __name__ == "__main__":
    sys.exit(main())
