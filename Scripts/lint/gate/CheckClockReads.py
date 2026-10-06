#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""엔진 코드 · 시험 코드가 `std::chrono` 시계를 직접 읽는 곳을 잡는다 — 시계는 `Source/Core/Time/MonotonicClock.h` 하나다.

시계가 하나여야 프로파일러 · 로그 · 기한 · 시험이 같은 시각을 본다. 대신 쓸 것(같은 헤더):

    MonotonicClock::nowNanoseconds()                   지금 시각
    Stopwatch · getElapsedMilliseconds()      걸린 시간
    Deadline::afterMilliseconds( ms ) · isExpired()   기다림 루프의 기한

`steady_clock` · `high_resolution_clock` · `system_clock` 이라는 이름 자체를 막는다 — `::now()` 만 보면
`using Clock = std::chrono::steady_clock; Clock::now()` 나 `using namespace std::chrono;` 뒤의 읽기를 놓친다.
기간 값(`std::chrono::milliseconds( n )` · `sleep_for`)은 시계 읽기가 아니므로 보지 않는다. 파일 시계
(`std::filesystem::file_time_type::clock`)도 보지 않는다 — 파일 시각과 견줄 때만 쓴다. 주석 · 문자열 안의 언급은 보지 않는다.

예외는 `_kMapExemptFileToReason` 표 한 곳이다. 예외 파일이 더는 std 시계를 읽지 않으면 그 줄은 낡은 예외로 실패한다.

  python Scripts/lint/gate/CheckClockReads.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · gate

from common import blankCommentsAndLiterals, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kListScanRoot = ("Source", "Test", "Tools/ReflectionParser", "Tools/OnlineLoadBot")
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

#: std 시계를 읽어도 되는 파일 → 이유.
_kMapExemptFileToReason = {
    "Source/Core/Time/WallClock.cpp": "UTC 벽시계의 유일한 자리 — 서버의 기간 · 만료 · 기록 시각은 기준점(epoch)이 있어야 한다. 경과 시간은 MonotonicClock",
}

_kClockNameRe = re.compile(r"(?<![\w$])(steady_clock|high_resolution_clock|system_clock)(?![\w$])")


def findClockReads(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """예외 밖에서 std 시계 이름을 쓰는 줄과, 더는 std 시계를 읽지 않는 예외 파일을 위반 문자열로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        bExempt = relative in _kMapExemptFileToReason
        listCodeMatch: list[tuple[int, str]] = []
        if _kClockNameRe.search(text) is not None:
            listCodeMatch = [(lineIndex, match.group(1))
                             for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1)
                             for match in _kClockNameRe.finditer(line)]
        if bExempt:
            if not listCodeMatch:
                listViolation.append(f"{relative}: 예외 표에 있지만 std 시계를 읽지 않는다 — 낡은 예외 줄을 지웁니다")
            continue
        listOriginalLine = text.splitlines()
        for lineIndex, clockName in listCodeMatch:
            listViolation.append(f"{relative}:{lineIndex}: std::chrono::{clockName}  | {listOriginalLine[lineIndex - 1].strip()}")
    return listViolation


class CheckClockReadsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "엔진 · 시험 코드가 std::chrono 시계가 아니라 MonotonicClock · Stopwatch · Deadline 을 읽는지 검사"
    buildComment = "Checking that code reads time through MonotonicClock, not std::chrono clocks..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "std::chrono 시계 직접 읽기"
    hint = (
        "  시계는 Source/Core/Time/MonotonicClock.h 하나입니다:\n"
        "      지금 시각   MonotonicClock::nowNanoseconds()\n"
        "      걸린 시간   Stopwatch stopwatch; ... stopwatch.getElapsedMilliseconds()\n"
        "      기다림 기한 Deadline::afterMilliseconds( ms ) · isExpired()\n"
        "  정말 std 시계가 필요하면 Scripts/lint/gate/CheckClockReads.py 의 _kMapExemptFileToReason 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "기다림 루프가 steady_clock::now 를 읽는다(Test 폴더)",
            "files": {
                "Test/Probe/ProbeWait.cpp": (
                    "#include <chrono>\n"
                    "void probe( bool& bDone )\n"
                    "{\n"
                    "    const auto start = std::chrono::steady_clock::now();\n"
                    "    while ( bDone == false && std::chrono::steady_clock::now() - start < std::chrono::seconds( 10 ) ) {}\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "별칭 뒤에 숨은 high_resolution_clock(Source 폴더)",
            "files": {
                "Source/Probe/ProbeTimer.cpp": (
                    "#include <chrono>\n"
                    "using Clock = std::chrono::high_resolution_clock;\n"
                    "long long probe() { return Clock::now().time_since_epoch().count(); }\n"
                ),
            },
        },
        {
            "name": "using namespace 뒤의 system_clock(ReflectionParser)",
            "files": {
                "Tools/ReflectionParser/ProbeStamp.cpp": (
                    "#include <chrono>\n"
                    "using namespace std::chrono;\n"
                    "long long probe() { return system_clock::now().time_since_epoch().count(); }\n"
                ),
            },
        },
        {
            "name": "std 시계를 더는 읽지 않는 예외 파일(낡은 예외)",
            "files": {
                "Source/Core/Time/WallClock.cpp": "// steady_clock 은 주석 안이라 읽기가 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Test · Tools/ReflectionParser 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findClockReads(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Source · Test · Tools/ReflectionParser · Tools/OnlineLoadBot 의 std::chrono 시계 읽기")


main = CheckClockReadsGate.run


if __name__ == "__main__":
    sys.exit(main())
