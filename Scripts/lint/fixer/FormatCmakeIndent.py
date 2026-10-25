#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatCmakeIndent.py

CMake 의 줄머리 공백 들여쓰기를 탭으로(4 칸 = 탭 하나, 남는 칸은 공백 그대로). 따옴표 문자열 안의 줄(`file(CONFIGURE CONTENT "…")` 의 C++ 코드 등)과
대괄호 인자(`[[ … ]]`)가 든 파일은 건드리지 않는다. 손대지 않는 영역(vcpkg 포트 툴체인 · 포트 파일)은 대상이 아니다.
규칙은 `Scripts/lint/CmakeIndentRules.py` 한 자리다(`inspectSource` — 파일 하나를 받아 고친 완성 글을 돌려주는 판정 함수).

  py -3 Scripts/lint/fixer/FormatCmakeIndent.py --files cmake/Engine/ModuleTargets.cmake
  py -3 Scripts/lint/fixer/FormatCmakeIndent.py --all --check
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Constants import kNotOurDirNames  # noqa: E402
from lint.CmakeIndentRules import inspectSource  # noqa: E402
from lint.LintFixer import FixerFileKind, FixPass, LintFixer  # noqa: E402


class FormatCmakeIndentFixer(LintFixer):
    description = "CMake 들여쓰기를 탭으로(문자열 안은 그대로)"
    fileKind = FixerFileKind(suffixes=(".cmake",), fileNames=("CMakeLists.txt",),
                             excludedDirNames=kNotOurDirNames | {"vcpkg-port"},
                             # vcpkg 포트 툴체인 — 손대지 않는 영역(따로 도는 CMake 프로세스가 읽는다)
                             excludedRelDirs=("cmake/Modules/Toolchain",))
    listPass = (
        FixPass(inspect=inspectSource, problem="줄머리에 공백 들여쓰기가 있다", done="들여쓰기를 탭으로 바꿨다",
                badSample="if(A)\n    set(x 1)\nendif()\n",
                fixedSample="if(A)\n\tset(x 1)\nendif()\n",
                goodSample='if(A)\n\tset(x 1)\n\tfile(CONFIGURE OUTPUT y CONTENT "\n    keep me\n")\nendif()\n'),
    )


main = FormatCmakeIndentFixer.run

if __name__ == "__main__":
    sys.exit(main())
