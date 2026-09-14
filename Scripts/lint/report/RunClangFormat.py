#!/usr/bin/env python3
"""
Scripts/lint/report/RunClangFormat.py

Source / Test / Tools/ReflectionParser 내 C++ 코드에 대해 clang-format 포맷팅을 적용합니다.

사용법:
  py -3 Scripts/lint/report/RunClangFormat.py                    # 변경된 파일(없으면 전체) 자동 포맷팅
  py -3 Scripts/lint/report/RunClangFormat.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/report/RunClangFormat.py --all              # 프로젝트 전체 파일 강제 포맷팅
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — 사촌 린트 패키지
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from fixer import FormatBranchBraces
from fixer import FormatForwardDeclarations
from LintFixer import addFileArguments, selectTargetFiles
from common import getProjectRoot, runClangFormatBatch, useUtf8Stdout


def main(argv: Sequence[str] | None = None) -> int:
    useUtf8Stdout()

    parser = argparse.ArgumentParser(description="C++ 소스코드에 clang-format을 적용합니다.")
    addFileArguments(parser)
    args = parser.parse_args(argv)

    root = getProjectRoot()
    fileList = selectTargetFiles(args, root, "RunClangFormat")

    if not fileList:
        sys.stderr.write("[RunClangFormat] 포맷팅 대상 C++ 파일이 없습니다.\n")
        return 0

    FormatForwardDeclarations.formatForwardDeclarationsBatch(fileList, checkOnly=False)
    FormatBranchBraces.formatBranchBracesBatch(fileList, checkOnly=False)

    print(f"[RunClangFormat] {len(fileList)}개 파일에 대해 clang-format 적용 중...", file=sys.stderr)
    return runClangFormatBatch(fileList, checkOnly=False, cwd=root)


if __name__ == "__main__":
    sys.exit(main())
