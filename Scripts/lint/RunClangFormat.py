#!/usr/bin/env python3
"""
Scripts/lint/RunClangFormat.py

CI 환경과 동일하게 Source / Test / Tools/ReflectionParser 내 모든 C++ 코드에 대해
clang-format 포맷팅을 실행하거나 검사합니다.

사용법:
  py -3 Scripts/lint/RunClangFormat.py           # 파일 직접 수정 (in-place)
  py -3 Scripts/lint/RunClangFormat.py --check   # 수정 없이 규칙 위반만 검사 (dry-run --Werror)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import collectSourceFiles, getProjectRoot, runClangFormatBatch

_kFormatRoots = ("Source", "Test", "Tools/ReflectionParser")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Source/Test/ReflectionParser 전체에 clang-format을 적용합니다.")
    parser.add_argument(
        "--check",
        action="store_true",
        help="파일을 수정하지 않고 포맷팅 규칙 준수 여부만 검사합니다 (CI용 --Werror 모드)",
    )
    args = parser.parse_args(argv)

    root = getProjectRoot()
    roots = [root / rel for rel in _kFormatRoots]
    fileList = collectSourceFiles(roots)

    if not fileList:
        sys.stderr.write("[RunClangFormat] 포맷팅 대상 C++ 파일이 없습니다.\n")
        return 1

    print(f"[RunClangFormat] {len(fileList)}개 파일에 대해 clang-format 적용 중...", file=sys.stderr)
    return runClangFormatBatch(fileList, checkOnly=args.check, cwd=root)


if __name__ == "__main__":
    sys.exit(main())
