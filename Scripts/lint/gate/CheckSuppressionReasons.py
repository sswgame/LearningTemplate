#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
코드 안의 경고 · 분석기 억제는 **무엇을 왜** 끄는지 든다.

  1) clang-tidy `NOLINT` · `NOLINTNEXTLINE` · `NOLINTBEGIN` 은 검사 이름을 괄호로 적는다(`NOLINT` 맨이름은 모든 검사를 끈다).
  2) 위 셋과 `#pragma clang|GCC diagnostic ignored "…"` 는 이유를 든다 — 같은 줄 끝(`— 이유` · `// 이유`)이나 **바로 윗줄의 `//` 주석**.
     `NOLINTEND` 는 짝의 이유를 따른다.
  3) CMake 의 경고 끄기(`-Wno-…` · `/wd…`)는 같은 줄에 `#` 이유를 든다(`cmake/Modules/Compiler/`).

억제를 지우는 것보다 남기는 것이 쉬운 쪽으로 기울면 한 번 맞았던 이유가 낡은 채 남는다 — `RHIDxgiFormat.h` 의 `-Wswitch-enum` pragma 는
CMake 가 이미 전역으로 끈 경고를 다시 끄고 있었다. 이유를 적게 하면 다음 사람이 이유가 아직 맞는지 볼 수 있다. clang-cl 의 `-Wall` 은
`-Weverything` 이라 Windows 에서 끄는 경고가 리눅스(GNU 드라이버)에서는 애초에 켜져 있지 않을 수 있다 — pragma 를 더하기 전에 `Clang.cmake` 를 본다.

  python Scripts/lint/gate/CheckSuppressionReasons.py [--root <repo>] [--files a.cpp b.cmake]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kLintTargetRelDirs, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

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
        {"name": "검사 이름 없는 NOLINT", "files": {"Source/Probe/A.cpp": "int a = 0; // NOLINT\n"}},
        {"name": "이유 없는 NOLINTNEXTLINE",
         "files": {"Source/Probe/B.cpp": "int f()\n{\n    // NOLINTNEXTLINE(bugprone-branch-clone)\n    return 0;\n}\n"}},
        {"name": "이유 없는 pragma", "files": {"Source/Probe/C.h": "#pragma once\nint x;\n#pragma clang diagnostic ignored \"-Wswitch-enum\"\n"}},
        {"name": "이유 없는 CMake 경고 끄기", "files": {"cmake/Modules/Compiler/Probe.cmake": "target_compile_options(x INTERFACE\n\t-Wno-padded\n)\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation: list[str] = []
        countCpp = 0
        countCmake = 0
        listCpp = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=_kCppSuffixes)
        for path, text in self.readFiles(listCpp, mustContain=None):
            violations, count = findCppViolations(normalizePath(str(path.relative_to(repositoryRoot))), text)
            listViolation += violations
            countCpp += count
        listCmake = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=_kListCmakeRoot, suffixes=(".cmake",))
        for path, text in self.readFiles(listCmake):
            violations, count = findCmakeViolations(normalizePath(str(path.relative_to(repositoryRoot))), text)
            listViolation += violations
            countCmake += count
        return GateResult(listViolation=listViolation, summary=f"C++ 억제 {countCpp} · CMake 경고 끄기 {countCmake}")


main = CheckSuppressionReasonsGate.run

if __name__ == "__main__":
    sys.exit(main())
