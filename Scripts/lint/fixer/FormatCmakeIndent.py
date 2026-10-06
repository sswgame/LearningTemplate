#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatCmakeIndent.py

CMake 의 줄머리 공백 들여쓰기를 탭으로(4 칸 = 탭 하나, 남는 칸은 공백 그대로). 따옴표 문자열 안의 줄(`file(CONFIGURE CONTENT "…")` 의 C++ 코드 등)과
대괄호 인자(`[[ … ]]`)가 든 파일은 건드리지 않는다. 손대지 않는 영역(vcpkg 포트 툴체인 · 포트 파일)은 대상이 아니다.

  py -3 Scripts/lint/fixer/FormatCmakeIndent.py --files cmake/Engine/ModuleTargets.cmake
  py -3 Scripts/lint/fixer/FormatCmakeIndent.py --all --check
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintFixer
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import kNotOurDirNames  # noqa: E402
from LintFixer import FixerFileKind, FixPass, LintFixer  # noqa: E402

_kLeadingWhitespaceRe = re.compile(r"^[ \t]+")


def advanceStringStateInternal(line: str, bInString: bool) -> bool:
    """줄 하나를 지난 뒤 따옴표 문자열 안인가. 문자열 밖의 `#` 부터는 주석이다."""
    index = 0
    while index < len(line):
        character = line[index]
        if bInString:
            if character == "\\":
                index += 2
                continue
            if character == '"':
                bInString = False
        elif character == '"':
            bInString = True
        elif character == "#":
            break
        index += 1
    return bInString


def indentWithTabs(text: str) -> tuple[str, bool]:
    """픽서 변환 — 문자열 밖 줄의 줄머리 공백을 탭으로. 줄끝(CRLF)은 지킨다."""
    if "[[" in text or "[=[" in text:
        return text, False                    # 대괄호 인자 — 줄 안팎을 가를 수 없다, 손대지 않는다
    newline = "\r\n" if "\r\n" in text else "\n"
    listLine = text.replace("\r\n", "\n").split("\n")
    bInString = False
    for index, line in enumerate(listLine):
        if not bInString:
            match = _kLeadingWhitespaceRe.match(line)
            if match and " " in match.group(0):
                width = 0
                for character in match.group(0):
                    width = (width // 4 + 1) * 4 if character == "\t" else width + 1
                listLine[index] = "\t" * (width // 4) + " " * (width % 4) + line[match.end():]
        bInString = advanceStringStateInternal(line, bInString)
    newText = newline.join(listLine)
    return newText, newText != text


class FormatCmakeIndentFixer(LintFixer):
    description = "CMake 들여쓰기를 탭으로(문자열 안은 그대로)"
    fileKind = FixerFileKind(suffixes=(".cmake",), fileNames=("CMakeLists.txt",),
                             excludedDirNames=kNotOurDirNames | {"vcpkg-port"},
                             # vcpkg 포트 툴체인 — 손대지 않는 영역(따로 도는 CMake 프로세스가 읽는다)
                             excludedRelDirs=("cmake/Modules/Toolchain",))
    listPass = (
        FixPass(transform=indentWithTabs, problem="줄머리에 공백 들여쓰기가 있다", done="들여쓰기를 탭으로 바꿨다",
                badSample="if(A)\n    set(x 1)\nendif()\n",
                goodSample='if(A)\n\tset(x 1)\n\tfile(CONFIGURE OUTPUT y CONTENT "\n    keep me\n")\nendif()\n'),
    )


main = FormatCmakeIndentFixer.run

if __name__ == "__main__":
    sys.exit(main())
