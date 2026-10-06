#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatIncludeOrder.py

include 순서 · 중복을 고친다 — 규칙과 판정은 `gate/CheckIncludeOrder.py` 한 자리(이 픽서는 그 변환을 부른다). 게이트는 검사만 한다.

  py -3 Scripts/lint/fixer/FormatIncludeOrder.py --files Source/Engine/Scene/Scene.cpp   # 그 파일만
  py -3 Scripts/lint/fixer/FormatIncludeOrder.py --check                                  # 고치지 않고 검사만(Git 변경 파일, 없으면 전체)
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintFixer · gate
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from gate import CheckIncludeOrder  # noqa: E402
from LintFixer import FixPass, LintFixer  # noqa: E402


class FormatIncludeOrderFixer(LintFixer):
    description = "include 순서 · 중복을 고친다(CheckIncludeOrder 의 규칙)"
    listPass = (
        FixPass(transform=CheckIncludeOrder.fixText, bNeedsPath=True, samplePath="Source/Engine/Probe/Probe.cpp",
                problem="include 순서 · 중복이 규칙과 다르다", done="include 순서를 고쳤다",
                badSample=CheckIncludeOrder.kFixBadSample, goodSample=CheckIncludeOrder.kFixGoodSample),
    )


main = FormatIncludeOrderFixer.run

if __name__ == "__main__":
    sys.exit(main())
