#!/usr/bin/env python3
"""
Scripts/lint/fixer/FormatClangFormat.py

Source / Test / Tools/ReflectionParser 내 C++ 코드와 셰이더(.hlsl · .hlsli)에 clang-format 포맷팅을 적용합니다.

사용법:
  py -3 Scripts/lint/fixer/FormatClangFormat.py                    # 변경된 파일(없으면 전체) 자동 포맷팅
  py -3 Scripts/lint/fixer/FormatClangFormat.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/fixer/FormatClangFormat.py --all              # 프로젝트 전체 파일 강제 포맷팅
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.fixer import FormatBranchBraces
from lint.fixer import FormatForwardDeclarations
from lint.LintFixer import FixerFileKind, addFileArguments, selectFixerTargetFiles
from common.Constants import kShaderSourceExtensions
from common.Host import runClangFormatBatch
from common.Paths import getProjectRoot

# 파일을 고쳐 쓰므로 `report/` 가 아니라 `fixer/` 에 있다. 변환이 clang-format 이라 파이썬 변환(`FixPass`)이 없다.
kFixerSkipReason = "파이썬 변환(FixPass)이 아니라 clang-format 을 부르는 실행기다 — 대상 파일 고르기만 LintFixer 와 같다"

kShaderFileKind = FixerFileKind(suffixes=tuple(sorted(kShaderSourceExtensions)))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 소스코드에 clang-format을 적용합니다.")
    addFileArguments(parser)
    args = parser.parse_args(argv)

    root = getProjectRoot()
    listCpp = selectFixerTargetFiles(args, root, "FormatClangFormat")
    # 셰이더는 clang-format 만 건다 — 앞의 두 픽서는 C++ 전방 선언 · 분기 규칙이다.
    listShader = selectFixerTargetFiles(args, root, "FormatClangFormat", kShaderFileKind)
    fileList = listCpp + listShader

    if not fileList:
        sys.stderr.write("[FormatClangFormat] 포맷팅 대상 C++ · 셰이더 파일이 없습니다.\n")
        return 0

    FormatForwardDeclarations.FormatForwardDeclarationsFixer().processFiles(listCpp, checkOnly=False)
    FormatBranchBraces.FormatBranchBracesFixer().processFiles(listCpp, checkOnly=False)

    print(f"[FormatClangFormat] {len(fileList)}개 파일(셰이더 {len(listShader)}개)에 대해 clang-format 적용 중...", file=sys.stderr)
    return runClangFormatBatch(fileList, checkOnly=False, cwd=root)


if __name__ == "__main__":
    sys.exit(main())
