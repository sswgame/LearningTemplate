#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""엔진 코드가 `std::thread` 를 직접 만드는 곳을 잡는다 — 스레드는 `Source/Core/Concurrency/thread.h` 의 `sw::thread` 다.

`sw::thread` 는 `std::thread` 와 같은 모양이고, 합류(`join`) 대기를 잠금 관찰자(교착 검출기)에 알린다. 그래서 "A 가 잠금을 쥔 채 B 를
join, B 는 그 잠금을 기다림" 같은 순환이 `sw::mutex` 끼리의 순환과 같은 대기 그래프에 잡힌다. 또 로더 락을 쥔 스레드(Windows `DllMain` ·
DLL 을 내릴 때 도는 정적 소멸자)에서 `join` 하면 영원히 멈추는데, `sw::thread::join` 은 그 경우 단언하고 스레드를 뗀다.
`std::thread` 를 직접 쓰면 이 둘이 모두 꺼진다.

`std::thread::id` · `std::thread::hardware_concurrency` 처럼 `::` 가 이어지는 쓰임은 스레드를 만드는 것이 아니므로 보지 않는다.
주석 · 문자열 안의 언급도 보지 않는다. 테스트(`Test/`)는 대상이 아니다 — 거기서는 짧게 쓰고 버리는 스레드가 많다.

예외는 `Scripts/lint/rules/CheckStdThread.toml` 의 `[exemption]` 한 곳이다. 예외 파일이 더는 `std::thread` 를 쓰지 않으면 그 줄은 낡은 예외로 실패한다(`LintGate` 기반이 본다).

  python Scripts/lint/gate/CheckStdThread.py [--root <repo>] [--files a.cpp b.h]
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

_kListScanRoot = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

_kStdThreadRe = re.compile(r"(?<![\w$])std::(?:j)?thread(?![\w$:])")


def findStdThreadUses(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """예외 밖에서 `std::thread` 를 쓰는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckStdThreadGate
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative.startswith("Source/") == False:
            continue
        bExempt = relative in gate.mapExemption
        listCodeMatch: list[tuple[int, str]] = []
        if _kStdThreadRe.search(text) is not None:
            listCodeMatch = [(lineIndex, match.group(0))
                             for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1)
                             for match in _kStdThreadRe.finditer(line)]
        if bExempt:
            gate.seeExemption(relative)
            if listCodeMatch:
                gate.useExemption(relative)
            continue
        listOriginalLine = text.splitlines()
        for lineIndex, name in listCodeMatch:
            listViolation.append(f"{relative}:{lineIndex}: {name}  | {listOriginalLine[lineIndex - 1].strip()}")
    return listViolation


class CheckStdThreadGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "엔진 코드가 std::thread 가 아니라 sw::thread(합류 대기 관찰 · 로더 락 안 join 단언)를 쓰는지 검사"
    buildComment = "Checking that engine code starts threads through sw::thread, not std::thread..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "std::thread 직접 사용"
    hint = (
        "  스레드는 Source/Core/Concurrency/thread.h 의 sw::thread 입니다(std::thread 와 같은 모양):\n"
        "      #include \"Core/Concurrency/thread.h\"      sw::thread worker( &Owner::run, this );\n"
        "  join 이 교착 검출기 대기 그래프에 보이고, 로더 락 안의 join 은 멈추는 대신 단언합니다.\n"
        "  정말 std::thread 가 필요하면 Scripts/lint/rules/CheckStdThread.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "멤버로 std::thread 를 든다",
            "files": {
                "Source/Probe/ProbeWorker.h": (
                    "#include <thread>\n"
                    "class ProbeWorker\n"
                    "{\n"
                    "    std::thread _thread;\n"
                    "};\n"
                ),
            },
        },
        {
            "name": "스레드 벡터와 대입(Source 폴더)",
            "files": {
                "Source/Probe/ProbePool.cpp": (
                    "#include <thread>\n"
                    "void probe( std::vector<std::thread>& list )\n"
                    "{\n"
                    "    list.push_back( std::thread( [] {} ) );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "std::thread 를 더는 쓰지 않는 예외 파일(낡은 예외)",
            "files": {
                "Source/Core/Diagnostics/CrashContext.cpp": "// std::thread 는 주석 안이라 쓰임이 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findStdThreadUses(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary=f"{' · '.join(_kListScanRoot)} 의 std::thread 직접 사용")


main = CheckStdThreadGate.run


if __name__ == "__main__":
    sys.exit(main())
