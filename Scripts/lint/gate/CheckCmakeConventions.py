#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckCmakeConventions.py

`AGENTS.md` 의 **CMake** 명명 규칙을 검사합니다.

Python 과 같은 이유로 여기 있다 — `AGENTS.md` 는 CMake 규칙을 네 줄 적어 두었지만 어떤 게이트도
`.cmake` 를 열어 본 적이 없다. 빌드 시스템 5,000 줄이 검사 밖에 있었다.

검사 규칙 (`AGENTS.md` → "### CMake", `cmake/README.md` → "네이밍 컨벤션"):

- `function()` / `macro()` 이름은 `sw_camelCase`
- `option()` 이름은 `SW_UPPER_SNAKE_CASE`
- 함수 안 지역 변수는 `camelCase` — `_` 로 시작하지 않는다

**서드파티는 보지 않는다.** `ThirdParty/` 와 `Tools/vcpkg/` 는 남의 규칙으로 쓰인 코드다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: `function(name ...)` · `macro(name ...)` 선언.
_kDefinitionRe = re.compile(r'^\s*(function|macro)\s*\(\s*([A-Za-z0-9_]+)', re.IGNORECASE)

#: `option(NAME "설명" 기본값)` 선언.
_kOptionRe = re.compile(r'^\s*option\s*\(\s*([A-Za-z0-9_]+)', re.IGNORECASE)

#: 함수 안에서 변수를 **만드는** 자리. `set()` 만 보면 놓친다 — `file(GLOB _x ...)` ·
#: `string(REPLACE ... _x)` · `file(STRINGS ... _x ...)` 도 전부 변수를 만든다. 그래서 만드는
#: 자리를 열거하는 대신 **쓰이는 자리**(`${_x}`)를 같이 본다: 지역 변수는 언젠가 반드시
#: 역참조되므로, 어느 명령이 만들었든 이 한 줄에 걸린다.
_kSetRe = re.compile(r'^\s*set\s*\(\s*([A-Za-z0-9_]+)')
_kDereferenceRe = re.compile(r'\$\{(_[A-Za-z0-9_]+)\}')

#: `sw_camelCase` — `sw_` 다음이 소문자로 시작하고 밑줄이 더 없다.
_kFunctionNameRe = re.compile(r'^sw_[a-z][a-zA-Z0-9]*$')

#: `SW_UPPER_SNAKE_CASE`
_kOptionNameRe = re.compile(r'^SW_[A-Z0-9_]+$')

#: 주석 줄.
_kCommentRe = re.compile(r'^\s*#')

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.
_kExcludedPart = ("ThirdParty", "vcpkg", "build", "generated")

#: CMake 자신이 정한 이름들 — 우리 규칙을 들이댈 수 없다.
_kReservedVariablePrefix = ("CMAKE_", "CTEST_", "CPACK_", "ENV", "SW_", "VCPKG_", "Python3_", "_CMAKE_")


def isExcludedPathInternal(path: Path) -> bool:
    return any(part in _kExcludedPart for part in path.parts)


def checkCmakeFileInternal(path: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나를 줄 단위로 봅니다."""
    relPath = path.relative_to(repositoryRoot).as_posix()
    listViolation: list[str] = []
    bInFunction = False
    setReported: set[str] = set()

    for lineNumber, line in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), start=1):
        if _kCommentRe.match(line):
            continue

        if definitionMatch := _kDefinitionRe.match(line):
            keyword = definitionMatch.group(1).lower()
            name = definitionMatch.group(2)
            bInFunction = True

            if not _kFunctionNameRe.match(name):
                listViolation.append(
                    f"{relPath}:{lineNumber} {keyword} '{name}' 는 sw_camelCase 여야 합니다 "
                    f"— AGENTS.md '### CMake'"
                )
            continue

        if re.match(r'^\s*end(function|macro)\s*\(', line, re.IGNORECASE):
            bInFunction = False
            continue

        if optionMatch := _kOptionRe.match(line):
            name = optionMatch.group(1)
            if not _kOptionNameRe.match(name):
                listViolation.append(
                    f"{relPath}:{lineNumber} option '{name}' 는 SW_UPPER_SNAKE_CASE 여야 합니다 "
                    f"— AGENTS.md '### CMake'"
                )
            continue

        if not bInFunction:
            continue

        listCandidate = _kDereferenceRe.findall(line)
        if setMatch := _kSetRe.match(line):
            listCandidate.append(setMatch.group(1))

        for name in listCandidate:
            if not name.startswith("_") or name in setReported:
                continue
            if name.startswith(_kReservedVariablePrefix) or name.lstrip("_").startswith("CMAKE"):
                continue

            setReported.add(name)
            listViolation.append(
                f"{relPath}:{lineNumber} 함수 내부 변수 '{name}' 는 '_' 없이 camelCase 여야 합니다 "
                f"('{name.lstrip('_')}' 권장) — cmake/README.md '네이밍 컨벤션'"
            )

    return listViolation


class CheckCmakeConventionsGate(LintGate):
    """`AGENTS.md` 의 CMake 규칙 — Python 과 함께, 지금까지 아무도 보지 않던 자리다."""

    description = "CMake 명명 규칙 검사 (AGENTS.md '### CMake')"
    buildComment = "Checking CMake naming conventions (AGENTS.md)..."
    timeoutSeconds = 20
    violationHeader = "CMake 명명 규칙 위반"
    hint = "  AGENTS.md '### CMake': function/macro 는 sw_camelCase, option 은 SW_UPPER_SNAKE_CASE, 함수 내부 변수는 '_' 없는 camelCase."
    selfTestCases = [
        {
            "name": "sw_ 없는 function",
            "files": {"cmake/Probe/Bad.cmake": "function(addSomething TARGET_NAME)\nendfunction()\n"},
        },
        {
            "name": "SW_ 없는 option",
            "files": {"cmake/Probe/BadOption.cmake": 'option(ENABLE_PROBE "probe" OFF)\n'},
        },
        {
            "name": "'_' 로 시작하는 함수 내부 변수",
            "files": {
                "cmake/Probe/BadLocal.cmake":
                    "function(sw_doProbe)\n\tset(_probeValue 1)\nendfunction()\n"
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 파일 (생략 시 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        if args.files:
            listPath = [Path(item).resolve() for item in args.files]
            listPath = [
                path for path in listPath
                if path.is_file() and (path.suffix == ".cmake" or path.name == "CMakeLists.txt")
            ]
        else:
            listPath = sorted(
                path
                for pattern in ("*.cmake", "CMakeLists.txt")
                for path in repositoryRoot.rglob(pattern)
                if not isExcludedPathInternal(path.relative_to(repositoryRoot))
            )

        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(checkCmakeFileInternal(path, repositoryRoot))

        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} cmake files scanned")


main = CheckCmakeConventionsGate.run


if __name__ == "__main__":
    sys.exit(main())
