#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""엔진 코드가 `std::abort` 를 직접 부르는 곳을 잡는다 — 멈출 때는 `SW_FATAL`(Logger.h) · `SW_LOW_LEVEL_FATAL`(Macros.h)이다.

`SW_FATAL` 은 이유 한 줄을 stderr 에 먼저 동기로 쓰고 로그로 보낸 뒤 비동기 싱크를 비우고(`Logger::flushGlobalForCrash`) 멈춘다
(`sw::internal::reportFatal`, `[[noreturn]]`). 직접 `std::abort` 를 부르면 로그만 쓰고 비우지 않거나 이유 없이 멈춰, 크래시 직전 줄을 잃는다.

형식으로 막지 못하는 틈이라 게이트로 둔다: `std::abort` 는 표준 헤더가 선언하고 서드파티 헤더(`JSON_THROW`)도 쓰므로 `#pragma poison` 으로 지울 수 없다.
멤버 함수 `x.abort( … )` · `session->abort( … )` 는 프로세스를 멈추는 것이 아니므로 보지 않는다. 주석 · 문자열 안의 언급도 보지 않는다.
테스트(`Test/`)는 대상이 아니다.

예외는 `Scripts/lint/rules/CheckDirectAbort.toml` 의 `[exemption]` 한 곳이다. 예외 파일이 더는 abort 를 부르지 않으면 낡은 예외로 실패한다.

  python Scripts/lint/gate/CheckDirectAbort.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import findFast  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

# `std::abort(` · `::abort(` · 인자 없는 맨 `abort()` — 멤버 호출(`.abort(` · `->abort(`)과 인자 있는 같은 이름 함수는 아니다.
_kAbortRe = re.compile(r"(?<![\w$.>])(?:std)?::abort\s*\(|(?<![\w$.>:])abort\s*\(\s*\)")


def findDirectAborts(source: SourceFile) -> list[str]:
    """예외 밖에서 abort 를 직접 부르는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckDirectAbortGate
    relative = source.relative
    if relative.startswith("Source/") == False:
        return []
    bExempt = relative in gate.mapExemption
    listCodeMatch: list[int] = []
    if findFast(_kAbortRe, source.text) is not None:
        listCodeMatch = [lineIndex for lineIndex, line in enumerate(source.listMaskedLine, start=1) if _kAbortRe.search(line) is not None]
    if bExempt:
        gate.seeExemption(relative)
        if listCodeMatch:
            gate.useExemption(relative)
        return []
    return [f"{relative}:{lineIndex}: abort  | {source.listLine[lineIndex - 1].strip()}" for lineIndex in listCodeMatch]


class CheckDirectAbortGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "엔진 코드가 std::abort 가 아니라 SW_FATAL(이유 한 줄 · 싱크 비우기)로 멈추는지 검사"
    buildComment = "Checking that engine code stops through SW_FATAL, not std::abort..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 std::abort 직접 호출"
    violationHeader = "std::abort 직접 호출"
    hint = (
        "  멈출 때는 이유 한 줄과 함께 SW_FATAL 을 씁니다(서식은 SW_LOG_* 와 같은 %#):\n"
        "      SW_FATAL( \"'%#' has no codec\", name );\n"
        "  로그보다 아래 티어(컨테이너 · 메모리 · 수학)는 SW_LOW_LEVEL_FATAL(printf 서식)입니다.\n"
        "  정말 직접 abort 가 필요하면 Scripts/lint/rules/CheckDirectAbort.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "로그 뒤 std::abort",
            "expectedViolation": "Source/Probe/ProbeStop.cpp:4: abort",
            "files": {
                "Source/Probe/ProbeStop.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    SW_LOG_ERROR( \"bad\" );\n"
                    "    std::abort();\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "전역 이름 ::abort",
            "expectedViolation": "Source/Probe/ProbeGlobal.cpp:1: abort",
            "files": {"Source/Probe/ProbeGlobal.cpp": "void probe() { ::abort(); }\n"},
        },
        {
            "name": "abort 를 더는 부르지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/Core/Diagnostics/CrashContext.cpp' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Core/Diagnostics/CrashContext.cpp": "// std::abort() 는 주석 안이라 호출이 아니다\nvoid probe( Session* p ) { p->abort( 1 ); }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findDirectAborts(source))


main = CheckDirectAbortGate.run


if __name__ == "__main__":
    sys.exit(main())
