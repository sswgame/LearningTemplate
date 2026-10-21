#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`std::remove_if` 를 직접 쓰는 곳을 잡는다 — 지우기는 `VectorUtil::removeIf( list, predicate )` 다.

`list.erase( std::remove_if( ... ), list.end() )` 에서 `erase` 의 둘째 인자를 빠뜨리면 한 개만 지워지는데도 컴파일된다.
`std::remove_if` 는 표준 이름이라 형식(`[[nodiscard]]` 따위)으로 막을 수 없다 — 그래서 막지 못하는 틈에만 이 게이트를 둔다.
`VectorUtil::removeIf` 가 erase-remove 를 한 곳에서 맞게 적으므로, 바깥에서는 그 모양을 손으로 적을 일이 없다.

`sw::list::remove_if` 같은 멤버 호출은 `std::` 가 붙지 않아 보지 않는다. 주석 · 문자열 안의 언급도 보지 않는다.
예외는 `Scripts/lint/rules/CheckStdRemoveIf.toml` 의 `[exemption]` 한 곳이다(도우미 자신). 낡은 예외는 `LintGate` 기반이 실패시킨다.

  python Scripts/lint/gate/CheckStdRemoveIf.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import findFast  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

_kStdRemoveIfRe = re.compile(r"(?<![\w$])std::remove_if(?![\w$])")


def findStdRemoveIfUses(source: SourceFile) -> list[str]:
    """예외 밖에서 `std::remove_if` 를 쓰는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckStdRemoveIfGate
    relative = source.relative
    listCodeMatch: list[tuple[int, str]] = []
    if findFast(_kStdRemoveIfRe, source.text) is not None:
        listCodeMatch = [(lineIndex, match.group(0))
                         for lineIndex, line in enumerate(source.listMaskedLine, start=1)
                         for match in _kStdRemoveIfRe.finditer(line)]
    if relative in gate.mapExemption:
        gate.seeExemption(relative)
        if listCodeMatch:
            gate.useExemption(relative)
        return []
    return [f"{relative}:{lineIndex}: {name}  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, name in listCodeMatch]


class CheckStdRemoveIfGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "std::remove_if 를 직접 쓰지 않고 VectorUtil::removeIf 를 쓰는지 검사"
    buildComment = "Checking that code erases through VectorUtil::removeIf, not std::remove_if..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 std::remove_if 직접 사용"
    violationHeader = "std::remove_if 직접 사용"
    hint = (
        "  지우기는 Source/Core/Container/VectorUtil.h 의 VectorUtil::removeIf 입니다(지운 개수를 돌려줍니다):\n"
        "      #include \"Core/Container/VectorUtil.h\"      VectorUtil::removeIf( _listEntry, []( const Entry& e ) { return e._bDead; } );\n"
        "  list.erase( std::remove_if( ... ), list.end() ) 는 erase 의 둘째 인자를 빠뜨려도 컴파일되어 한 개만 지웁니다.\n"
        "  정말 std::remove_if 가 필요하면 Scripts/lint/rules/CheckStdRemoveIf.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "erase-remove 를 손으로 적는다",
            "expectedViolation": "Source/Probe/ProbeList.cpp:4: std::remove_if",
            "files": {
                "Source/Probe/ProbeList.cpp": (
                    "#include <algorithm>\n"
                    "void probe( std::vector<int>& list )\n"
                    "{\n"
                    "    list.erase( std::remove_if( list.begin(), list.end(), []( int v ) { return v == 0; } ), list.end() );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "테스트 폴더도 같다",
            "expectedViolation": "Test/ProbeTest/ProbeList.cpp:2: std::remove_if",
            "files": {
                "Test/ProbeTest/ProbeList.cpp": (
                    "void probe( std::vector<int>& list )\n"
                    "{ std::remove_if( list.begin(), list.end(), []( int v ) { return v == 0; } ); }\n"
                ),
            },
        },
        {
            "name": "std::remove_if 를 더는 쓰지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/Core/Container/VectorUtil.h' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Core/Container/VectorUtil.h": "// std::remove_if 는 주석 안이라 쓰임이 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findStdRemoveIfUses(source))


main = CheckStdRemoveIfGate.run


if __name__ == "__main__":
    sys.exit(main())
