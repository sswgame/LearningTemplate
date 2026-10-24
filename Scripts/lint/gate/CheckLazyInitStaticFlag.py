#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""함수 안 지연 초기화를 맨 `static bool` 플래그로 하는 곳을 잡는다 — 함수 안 정적 변수(`static T value = make();`) 나 `std::call_once` 다.

`static bool s_bTried{ false }; if ( s_bTried == false ) { s_bTried = true; ... }` 는 두 스레드가 동시에 첫 호출을 하면 둘 다 초기화를
돈다(셰이더 리플렉션 로더에서 실제로 났다). 함수 안 정적 변수는 C++11 부터 초기화가 한 번만 일어나도록 보장되므로 이 모양을 손으로
적을 일이 없다. `std::atomic<bool>` · `const` · `constexpr` · `thread_local` 은 보지 않는다.
`static bool` 은 표준 낱말이라 형식으로 막을 수 없다 — 그래서 막지 못하는 틈에만 이 게이트를 둔다.

  python Scripts/lint/gate/CheckLazyInitStaticFlag.py [--root <repo>] [--files a.cpp b.h] [--list]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kScanRoots = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

# `static bool name`(앞에 const · constexpr · thread_local 이 없고 atomic 이 아닌 것) — 줄 머리 기준.
_kStaticBoolRe = re.compile(r"^\s*static\s+bool\s+\w+\s*(?:[={;])")


class CheckLazyInitStaticFlagGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "함수 안 지연 초기화가 맨 static bool 플래그가 아니라 함수 안 정적 변수 · call_once 인지 검사(기준선 래칫)"
    buildComment = "Checking that lazy initialization avoids bare static bool flags..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoots)
    sourceScanRoots = _kScanRoots
    sourceSuffixes = _kSuffixes
    violationHeader = "맨 static bool 플래그"
    countedNoun = "static bool flags"
    baselineExceededHint = "함수 안 정적 변수 또는 std::call_once 로 바꾼다"
    hint = ("  static const Loader s_loader = makeLoader();      // C++11 이 초기화를 한 번만 보장한다\n"
            "  static std::once_flag s_once; std::call_once( s_once, init );\n"
            "  `static bool s_bTried` + `if` 는 두 스레드가 동시에 첫 호출을 하면 둘 다 초기화한다. 전체를 보려면 --list.")
    selfTestCases = [
        {"name": "함수 안 맨 플래그", "expectedViolation": "1  Source/Probe/A.cpp",
         "files": {"Source/Probe/A.cpp": "void load()\n{\n    static bool s_bTried{ false };\n    if ( s_bTried == false )\n    {\n        s_bTried = true;\n    }\n}\n"},
         "args": ["--baseline", "0"]},
        {"name": "=로 초기화", "expectedViolation": "1  Source/Probe/B.cpp",
         "files": {"Source/Probe/B.cpp": "void load()\n{\n    static bool s_bDone = false;\n}\n"}, "args": ["--baseline", "0"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        listFound: list[CountedFinding] = []
        for lineIndex, line in enumerate(source.listMaskedLine, start=1):
            if _kStaticBoolRe.match(line) is not None:
                listFound.append(CountedFinding("total", source.relative, lineIndex, lineIndex,
                                                "맨 static bool 플래그 — 함수 안 정적 변수나 std::call_once 로", source.listLine[lineIndex - 1].strip()))
        return listFound


main = CheckLazyInitStaticFlagGate.run

if __name__ == "__main__":
    sys.exit(main())
