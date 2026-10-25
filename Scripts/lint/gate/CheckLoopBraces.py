#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckLoopBraces.py

반복문(`for` · 범위 `for` · `while`)은 본문이 한 문장이어도 중괄호를 쓴다(docs/agents/CppStyle.md '분기문 규칙').

`if` 는 한 줄 본문의 중괄호를 벗기지만(`FormatBranchBraces`), 반복문은 늘 중괄호다 — 본문에 한 줄을 더할 때 반복 밖으로 새는 줄이 생기지 않게.
`do { … } while ( … );` 의 꼬리 `while` 은 반복문 머리가 아니다. 본문이 `;` 하나(빈 본문)이면 `{}` 로 쓴다.

판정과 고침은 `Scripts/lint/LoopBraceRules.py` 한 자리다(`inspectSource` — 위반과 고친 글이 한 함수의 두 출력) — 고치는 것은 같은 함수를 부르는 픽서 `Scripts/lint/fixer/FormatBranchBraces.py` 의 반복문 패스.
주석 · 전처리기 · 매크로 줄바꿈이 머리와 본문 사이나 본문 안에 끼면 픽서는 건드리지 않고(사람이 고친다) 이 게이트만 알린다.

  python Scripts/lint/gate/CheckLoopBraces.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.LoopBraceRules import inspectSource  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")


class CheckLoopBracesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "본문에 중괄호가 없는 for · while 반복문 검사(한 문장 본문도 중괄호)"
    buildComment = "Checking that every for / while loop body has braces..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in kLintTargetRelDirs)
    preCommitFileArgument = "--files"
    sourceScanRoots = kLintTargetRelDirs
    sourceSuffixes = _kSuffixes
    violationHeader = "중괄호 없는 반복문"
    hint = ("  for ( int32 index = 0; index < count; ++index )\n  {\n      doThing( index );\n  }\n"
            "  고치기: py -3 Scripts/lint/fixer/FormatBranchBraces.py --files <파일>(주석 · 전처리기가 낀 본문은 손으로)")
    selfTestCases = [
        {"name": "한 문장 본문의 for", "expectedViolation": "Source/Probe/A.cpp:3: for 본문에 중괄호가 없습니다", "files": {"Source/Probe/A.cpp": "void f()\n{\n    for ( int i = 0; i < 3; ++i )\n        g( i );\n}\n"}},
        {"name": "같은 줄 본문의 범위 for", "expectedViolation": "Source/Probe/B.cpp:3: for 본문에 중괄호가 없습니다", "files": {"Source/Probe/B.cpp": "void f()\n{\n    for ( int value : list ) g( value );\n}\n"}},
        {"name": "빈 본문의 while", "expectedViolation": "Test/Probe/C.cpp:3: while 본문에 중괄호가 없습니다", "files": {"Test/Probe/C.cpp": "void f()\n{\n    while ( poll() );\n}\n"}},
        # 조각은 바이트 그대로 쓰인다(CheckLintsAreAlive) — 줄 끝이 CRLF 인 파일도 같은 줄 번호로 잡는다. 위반 줄 끝에 `\r` 이 남지 않는다.
        {"name": "CRLF 파일의 for", "expectedViolation": "Source/Probe/D.cpp:3: for 본문에 중괄호가 없습니다 | for ( int i = 0; i < 3; ++i )\n",
         "files": {"Source/Probe/D.cpp": "void f()\r\n{\r\n    for ( int i = 0; i < 3; ++i )\r\n        g( i );\r\n}\r\n"}},
        {"name": "BOM 이 있는 파일의 같은 줄 본문", "expectedViolation": "Source/Probe/E.cpp:3: for 본문에 중괄호가 없습니다 | for ( int value : list ) g( value );\n",
         "files": {"Source/Probe/E.cpp": "﻿void f()\n{\n    for ( int value : list ) g( value );\n}\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return inspectSource(source)


main = CheckLoopBracesGate.run

if __name__ == "__main__":
    sys.exit(main())
