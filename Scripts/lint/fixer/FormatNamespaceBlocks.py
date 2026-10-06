#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatNamespaceBlocks.py

한 namespace 블록에 클래스 · 구조체 정의가 여럿이면 정의마다 블록을 나눈다 — 규칙과 판정은 `gate/CheckNamespaceBlocks.py` 한 자리.

  py -3 Scripts/lint/fixer/FormatNamespaceBlocks.py --files Source/Engine/Scene/Scene.h
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintFixer · gate
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from gate import CheckNamespaceBlocks  # noqa: E402
from LintFixer import FixPass, LintFixer  # noqa: E402


class FormatNamespaceBlocksFixer(LintFixer):
    description = "정의마다 namespace 블록을 나눈다(CheckNamespaceBlocks 의 규칙)"
    listScopeRelDir = common.kLintTargetRelDirs   # 게이트가 보는 범위
    listPass = (
        FixPass(transform=CheckNamespaceBlocks.fixText, problem="한 namespace 블록에 정의가 여럿이다", done="정의마다 namespace 블록을 나눴다",
                badSample=CheckNamespaceBlocks.kFixBadSample, goodSample=CheckNamespaceBlocks.kFixGoodSample),
    )


main = FormatNamespaceBlocksFixer.run

if __name__ == "__main__":
    sys.exit(main())
