#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
한 파일에 클래스 · 구조체 정의가 여럿이면 **정의마다 namespace 블록을 따로** 둔다.

  한 블록에 클래스 여럿이 들어 있으면 에디터에서 블록을 접을 때 클래스 단위로 접히지 않고, 클래스 경계가 들여쓰기 하나로만 갈린다.
  `.cpp` 의 내부 도우미를 클래스 구현과 다른 `namespace sw` 블록에 두는 규칙(docs/agents/Helpers.md "Helpers: Util vs Internal")과 같은 이유다.

  - 대상은 이름 있는 namespace 블록 바로 안의 `class` · `struct` **정의**다(전방 선언 · 한 줄 정의 · enum 은 아니다). 익명 namespace 는
    파일당 하나인 내부 도우미 블록이라 나누지 않는다(CheckCodeConventions `Structure/AnonymousNamespaceCount`).
  - 이름이 같은 정의가 이어지면(템플릿 특수화) 한 묶음으로 둔다.
  - 나누는 자리는 앞 정의의 `};` 바로 뒤다. 그 사이의 함수 · 상수 · 주석은 뒤 정의의 블록으로 간다.
  - 나눌 자리가 namespace 를 연 줄과 다른 전처리기 조건(`#if`) 깊이에 있으면 나누지 않는다 — 한쪽 가지에서만 블록이 맞게 된다.

  python Scripts/lint/gate/CheckNamespaceBlocks.py [--files <path> ...]          # 검사
  python Scripts/lint/fixer/FormatNamespaceBlocks.py [--files <path> ...]       # 고치기(같은 splitNamespaceBlocks)
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.NamespaceBlockRules import inspectSource  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanSet  # noqa: E402


class CheckNamespaceBlocksGate(LintGate):
    """
    검사만 한다 — 고치는 것은 `fixer/FormatNamespaceBlocks.py`(같은 `NamespaceBlockRules.inspectSource` 의 `fixedText`)와 그것을 부르는 `FormatModified.py`.
    소스 게이트다: 원본 글(`SourceFile.rawText`)의 줄 끝 · BOM 을 지키는 판정이라 정규화한 `text` 가 아니라 원본을 나눈다.
    """

    sourceScanSets = (SourceScanSet("cpp", kLintTargetRelDirs, (".h", ".cpp", ".inl")),)

    description = "클래스마다 namespace 블록 검사"
    buildComment = "Checking that each class or struct definition has its own namespace block..."
    timeoutSeconds = 30
    preCommitPattern = ("*.cpp", "*.h", "*.inl")
    preCommitFileArgument = "--files"
    violationHeader = "한 namespace 블록에 정의 여럿"
    selfTestCases = [
        {
            # 원본(`rawText`)을 나눈다 — BOM 이 있어도 줄 번호가 어긋나지 않는다(이 조각은 임시 트리에 쓸 때 윈도우에서 줄 끝이 CRLF 가 된다).
            "name": "BOM 이 있어도 한 블록에 클래스 둘",
            "expectedViolation": "Source/Engine/Probe/ProbeBom.h:9: 한 namespace 블록에 클래스 · 구조체 정의가 여럿입니다",
            "files": {
                "Source/Engine/Probe/ProbeBom.h": (
                    "﻿#pragma once\n\nnamespace sw\n{\n    class Alpha\n    {\n    };\n\n    class Beta\n    {\n    };\n} // namespace sw\n"
                ),
            },
        },
        {
            "name": "한 블록에 클래스 둘",
            "expectedViolation": "Source/Engine/Probe/Probe.h:9: 한 namespace 블록에 클래스 · 구조체 정의가 여럿입니다",
            "files": {
                "Source/Engine/Probe/Probe.h": (
                    "#pragma once\n\nnamespace sw\n{\n    class Alpha\n    {\n    };\n\n    class Beta\n    {\n    };\n} // namespace sw\n"
                ),
            },
        },
        {
            "name": "구조체와 클래스(상속 · 매크로 · final)",
            "expectedViolation": "Source/Engine/Probe/Probe.h:10: 한 namespace 블록에 클래스 · 구조체 정의가 여럿입니다",
            "files": {
                "Source/Engine/Probe/Probe.h": (
                    "#pragma once\n\nnamespace sw::editor\n{\n    struct Settings\n    {\n        int value{ 0 };\n    };\n\n"
                    "    class SW_API Panel final : public IPanel\n    {\n    };\n} // namespace sw::editor\n"
                ),
            },
        },
        {
            # 두 줄 전방 선언의 `;` 가 여는 중인 정의를 지운다 — 안 지우면 Alpha 의 `{` 가 Beta 로 읽혀 같은 이름 한 묶음이 된다.
            "name": "두 줄 전방 선언 뒤의 정의 둘",
            "expectedViolation": "Source/Engine/Probe/Probe.h:12: 한 namespace 블록에 클래스 · 구조체 정의가 여럿입니다",
            "files": {
                "Source/Engine/Probe/Probe.h": (
                    "#pragma once\n\nnamespace sw\n{\n    struct Beta\n    ;\n\n    class Alpha\n    {\n    };\n\n    class Beta\n    {\n    };\n"
                    "} // namespace sw\n"
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return inspectSource(source)


main = CheckNamespaceBlocksGate.run


if __name__ == "__main__":
    sys.exit(main())
