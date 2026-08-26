#!/usr/bin/env python3
"""
Scripts/lint/RunClangFormat.py

Source / Test / Tools/ReflectionParser 내 C++ 코드에 대해 clang-format 포맷팅을 적용합니다.

사용법:
  py -3 Scripts/lint/RunClangFormat.py                    # 변경된 파일(없으면 전체) 자동 포맷팅
  py -3 Scripts/lint/RunClangFormat.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/RunClangFormat.py --all              # 프로젝트 전체 파일 강제 포맷팅
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    collectSourceFiles,
    getLintSearchDirs,
    getModifiedCppFiles,
    getProjectRoot,
    runClangFormatBatch,
)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 소스코드에 clang-format을 적용합니다.")
    parser.add_argument(
        "files",
        nargs="*",
        help="포맷팅할 특정 파일 경로 목록 (생략 시 Git 변경 파일, 변경 파일이 없으면 전체 대상)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="수정 여부와 관계없이 프로젝트 전체 소스코드에 적용",
    )
    args = parser.parse_args(argv)

    root = getProjectRoot()

    if args.files:
        fileList = [Path(f).resolve() for f in args.files if Path(f).is_file()]
    elif args.all:
        roots = getLintSearchDirs(root)
        fileList = collectSourceFiles(roots)
    else:
        modifiedFiles = getModifiedCppFiles(root)
        if modifiedFiles:
            fileList = modifiedFiles
            print(f"[RunClangFormat] Git 변경 파일 {len(fileList)}개 감지.", file=sys.stderr)
        else:
            roots = getLintSearchDirs(root)
            fileList = collectSourceFiles(roots)
            print(f"[RunClangFormat] 변경된 파일이 없어 전체 {len(fileList)}개 파일 대상 실행.", file=sys.stderr)

    if not fileList:
        sys.stderr.write("[RunClangFormat] 포맷팅 대상 C++ 파일이 없습니다.\n")
        return 0

    print(f"[RunClangFormat] {len(fileList)}개 파일에 대해 clang-format 적용 중...", file=sys.stderr)
    return runClangFormatBatch(fileList, checkOnly=False, cwd=root)


if __name__ == "__main__":
    sys.exit(main())
