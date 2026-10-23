#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""로그 · assert 문자열에 한글이 있는 곳을 잡는다 — 로그와 assert 문자열은 영어다(AGENTS.md).

로그는 사람이 한글을 읽는 화면이 아니라 검색 · 비교 · 외부 도구로 가는 줄이다. 한글이 섞이면 터미널 코드 페이지에 따라 깨지고, 같은 사건을 영어로
적은 줄과 한 번에 찾지 못한다. 대상은 `SW_LOG_ERROR` · `SW_LOG_WARNING` · `SW_LOG_INFO` · `SW_LOG_TRACE` · `SW_LOG_ASSERT` · `SW_ASSERT`(및 `SW_ASSERT_*`)의 인자다.
호출은 `;` 까지 한 덩어리로 본다(여러 줄 호출 포함). 주석 안의 한글은 이 규칙 밖이다.

기존 한글 줄은 폴더 단위로 예외(`Scripts/lint/rules/CheckLogLanguage.toml` 의 `[exemption]`, fnmatch 패턴)에 두고, 폴더를 영어로 고치면 그 줄은 낡은 예외로
실패하므로 지워야 한다([KoreanLogStrings](../../../docs/plans/KoreanLogStrings.md)). 새 폴더에는 한글 로그를 들일 수 없다.

  python Scripts/lint/gate/CheckLogLanguage.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = ("Source", "Test")
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")

#: 로그 · assert 호출의 시작. 인자는 `;` 까지다.
_kCallRe = re.compile(r"\bSW_(?:LOG_(?:ERROR|WARNING|INFO|TRACE|ASSERT)|ASSERT\w*)\s*\(")
_kHangulRe = re.compile(r"[가-힣]")


def findHangulCalls(source: SourceFile) -> list[tuple[int, str]]:
    """한글이 든 로그 · assert 호출을 (줄 번호, 호출 이름) 으로 돌려줍니다 — 줄 번호는 호출을 연 줄이다."""
    text = source.noComment
    listFound: list[tuple[int, str]] = []
    for match in _kCallRe.finditer(text):
        end = text.find(";", match.end())
        span = text[match.end():end if end != -1 else len(text)]
        if _kHangulRe.search(span) is None:
            continue
        listFound.append((text.count("\n", 0, match.start()) + 1, match.group(0).rstrip("( \t")))
    return listFound


def findLogLanguageViolations(source: SourceFile) -> list[str]:
    """예외 폴더 밖의 한글 로그 줄을 위반 문자열로 돌려줍니다. 예외에 맞는 파일은 보면 `see`, 한글이 있으면 `use` 로 적습니다."""
    gate = CheckLogLanguageGate
    listFound = findHangulCalls(source) if _kHangulRe.search(source.noComment) is not None else []
    key = gate.findExemptionKey(source.relative)
    if key is not None:
        gate.seeExemption(key)
        if listFound:
            gate.useExemption(key)
        return []
    return [f"{source.relative}:{lineNumber}: {name} 에 한글  | {source.listLine[lineNumber - 1].strip()}" for lineNumber, name in listFound]


class CheckLogLanguageGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "로그 · assert 문자열이 영어인지 검사(한글이 든 SW_LOG_* · SW_ASSERT 호출)"
    buildComment = "Checking that log and assert strings contain no Hangul..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 한글 로그 · assert"
    violationHeader = "한글이 든 로그 · assert 문자열"
    hint = (
        "  로그와 assert 문자열은 영어로 씁니다: SW_LOG_WARNING( \"Failed to load '{}'\", path );\n"
        "  이미 있는 폴더의 한글 줄은 Scripts/lint/rules/CheckLogLanguage.toml 의 [exemption] 폴더 줄이 받고 있습니다(docs/plans/KoreanLogStrings.md)."
    )
    selfTestCases = [
        {
            "name": "한글 로그",
            "expectedViolation": "Source/Probe/A.cpp:3: SW_LOG_WARNING 에 한글",
            "files": {"Source/Probe/A.cpp": "void f()\n{\n    SW_LOG_WARNING( \"불러오지 못했습니다: {}\", path );\n}\n"},
        },
        {
            "name": "여러 줄 호출의 한글 assert",
            "expectedViolation": "Test/Probe/B.cpp:3: SW_ASSERT 에 한글",
            "files": {"Test/Probe/B.cpp": "void f()\n{\n    SW_ASSERT( pValue != nullptr\n               && \"값이 필요합니다\" );\n}\n"},
        },
        {
            "name": "더는 한글이 없는 예외 폴더(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/Core/CommandLine/*'",
            "files": {"Source/Core/CommandLine/D.cpp": "void f()\n{\n    SW_LOG_ERROR( \"Load failed\" );\n}\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Test 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findLogLanguageViolations(source))


main = CheckLogLanguageGate.run

if __name__ == "__main__":
    sys.exit(main())
