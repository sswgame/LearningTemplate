#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Include 순서 및 스타일 검사 린터.

규칙:
1. .cpp 파일의 첫 번째 include는 무조건 "pch.h" 이어야 합니다.
2. 프로젝트 헤더 ("...") 인클루드가 시스템/외부 헤더 (<...>) 인클루드보다 먼저 와야 합니다.

이 게이트는 검사만 한다 — 고치는 것은 같은 규칙을 부르는 픽서 `Scripts/lint/fixer/FormatIncludeOrder.py`(규칙 · 판정은 `Scripts/lint/IncludeOrderRules.py` 한 자리).

사용법:
  python Scripts/lint/gate/CheckIncludeOrder.py [--root <repo>] [--files <path> ...]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kCppAllExtensions, kLintTargetRelDirs  # noqa: E402
from common.Parallel import flatMapConcurrent  # noqa: E402
from lint.IncludeOrderRules import buildHeaderLookupMap, computeIncludeOrderInternal  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402


def findViolations(filePath: Path, repositoryRoot: Path,
                   sourceHeaderMap: dict[str, str] | None = None,
                   testHeaderMap: dict[str, str] | None = None,
                   toolsHeaderMap: dict[str, str] | None = None) -> list[str]:
    """파일 하나의 위반 — 읽기만 한다(고치는 것은 `fixText` · `FormatIncludeOrder`)."""
    relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8-sig", errors="strict")
    except (OSError, UnicodeDecodeError) as error:
        return [f"[CheckIncludeOrder] 읽기 실패: {relativeFilePath}: {error}"]
    newText, violationsList = computeIncludeOrderInternal(text, relativeFilePath, sourceHeaderMap, testHeaderMap, toolsHeaderMap)
    if newText != text:
        violationsList.append(f"{relativeFilePath}: Include 순서/중복 문제가 발견되었습니다. "
                              f"(`py -3 Scripts/lint/fixer/FormatIncludeOrder.py --files {relativeFilePath}` 로 고친다)")
    return violationsList


class CheckIncludeOrderGate(LintGate):
    """
    검사만 한다(게이트는 고치지 않는다 — 고치는 모드가 있으면 CTest 에 게이트로 등록돼 있어도 실패할 수 없다).
    고치는 것은 `fixer/FormatIncludeOrder.py`(같은 `computeIncludeOrderInternal`)와 그것을 부르는 `FormatModified.py`.
    """

    description = "Include 순서 검사"
    buildComment = "Checking Include Order rules..."
    timeoutSeconds = 15
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    preCommitFileArgument = "--files"
    violationHeader = "Include 순서 규칙 위반"
    selfTestCases = [
        {
            "name": "Engine 이 Core 보다 앞",
            "expectedViolation": "Source/Engine/Probe/Probe.cpp: Include 순서/중복 문제가 발견되었습니다. (`py -3 Scripts/lint/fixer/FormatIncludeOrder.py --files Source/Engine/Probe/Probe",
            "files": {
                "Source/Engine/Probe/Probe.cpp": (
                    '#include "pch.h"\n\n'
                    '#include "Engine/Common/EngineServices.h"\n'
                    '#include "Core/File/FileUtil.h"\n'
                ),
            },
        },
        {
            "name": "같은 플랫폼 조건의 include 블록 둘",
            "expectedViolation": "Source/Engine/Probe/Probe.cpp:9: SW_PLATFORM_* 로 include 를 고르는 #if 블록이 3 줄에도 있습니다",
            "files": {
                "Source/Engine/Probe/Probe.cpp": (
                    '#include "pch.h"\n\n'
                    '#if defined( SW_PLATFORM_WINDOWS )\n'
                    '    #include "Core/File/Windows/WindowsFileDialog.h"\n'
                    '#elif defined( SW_PLATFORM_LINUX )\n'
                    '    #include "Core/File/Linux/LinuxFileDialog.h"\n'
                    '#endif\n\n'
                    '#if defined( SW_PLATFORM_LINUX )\n'
                    '    #include <link.h>\n'
                    '#endif\n'
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None,
                            help="검사할 파일 (생략 시 전체). 헤더 조회표는 어차피 전체를 봐야 만들어진다")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        allFiles = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=kLintTargetRelDirs, suffixes=kCppAllExtensions)

        sourceHeaderMap, testHeaderMap, toolsHeaderMap = buildHeaderLookupMap(repositoryRoot)

        violations = flatMapConcurrent(
            lambda path: findViolations(path, repositoryRoot, sourceHeaderMap, testHeaderMap, toolsHeaderMap),
            allFiles,
        )
        return GateResult(listViolation=violations, summary=f"{len(allFiles)} files scanned")


main = CheckIncludeOrderGate.run


if __name__ == "__main__":
    sys.exit(main())
