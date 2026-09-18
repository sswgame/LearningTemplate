#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
nullptr 을 돌려줄 수 있는 조회 함수를 곧바로 `->` 로 따라가는 곳을 잡는다.

에디터의 `editor::getService<T>()` 는 **문서대로 nullptr 을 돌려줄 수 있다** — 지역 등록도
없고 모듈 서비스 표에도 없으면 그렇다. 그런데 2026-09-18 에 세어 보니 열세 자리가 그 값을
확인 없이 `->` 로 따라가고 있었다. 커맨드 스택처럼 EditorModule 보다 오래 사는 서비스는
종료할 때 결합이 먼저 풀리므로, 그 창에 걸리면 널 역참조다.

같은 파일 안에서 어떤 호출부는 확인하고 어떤 호출부는 확인하지 않는 것이 문제의 모양이었다
(`EditorTransaction::push` 는 확인했고 그 위 일곱은 안 했다). 사람이 지킬 규칙이 아니라
기계가 볼 규칙으로 옮긴다.

  python Scripts/lint/gate/CheckNullableServiceUse.py [--root <repo>] [--files a.cpp b.cpp]
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

# `getService<T>()->` · `getService< T >()->` 처럼 곧바로 화살표가 붙는 모양만 잡는다.
# 포인터를 받아 두고 확인한 뒤 쓰는 형태(`T* p = getService<T>();`)는 걸리지 않는다.
_kNullableCallRe = re.compile(r"\bgetService\s*<[^<>()]{1,80}>\s*\(\s*\)\s*->")

_kScanRoot = "Source/Editor"


def findDirectDereferences(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """`getService<T>()->` 꼴을 모아 위반 문자열로 돌려줍니다."""
    if listTargetFile:
        listPath = [repositoryRoot / f for f in listTargetFile]
    else:
        listPath = collectSourceFiles([repositoryRoot / _kScanRoot], {".cpp", ".h"})

    violations: list[str] = []
    for path in listPath:
        if not path.exists() or path.is_dir():
            continue
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if not relative.startswith(_kScanRoot):
            continue

        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue

        for lineIndex, line in enumerate(text.splitlines(), start=1):
            if _kNullableCallRe.search(line):
                violations.append(f"[Nullable Service] {relative}:{lineIndex}: {line.strip()}")
    return violations


class CheckNullableServiceUseGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "nullptr 가능 서비스 조회를 확인 없이 역참조하는 곳 검사"
    buildComment = "Checking nullable editor service dereferences..."
    timeoutSeconds = 15
    preCommitPattern = ("Source/Editor/*",)
    preCommitFileArgument = "--files"
    violationHeader = "확인 없이 역참조한 서비스 조회"
    hint = (
        "  editor::getService<T>() 는 nullptr 을 돌려줄 수 있습니다.\n"
        "  포인터를 받아 두고 확인한 뒤 쓰십시오:\n"
        "      CommandStack* pStack = editor::getService<CommandStack>();\n"
        "      if ( pStack == nullptr )\n"
        "          return;"
    )
    selfTestCases = [
        {
            "name": "서비스 조회를 바로 역참조",
            "files": {
                "Source/Editor/Probe/ProbeService.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    editor::getService<CommandStack>()->undo();\n"
                    "}\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 특정 파일 (생략 시 Source/Editor 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations = findDirectDereferences(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Source/Editor 의 서비스 조회 역참조")


main = CheckNullableServiceUseGate.run


if __name__ == "__main__":
    sys.exit(main())
