#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""엔진 코드가 `std::mutex` · `std::shared_mutex` · `std::recursive_mutex` 를 직접 쓰는 곳을 잡는다 — 잠금은 `Source/Core/Concurrency/mutex.h` 의
`sw::mutex` · `sw::shared_mutex` 다.

감싼 잠금은 세 가지를 켠다. clang `-Wthread-safety` 가 보는 능력 주석(`SW_CAPABILITY`, `SW_GUARDED_BY` 계약), Debug 의 쥔 잠금 수(로거가 0 을
단언 — 잎 잠금 규칙, 결정 5-3), 교착 검출 빌드의 대기 그래프. `std::` 잠금을 직접 쓰면 셋이 모두 꺼진다. 형식(타입)만으로는 `std::` 를 쓰는 것을
막을 수 없어 이 게이트가 그 틈을 막는다.

가드 `std::scoped_lock` · `std::lock_guard` · `std::unique_lock` · `std::shared_lock` 도 잡는다 — MSVC STL 에 능력 주석이 없어 그 범위를 분석이 잠긴 범위로
보지 못하므로(`SW_GUARDED_BY` 멤버가 경고가 된다) 가드는 `Core/Concurrency/ScopedLock.h` 의 `ScopedLock` · `ScopedSharedLock` · `UniqueLock` ·
`ScopedLockPair` 다. 그 넷이 지역 가드 · 조건 변수 대기 · 늦게 잡기 · 둘 잡기를 모두 덮고, 남는 틈(잠금이 배열 · 멤버 · 반환값으로 범위를 건너가는 곳)만
예외 표에 이유와 함께 둔다. 주석 · 문자열 안의 언급은 보지 않는다.
테스트(`Test/`)는 대상이 아니다 — 하네스 · 짧은 테스트 스레드는 엔진 잠금 규칙과 상관없다.

예외는 `Scripts/lint/rules/CheckStdMutex.toml` 의 `[exemption]` 한 곳이다. 예외 파일이 더는 쓰지 않으면 그 줄은 낡은 예외로 실패한다(`LintGate` 기반이 본다).

  python Scripts/lint/gate/CheckStdMutex.py [--root <repo>] [--files a.cpp b.h]
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

_kStdMutexRe = re.compile(r"(?<![\w$])std::(?:(?:shared_|recursive_|timed_|shared_timed_|recursive_timed_)?mutex|scoped_lock|lock_guard|unique_lock|shared_lock)(?![\w$])")


def findStdMutexUses(source: SourceFile) -> list[str]:
    """예외 밖에서 `std::` 잠금 타입을 쓰는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckStdMutexGate
    relative = source.relative
    if relative.startswith("Source/") == False:
        return []
    bExempt = relative in gate.mapExemption
    listCodeMatch: list[tuple[int, str]] = []
    if findFast(_kStdMutexRe, source.text) is not None:
        listCodeMatch = [(lineIndex, match.group(0))
                         for lineIndex, line in enumerate(source.listMaskedLine, start=1)
                         for match in _kStdMutexRe.finditer(line)]
    if bExempt:
        gate.seeExemption(relative)
        if listCodeMatch:
            gate.useExemption(relative)
        return []
    return [f"{relative}:{lineIndex}: {name}  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, name in listCodeMatch]


class CheckStdMutexGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "엔진 코드가 std 잠금 · 가드가 아니라 sw::mutex · sw::shared_mutex 와 ScopedLock 계열(잠금 계약 분석 · 쥔 잠금 수 · 교착 검출)을 쓰는지 검사"
    buildComment = "Checking that engine code locks through sw::mutex / sw::shared_mutex, not std::mutex..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 std:: 잠금 타입 직접 사용"
    violationHeader = "std:: 잠금 타입 직접 사용"
    hint = (
        "  잠금은 Source/Core/Concurrency/mutex.h 의 sw::mutex · sw::shared_mutex 입니다(std 와 같은 모양):\n"
        "      #include \"Core/Concurrency/mutex.h\"      mutable shared_mutex _mutex;   ScopedLock<mutex> lock{ _mutex };\n"
        "  가드는 ScopedLock · ScopedSharedLock · UniqueLock(조건 변수 · 늦게 잡기) · ScopedLockPair(둘) 입니다(Core/Concurrency/ScopedLock.h).\n"
        "  -Wthread-safety 계약(SW_GUARDED_BY), Debug 의 '잠금을 쥔 채 로그' 단언, 교착 검출이 켜집니다.\n"
        "  정말 std:: 잠금이 필요하면 Scripts/lint/rules/CheckStdMutex.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "멤버로 std::shared_mutex 를 든다",
            "expectedViolation": "Source/Probe/ProbeCache.h:4: std::shared_mutex",
            "files": {
                "Source/Probe/ProbeCache.h": (
                    "#include <shared_mutex>\n"
                    "class ProbeCache\n"
                    "{\n"
                    "    mutable std::shared_mutex _mutex;\n"
                    "};\n"
                ),
            },
        },
        {
            "name": "가드의 템플릿 인자로 std::mutex",
            "expectedViolation": "ProbeTable.cpp:3: std::mutex",
            "files": {
                "Source/Probe/ProbeTable.cpp": (
                    "void probe( Table& table )\n"
                    "{\n"
                    "    std::scoped_lock<std::mutex> lock{ table._mutex };\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "std::unique_lock 가드",
            "expectedViolation": "ProbeQueue.cpp:3: std::unique_lock",
            "files": {
                "Source/Probe/ProbeQueue.cpp": (
                    "void probe( Queue& queue )\n"
                    "{\n"
                    "    std::unique_lock<mutex> lock{ queue._mutex };\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "std:: 잠금을 더는 쓰지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/Core/Concurrency/Futex.cpp' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Core/Concurrency/Futex.cpp": "// std::mutex 는 주석 안이라 쓰임이 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findStdMutexUses(source))


main = CheckStdMutexGate.run


if __name__ == "__main__":
    sys.exit(main())
