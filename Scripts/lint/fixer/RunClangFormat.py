#!/usr/bin/env python3
"""
Scripts/lint/fixer/RunClangFormat.py

Source / Test / Tools/ReflectionParser 내 C++ 코드에 대해 clang-format 포맷팅을 적용합니다.

사용법:
  py -3 Scripts/lint/fixer/RunClangFormat.py                    # 변경된 파일(없으면 전체) 자동 포맷팅
  py -3 Scripts/lint/fixer/RunClangFormat.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/fixer/RunClangFormat.py --all              # 프로젝트 전체 파일 강제 포맷팅
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
from common import getProjectRoot, runClangFormatBatch

# 파일을 고쳐 쓰므로 `report/` 가 아니라 `fixer/` 에 있다. 변환이 clang-format 이라 파이썬 변환(`FixPass`)이 없다.
kFixerSkipReason = "파이썬 변환(FixPass)이 아니라 clang-format 을 부르는 실행기다 — 대상 파일 고르기만 LintFixer 와 같다"


def main(argv: Sequence[str] | None = None) -> int:

    parser = argparse.ArgumentParser(description="C++ 소스코드에 clang-format을 적용합니다.")
    addFileArguments(parser)
    args = parser.parse_args(argv)

    root = getProjectRoot()
    fileList = selectTargetFiles(args, root, "RunClangFormat")

    if not fileList:
        sys.stderr.write("[RunClangFormat] 포맷팅 대상 C++ 파일이 없습니다.\n")
        return 0

    FormatForwardDeclarations.FormatForwardDeclarationsFixer().processFiles(fileList, checkOnly=False)
    FormatBranchBraces.FormatBranchBracesFixer().processFiles(fileList, checkOnly=False)

    print(f"[RunClangFormat] {len(fileList)}개 파일에 대해 clang-format 적용 중...", file=sys.stderr)
    return runClangFormatBatch(fileList, checkOnly=False, cwd=root)


if __name__ == "__main__":
    sys.exit(main())
