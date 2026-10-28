#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommitLint.py

Git pre-commit 훅에서 호출되어 staged C++ · 셰이더 파일에 include 순서 검사(`gate/CheckIncludeOrder.py`)와 clang-format 포맷 검사를 돌립니다.
검사에 실패하면 커밋을 중단시킵니다. 고치는 것은 `py -3 -m Scripts format` · `format-includes` 다.

**병합 커밋은 새 내용만 본다.** 병합 중(`MERGE_HEAD` 가 있다)이면 병합으로 바뀐 파일이 전부 staged 로 잡히는데, 그 대부분은 한쪽 부모와 바이트가 같다 —
그 부모 커밋을 만들 때 이 훅이 같은 내용을 이미 검사했다. 그래서 staged 내용이 어느 부모의 같은 경로 blob 과도 다른 파일만 검사한다
(`StagedScope.selectFileScopedStaged`).

**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Constants import kShaderSourceExtensions  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Host import getAllStagedFiles, getStagedCppFiles, runClangFormatBatch  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.StagedScope import selectFileScopedStaged  # noqa: E402
from lint.gate.CheckIncludeOrder import checkIncludeOrder  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    argparse.ArgumentParser(description="Git staged 파일에 커밋 전 검사(include 순서 · clang-format)를 돌린다 — 인자 없음").parse_args(argv)

    projectRoot = getProjectRoot()
    allStagedFiles = getAllStagedFiles(projectRoot)
    if not allStagedFiles:
        return ExitCode.Passed

    print(f"[PreCommitLint] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")
    hasErrors = False

    setFileScoped = set(selectFileScopedStaged(projectRoot, allStagedFiles))
    stagedCppFiles = [path for path in getStagedCppFiles(projectRoot) if path in setFileScoped]
    stagedShaderFiles = [path for path in getStagedCppFiles(projectRoot, extensions=kShaderSourceExtensions) if path in setFileScoped]

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
        print("\n[PreCommitLint] [FAIL] 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
        return ExitCode.Violation

    print("\n[PreCommitLint] [OK] 모든 검사를 통과했습니다.")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
