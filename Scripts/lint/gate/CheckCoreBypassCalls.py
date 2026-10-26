#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""엔진 코드가 Core 가 한 곳에 모아 둔 표준 · OS 호출을 직접 부르는 곳을 잡는다 — 셋 다 표준 이름이라 형식으로 막을 수 없는 틈이다.

    parse   글 → 숫자: `from_chars` · `strto*` · `ato*` · `sto*`
            → `StringUtil::parseInt` · `parseInt64` · `parseUint64` · `parseFloat` · `parseDouble` · `tryParseBool`
              (`atoi` 는 실패를 0 으로, `stoul` 은 예외로 알리고, 직접 읽으면 공백 · `+` · 끝 검사가 호출부마다 달라진다)
    random  `std::random_device` → `SecureRandom::fill` · `nextUint64`(Windows BCryptGenRandom · 리눅스 getrandom, 실패하면 멈춤)
    stderr  `stderr` · `std::cerr` 에 직접 쓰기 → `SW_LOW_LEVEL_ERROR` · `SW_LOW_LEVEL_WARNING`
              (Error 는 stderr 에 동기로 먼저 쓰고 로거가 있으면 로그에도 남긴다 — stderr 로 가는 길은 `Core/Common/Macros.cpp` 하나)

주석 · 문자열 안의 언급은 보지 않는다. 테스트(`Test/`)와 도구는 대상이 아니다.
예외는 `Scripts/lint/rules/CheckCoreBypassCalls.toml` 의 `[exemption]` 이고 키는 `종류:경로`(`stderr:Source/Core/Common/Macros.cpp`)다.
예외 파일이 더는 그 호출을 쓰지 않으면 그 줄은 낡은 예외로 실패한다(`LintGate` 기반이 본다).

  python Scripts/lint/gate/CheckCoreBypassCalls.py [--root <repo>] [--files a.cpp b.h]
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

# 종류 → 정규식. 이름 앞이 멤버 접근(`.` · `->`)이면 남의 함수라 보지 않는다.
_kMapKindRe: dict[str, re.Pattern[str]] = {
    "parse": re.compile(r"(?<![\w$.>])(?:(?:std)?::)?(from_chars|strto(?:d|f|ld|l|ll|ul|ull|imax|umax)|ato(?:i|l|ll|f)|sto(?:i|l|ll|ul|ull|f|d|ld))\s*\("),
    "random": re.compile(r"(?<![\w$])(random_device)(?![\w$])"),
    "stderr": re.compile(r"(?<![\w$])(stderr|std::cerr)(?![\w$])"),
}


def findBypassCalls(source: SourceFile) -> list[str]:
    """예외 밖에서 Core 를 우회하는 호출 줄을 위반 문자열로 돌려줍니다. 예외 키는 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckCoreBypassCallsGate
    relative = source.relative
    if relative.startswith("Source/") == False:
        return []
    listViolation: list[str] = []
    for kind, pattern in _kMapKindRe.items():
        key = f"{kind}:{relative}"
        bExempt = key in gate.mapExemption
        listMatch: list[tuple[int, str]] = []
        if findFast(pattern, source.text) is not None:
            listMatch = [(lineIndex, match.group(1))
                         for lineIndex, line in enumerate(source.listMaskedLine, start=1)
                         for match in pattern.finditer(line)]
        if bExempt:
            gate.seeExemption(key)
            if listMatch:
                gate.useExemption(key)
            continue
        listViolation += [f"{relative}:{lineIndex}: {kind} {name}  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, name in listMatch]
    return listViolation


class CheckCoreBypassCallsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "엔진 코드가 글 → 숫자 · OS 난수 · stderr 를 Core 창구(StringUtil::parse* · SecureRandom · SW_LOW_LEVEL_*)로 거치는지 검사"
    buildComment = "Checking that engine code reaches number parsing, OS randomness and stderr through Core..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 Core 우회 호출(글 → 숫자 · random_device · stderr)"
    violationHeader = "Core 우회 호출"
    hint = (
        "  Core 의 창구를 씁니다:\n"
        "      parse   StringUtil::parseInt · parseInt64 · parseUint64 · parseFloat · parseDouble (Core/Container/StringUtil.h)\n"
        "      random  SecureRandom::fill · nextUint64 (Core/Math/SecureRandom.h) — 되풀이할 수열은 DeterministicRandom32 · 64\n"
        "      stderr  SW_LOW_LEVEL_ERROR · SW_LOW_LEVEL_WARNING (Core/Common/Macros.h, printf 서식)\n"
        "  정말 직접 불러야 하면 Scripts/lint/rules/CheckCoreBypassCalls.toml 의 [exemption] 에 `종류:경로` 키와 이유를 적습니다."
    )
    selfTestCases = [
        {
            "name": "엔진 코드가 atoi 로 포트를 읽는다",
            "expectedViolation": "Source/Probe/ProbePort.cpp:2: parse atoi",
            "files": {
                "Source/Probe/ProbePort.cpp": (
                    "#include <cstdlib>\n"
                    "int probe( const char* p ) { return std::atoi( p ); }\n"
                ),
            },
        },
        {
            "name": "from_chars 를 직접 부른다(결과 형식 이름은 호출이 아니다)",
            "expectedViolation": "Source/Probe/ProbeHash.cpp:3: parse from_chars",
            "files": {
                "Source/Probe/ProbeHash.cpp": (
                    "#include <charconv>\n"
                    "std::from_chars_result probe( const char* p, unsigned& v )\n"
                    "{ return std::from_chars( p, p + 4, v ); }\n"
                ),
            },
        },
        {
            "name": "random_device 로 씨앗을 만든다",
            "expectedViolation": "Source/Probe/ProbeSeed.cpp:2: random random_device",
            "files": {
                "Source/Probe/ProbeSeed.cpp": (
                    "#include <random>\n"
                    "unsigned probe() { std::random_device device; return device(); }\n"
                ),
            },
        },
        {
            "name": "stderr 에 직접 쓴다(주석 안의 stderr 는 보지 않는다)",
            "expectedViolation": "Source/Probe/ProbeReport.cpp:3: stderr stderr",
            "files": {
                "Source/Probe/ProbeReport.cpp": (
                    "#include <cstdio>\n"
                    "// stderr 로 알린다\n"
                    "void probe() { std::fprintf( stderr, \"failed\\n\" ); }\n"
                ),
            },
        },
        {
            "name": "stderr 를 더는 쓰지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'stderr:Source/Core/Common/Macros.cpp' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Core/Common/Macros.cpp": "// stderr 는 주석 안이라 쓰임이 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findBypassCalls(source))


main = CheckCoreBypassCallsGate.run


if __name__ == "__main__":
    sys.exit(main())
