#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckCmakeConventions.py

`AGENTS.md` 의 **CMake** 명명 규칙을 검사합니다.

Python 과 같은 이유로 여기 있다 — C++ 게이트는 `.cmake` 를 열지 않으므로, 이 게이트가 없으면 빌드 시스템
수천 줄이 `AGENTS.md` 의 CMake 규칙 밖에 남는다.

검사 규칙 (`AGENTS.md` → "### CMake", `cmake/README.md` → "네이밍 컨벤션"):

- `function()` / `macro()` 이름은 `sw_camelCase`
- `option()` 이름은 `SW_UPPER_SNAKE_CASE`
- 함수 안 지역 변수는 `camelCase` — `_` 로 시작하지 않는다(쓰는 자리 `${_x}` · `IN LISTS _x` · `if(_x)` 와 만드는 자리 둘 다 본다)
- `SHARED` · `MODULE` 라이브러리는 팩토리(`sw_addModuleLibrary` — `cmake/Engine/ModuleTargets.cmake`)와 Engine 자신만 만든다
- 파일 스코프에서 CMake 내장 경로를 다른 이름에 그대로 담지 않는다(별칭 금지 — `${CMAKE_BINARY_DIR}` 를 그대로 쓴다)
- `if(COMMAND sw_…)` 가드를 두지 않는다 — 우리 함수는 include 순서로 늘 정의돼 있다(가드가 있으면 정의 순서를 고친다)

**서드파티는 보지 않는다.** `ThirdParty/` 와 `Tools/vcpkg/` 는 남의 규칙으로 쓰인 코드다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kNotOurDirNames  # noqa: E402
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

#: 역참조(`${_x}`) 없이 이름만으로 쓰는 자리 — `foreach(x IN LISTS _x)` · `if(_x)` · `if(NOT _x)`. 이름만 쓰이는 지역 변수는
#: 위 한 줄로는 잡히지 않는다(`_allHeaders` · `_hasReflect` 가 그 사각에서 살았다).
_kBareUseRe = re.compile(r'\bIN\s+LISTS\s+(_[A-Za-z0-9_]+)|^\s*(?:else)?if\s*\(\s*(?:NOT\s+)?(_[A-Za-z0-9_]+)\b', re.IGNORECASE)

#: 변수를 **만드는** 자리 — 명령마다 결과 변수의 자리가 달라 명령별로 한 줄씩.
_kListLocalCreateRe: tuple[re.Pattern[str], ...] = (
    re.compile(r'^\s*(?:set|foreach)\s*\(\s*(_[A-Za-z0-9_]+)', re.IGNORECASE),
    re.compile(r'^\s*list\s*\(\s*[A-Z_]+\s+(_[A-Za-z0-9_]+)', re.IGNORECASE),
    re.compile(r'^\s*file\s*\(\s*(?:GLOB|GLOB_RECURSE)\s+(_[A-Za-z0-9_]+)', re.IGNORECASE),
    re.compile(r'^\s*file\s*\(\s*(?:STRINGS|READ)\s+\S+\s+(_[A-Za-z0-9_]+)', re.IGNORECASE),
    re.compile(r'^\s*(?:get_filename_component|get_property|get_target_property)\s*\(\s*(_[A-Za-z0-9_]+)', re.IGNORECASE),
    re.compile(r'^\s*math\s*\(\s*EXPR\s+(_[A-Za-z0-9_]+)', re.IGNORECASE),
    re.compile(r'^\s*string\s*\((?!\s*JSON\b).*\s(_[A-Za-z0-9_]+)\s*\)\s*$', re.IGNORECASE),
)

#: `add_library(<이름> SHARED|MODULE|${종류} …)` — 모듈 라이브러리는 팩토리만 만든다.
_kModuleLibraryRe = re.compile(r'^\s*add_library\s*\(\s*\S+\s+(SHARED|MODULE|\$\{)', re.IGNORECASE)

#: 파일 스코프의 `set(<이름> "${CMAKE_…_DIR}")` — 값 전체가 내장 경로 하나(별칭).
_kPathAliasRe = re.compile(
    r'^\s*set\s*\(\s*([A-Za-z0-9_]+)\s+"?\$\{((?:CMAKE_(?:CURRENT_)?(?:SOURCE|BINARY|LIST)_DIR)|PROJECT_(?:SOURCE|BINARY)_DIR)\}"?\s*\)',
    re.IGNORECASE)

#: vcpkg 포트 툴체인 영역 — 손대지 않는 영역이라 별칭 · 가드 규칙을 들이대지 않는다(따로 도는 CMake 프로세스가 읽는다).
_kVcpkgToolchainPrefix = "cmake/Modules/Toolchain/Vcpkg/"

#: `if(COMMAND sw_…)` — 우리 함수의 정의 여부를 묻는 가드.
_kCommandGuardRe = re.compile(r'^\s*(?:else)?if\s*\(.*\bCOMMAND\s+sw_', re.IGNORECASE)

#: `sw_camelCase` — `sw_` 다음이 소문자로 시작하고 밑줄이 더 없다.
_kFunctionNameRe = re.compile(r'^sw_[a-z][a-zA-Z0-9]*$')

#: `SW_UPPER_SNAKE_CASE`
_kOptionNameRe = re.compile(r'^SW_[A-Z0-9_]+$')

#: 주석 줄.
_kCommentRe = re.compile(r'^\s*#')

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.
#:
#: `ThirdParty/` 를 통째로 빼지 않는다. 그 아래 `CMakeLists.txt` 는 **우리가 쓴 얇은 래퍼**이고
#: (`sw_copyDxcDlls` 같은 우리 함수가 거기 있다) 우리 규칙을 따라야 한다. 남의 코드는 vcpkg 가
#: 가져오는 포트 파일뿐이라 그것만 뺀다.
_kExcludedDirName = kNotOurDirNames | {"vcpkg-port"}

#: CMake 자신이 정한 이름들 — 우리 규칙을 들이댈 수 없다.
_kReservedVariablePrefix = ("CMAKE_", "CTEST_", "CPACK_", "ENV", "SW_", "VCPKG_", "Python3_", "_CMAKE_")


def checkCmakeFileInternal(path: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나를 줄 단위로 봅니다."""
    relPath = path.relative_to(repositoryRoot).as_posix()
    gate = CheckCmakeConventionsGate
    if relPath in gate.mapExemption:
        gate.seeExemption(relPath)
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

        if _kModuleLibraryRe.match(line) and relPath in gate.mapExemption:
            gate.useExemption(relPath)
        elif _kModuleLibraryRe.match(line):
            listViolation.append(f"{relPath}:{lineNumber} SHARED · MODULE 라이브러리는 sw_addModuleLibrary 로 만든다 — cmake/Engine/ModuleTargets.cmake")

        if _kCommandGuardRe.match(line) and not relPath.startswith(_kVcpkgToolchainPrefix):
            listViolation.append(f"{relPath}:{lineNumber} if(COMMAND sw_…) 가드 — 우리 함수는 include 순서로 늘 정의돼 있다, 가드 대신 정의 순서를 고친다")

        if not bInFunction and not relPath.startswith(_kVcpkgToolchainPrefix) and (aliasMatch := _kPathAliasRe.match(line)):
            listViolation.append(f"{relPath}:{lineNumber} '{aliasMatch.group(1)}' 는 ${{{aliasMatch.group(2)}}} 의 별칭 — 별칭 없이 그 내장 변수를 그대로 쓴다")

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
        listCandidate += [name for match in _kBareUseRe.finditer(line) for name in match.groups() if name]
        listCandidate += [match.group(1) for pattern in _kListLocalCreateRe if (match := pattern.match(line))]

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

    #: SHARED · MODULE `add_library` 를 직접 불러도 되는 파일 → 이유.
    mapExemption = {
        "cmake/Engine/ModuleTargets.cmake": "팩토리 sw_addModuleLibrary 자신",
        "Source/Engine/CMakeLists.txt": "Engine 자신 — Dev 에서는 DLL, Shipping 에서는 정적이라 팩토리가 부르는 쪽이 아니다",
    }

    description = "CMake 명명 규칙 검사 (AGENTS.md '### CMake')"
    buildComment = "Checking CMake naming conventions (AGENTS.md)..."
    timeoutSeconds = 20
    preCommitPattern = ("*.cmake", "*CMakeLists.txt")
    preCommitFileArgument = "--files"
    violationHeader = "CMake 명명 규칙 위반"
    hint = ("  AGENTS.md '### CMake': function/macro 는 sw_camelCase, option 은 SW_UPPER_SNAKE_CASE, 함수 내부 변수는 '_' 없는 camelCase,\n"
            "  모듈 라이브러리는 sw_addModuleLibrary, 내장 경로 별칭 · if(COMMAND sw_…) 가드 금지.")
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
        {
            # 역참조 없이 이름만 쓰이는 지역 변수 — 만드는 자리(file GLOB_RECURSE) · 쓰는 자리(IN LISTS)
            "name": "역참조 없는 '_' 지역 변수",
            "files": {
                "cmake/Probe/BareLocal.cmake":
                    "function(sw_probe)\n\tfile(GLOB_RECURSE _allHeaders \"*.h\")\n\tforeach(h IN LISTS _allHeaders)\n\tendforeach()\nendfunction()\n"
            },
        },
        {
            "name": "팩토리 밖 MODULE 라이브러리",
            "files": {"Source/Probe/CMakeLists.txt": "add_library(ProbeModule MODULE probe.cpp)\n"},
        },
        {
            "name": "내장 경로 별칭",
            "files": {"cmake/Probe/Alias.cmake": "set(sw_output_directory \"${CMAKE_BINARY_DIR}\")\n"},
        },
        {
            "name": "if(COMMAND sw_…) 가드",
            "files": {"cmake/Probe/Guard.cmake": "if(COMMAND sw_probe)\n\tsw_probe()\nendif()\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, suffixes=(".cmake",), fileNames=("CMakeLists.txt",),
                                          excludedDirNames=_kExcludedDirName)

        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(checkCmakeFileInternal(path, repositoryRoot))

        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} cmake files scanned")


main = CheckCmakeConventionsGate.run


if __name__ == "__main__":
    sys.exit(main())
