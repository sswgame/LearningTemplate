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
from lint.IncludeOrderRules import buildHeaderLookupMap, inspectSource  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanSet  # noqa: E402


class CheckIncludeOrderGate(LintGate):
    """
    검사만 한다(게이트는 고치지 않는다 — 고치는 모드가 있으면 CTest 에 게이트로 등록돼 있어도 실패할 수 없다).
    고치는 것은 `fixer/FormatIncludeOrder.py`(같은 `IncludeOrderRules.inspectSource` 의 `fixedText`)와 그것을 부르는 `FormatModified.py`.
    소스 게이트다: BOM 을 뗀 글(`SourceFile.bodyText`)로 판정하고, 헤더 조회표는 훑기 전에 한 번 만들어 `args` 에 둔다(자식 프로세스로 같이 간다).
    """

    sourceScanSets = (SourceScanSet("cpp", kLintTargetRelDirs, tuple(sorted(kCppAllExtensions))),)

    description = "Include 순서 검사"
    buildComment = "Checking Include Order rules..."
    timeoutSeconds = 15
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    preCommitFileArgument = "--files"
    violationHeader = "Include 순서 규칙 위반"
    selfTestCases = [
        {
            # BOM 은 판정에서 떼고(`bodyText`) 고친 글에서 돌려놓는다 — BOM 때문에 첫 줄 `pch.h` 규칙이 어긋나지 않는다.
            "name": "BOM 이 있는 파일의 Engine 이 Core 보다 앞",
            "expectedViolation": "Source/Engine/Probe/ProbeBom.cpp: Include 순서/중복 문제가 발견되었습니다.",
            "files": {
                "Source/Engine/Probe/ProbeBom.cpp": (
                    '﻿#include "pch.h"\n\n'
                    '#include "Engine/Common/EngineServices.h"\n'
                    '#include "Core/File/FileUtil.h"\n'
                ),
            },
        },
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

    def prepareSourceScan(self, repositoryRoot: Path, args: argparse.Namespace) -> None:
        args.headerMaps = buildHeaderLookupMap(repositoryRoot)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return inspectSource(source, args.headerMaps)


main = CheckIncludeOrderGate.run


if __name__ == "__main__":
    sys.exit(main())
