#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""로그 인자로 넘기는 `.data()` 를 잡는다 — 뷰의 끝을 넘어 읽는다.

`formatstring` 은 인자가 `string_view` 면 **길이로** 쓰고(`write()` 가 `str.length()` 를 본다),
`const utf8*` 면 `strlen` 으로 읽는다. 그러니 `string_view` 를 `.data()` 로 풀어서 넘기면
**뷰가 끝나는 곳을 지나** 다음 널까지 읽는다. 부분 뷰(`substr`)일 때 실제로 넘어간다.

고치는 법은 `.data()` 를 **지우는 것**뿐이다 — 포매터가 뷰를 그대로 받는다.

  SW_LOG_ERROR( "failed: %#", path.data() );   // 뷰 끝을 넘어 읽는다
  SW_LOG_ERROR( "failed: %#", path );          // 길이만큼만 읽는다

2026-09-20 에 세어 보니 열네 자리가 그러고 있었다(Editor 4 · Engine 10).

`.c_str()` 은 잡지 않는다 — `string` 은 언제나 널로 끝나므로 그쪽은 안전하다.

  python Scripts/lint/gate/CheckLogViewArgument.py [--root <repo>] [--files a.cpp b.cpp]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectSourceFiles, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

# 한 줄 안에 로그 호출과 `<식별자>.data()` 가 같이 있는 모양만 본다.
# 여러 줄로 쪼갠 호출은 놓친다 — 그 대신 오탐이 없다(이 저장소의 로그는 거의 한 줄이다).
_kLogCallRe = re.compile(r"\bSW_LOG_[A-Z_]+\s*\(")
_kViewDataRe = re.compile(r"\b[A-Za-z_]\w*\s*\.\s*data\s*\(\s*\)")

_kListScanRoot = ("Source", "Tools")


def findLogViewArguments(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """로그 호출 줄에 있는 `.data()` 를 모아 위반 문자열로 돌려줍니다."""
    if listTargetFile:
        listPath = [repositoryRoot / f for f in listTargetFile]
    else:
        listPath = collectSourceFiles([repositoryRoot / r for r in _kListScanRoot], {".cpp", ".h"})

    violations: list[str] = []
    for path in listPath:
        if not path.exists() or path.is_dir():
            continue
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if not any(relative.startswith(r) for r in _kListScanRoot):
            continue

        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue

        for lineIndex, line in enumerate(text.splitlines(), start=1):
            if not _kLogCallRe.search(line):
                continue
            if _kViewDataRe.search(line):
                violations.append(f"[Log View Argument] {relative}:{lineIndex}: {line.strip()}")
    return violations


class CheckLogViewArgumentGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "로그 인자의 .data() 검사 (뷰 끝을 넘어 읽는다)"
    buildComment = "Checking log arguments for string_view::data()..."
    timeoutSeconds = 20
    preCommitPattern = ("Source/*", "Tools/*")
    preCommitFileArgument = "--files"
    violationHeader = "로그 인자로 넘긴 .data()"
    hint = (
        "  포매터는 string_view 를 길이로 쓰고 const utf8* 는 strlen 으로 읽습니다.\n"
        "  뷰를 .data() 로 풀면 뷰가 끝나는 곳을 지나 읽습니다 — .data() 를 지우십시오:\n"
        "      SW_LOG_ERROR( \"failed: %#\", path );\n"
        "  string 의 .c_str() 은 안전하므로 이 검사에 걸리지 않습니다."
    )
    selfTestCases = [
        {
            "name": "로그 인자에 .data()",
            "files": {
                "Source/Probe/ProbeLog.cpp": (
                    "void probe( string_view path )\n"
                    "{\n"
                    "    SW_LOG_ERROR( \"failed: %#\", path.data() );\n"
                    "}\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 특정 파일 (생략 시 Source · Tools 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findLogViewArguments(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Source · Tools 의 로그 인자 .data()")


main = CheckLogViewArgumentGate.run


if __name__ == "__main__":
    sys.exit(main())
