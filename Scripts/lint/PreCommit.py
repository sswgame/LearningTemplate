#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommit.py

Git pre-commit 훅에서 호출되어 staged C++ · 셰이더 파일에 include 순서 검사(`IncludeOrder.py`)와 clang-format 포맷 검사를 돌립니다.
검사에 실패하면 커밋을 중단시킵니다. 고치는 것은 `py -3 -m Scripts format` · `format-includes` 다.

**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ProjectConstants import kShaderSourceExtensions  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Git import getAllStagedFiles  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.Format import getStagedCppFiles, runClangFormatBatch  # noqa: E402
from lint.IncludeOrder import checkIncludeOrder  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    argparse.ArgumentParser(description="Git staged 파일에 커밋 전 검사(include 순서 · clang-format)를 돌린다 — 인자 없음").parse_args(argv)

    projectRoot = getProjectRoot()
    allStagedFiles = getAllStagedFiles(projectRoot)
    if not allStagedFiles:
        return ExitCode.Passed

    print(f"[PreCommit] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")
    hasErrors = False

    stagedCppFiles = getStagedCppFiles(projectRoot)
    stagedShaderFiles = getStagedCppFiles(projectRoot, extensions=kShaderSourceExtensions)

    # --- include 순서 -------------------------------------------------------------
    if stagedCppFiles:
        listViolation, checked = checkIncludeOrder(projectRoot, [str(path) for path in stagedCppFiles])
        if listViolation:
            hasErrors = True
            print(f"[CheckIncludeOrder] FAIL — Include 순서 규칙 위반 {len(listViolation)}건")
            for line in listViolation:
                print(f"  - {line.lstrip('!')}")
        else:
            print(f"[CheckIncludeOrder] OK ({checked} files)")

    # --- clang-format(셰이더도 같은 설정, Resource/.clang-format) -------------------
    if stagedCppFiles or stagedShaderFiles:
        if runClangFormatBatch(stagedCppFiles + stagedShaderFiles, checkOnly=True, cwd=projectRoot) != 0:
            hasErrors = True
            print("  [Error] 포맷팅 규칙에 어긋나는 파일이 있습니다. "
                  "'py -3 -m Scripts format' 으로 자동 수정한 뒤 다시 git add 하세요.")
        else:
            print(f"[포맷] C++ 파일 {len(stagedCppFiles)}개 · 셰이더 {len(stagedShaderFiles)}개 통과")

    if hasErrors:
        print("\n[PreCommit] [FAIL] 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
        return ExitCode.Violation

    print("\n[PreCommit] [OK] 모든 검사를 통과했습니다.")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
