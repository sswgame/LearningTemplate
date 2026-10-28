#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatModified.py

Git 작업 트리에서 수정되거나 새로 추가된(Untracked 포함) C++ 파일들에 대해서만
인클루드 순서 정리와 clang-format 자동 포맷팅을 적용합니다. 셰이더(.hlsl · .hlsli)는 clang-format 만 겁니다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.fixer import FormatIncludeOrder
from common.Constants import kShaderSourceExtensions
from common.ExitCode import ExitCode
from common.Host import getModifiedCppFiles, runClangFormatBatch
from common.Paths import getProjectRoot


def main(argv: list[str] | None = None) -> int:
    argparse.ArgumentParser(description="작업 트리의 변경된 C++ 파일에 include 순서 픽서와 clang-format 을 순서대로 — 인자 없음").parse_args(argv)

    projectRoot = getProjectRoot()
    modifiedFiles = getModifiedCppFiles(projectRoot)
    # 셰이더는 2단계 clang-format 만 건다 — 1단계는 C++ 규칙이다.
    modifiedShaders = getModifiedCppFiles(projectRoot, extensions=kShaderSourceExtensions)

    if not modifiedFiles and not modifiedShaders:
        print("[FormatModified] 변경된 C++ · 셰이더 소스 파일이 없습니다.")
        return ExitCode.Passed

    print(f"[FormatModified] {len(modifiedFiles)}개의 수정된 C++ 파일, {len(modifiedShaders)}개의 수정된 셰이더 발견.")

    # 1. include 순서 · 중복 정리(규칙은 lint/IncludeOrderRules.py)
    print("\n[1/2] Include 순서 및 중복 정리 중...")
    includeResults = FormatIncludeOrder.FormatIncludeOrderFixer().processFiles(modifiedFiles, checkOnly=False)
    for msg in includeResults:
        print(f"  - {msg}")
    if not includeResults:
        print("  - Include 검사 OK")

    # 2. clang-format 배치 실행 (in-place 포맷팅)
    print("\n[2/2] clang-format 실행 중...")
    resultCode = runClangFormatBatch(modifiedFiles + modifiedShaders, checkOnly=False, cwd=projectRoot)
    if resultCode != 0:
        print(f"\n[FormatModified] clang-format 실행 중 오류 발생 (exit {resultCode})")
        return resultCode

    print("\n[FormatModified] 성공적으로 모든 수정된 파일의 포맷팅을 완료했습니다!")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
