#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckCmakeReadme.py

`cmake/README.md` 가 **아직 사실인지** 검사합니다.

`cmake/README.md` 는 스스로를 정본이라고 말한다 (루트 `CMakeLists.txt` 머리 주석: "cmake/ 의 계층과
각 파일의 역할은 cmake/README.md 가 정본이다"). 그런데 그 문서를 검사하는 것이 없어서 **조용히
낡았다** — `sw_registerLintTests` 를 "`CheckEngineLayers` · `CheckIncludeOrder` · `CheckSourceGlob`
일괄 등록" 이라고 적어 두었는데, 그 목록은 이미 두 커밋 전에 사라졌다(폴더가 목록이 되었다).

`lint/` 폴더에는 린트가 살아 있는지 보는 `CheckLintsAreAlive.py` 가 있다. `cmake/` 에는 그런 것이
없었다. 이 게이트가 그 자리다.

검사 규칙 — **문서가 하는 말 중 기계가 확인할 수 있는 것만** 본다:

- 디렉터리 트리에 적힌 `*.cmake` · `*.h.in` 파일이 실제로 있는가
- "주요 헬퍼 함수" 표에 적힌 `sw_*` 가 실제로 정의되어 있는가
- 네이밍 표의 예시로 든 `sw_*` 가 실제로 정의되어 있는가

**반대 방향은 보지 않는다.** 표는 "주요" 헬퍼라고 말하지 전부라고 말하지 않는다 — 함수를 더할
때마다 문서를 고치라고 강요하면 그 규칙이 먼저 무시된다. 여기서 막는 것은 **문서가 없는 것을
가리키는 일**이다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 문서 안의 `` `sw_foo` `` — 백틱에 싸인 함수 이름.
_kQuotedFunctionRe = re.compile(r'`(sw_[a-zA-Z0-9_]+)`')

#: 트리 그림 속 파일 이름 (`BuildOptions.cmake` · `ConfigConstants.h.in`).
_kTreeFileRe = re.compile(r'^[\s│├└─]*([A-Za-z0-9_.-]+\.(?:cmake|h\.in))\b')

#: `function(sw_foo ...)` · `macro(sw_foo ...)` 정의.
_kDefinitionRe = re.compile(r'^\s*(?:function|macro)\s*\(\s*(sw_[a-zA-Z0-9_]+)', re.IGNORECASE | re.MULTILINE)

#: `add_library(sw_foo INTERFACE)` — 문서의 네이밍 표가 예로 드는 INTERFACE 타겟들이 이 모양이다.
_kTargetRe = re.compile(r'^\s*add_library\s*\(\s*(sw_[a-zA-Z0-9_]+)', re.IGNORECASE | re.MULTILINE)

#: `set(sw_foo ...)` · `list(APPEND sw_foo ...)` — 문서의 네이밍 표는 **변수**도 예로 든다
#: (`sw_flag_libraries` 는 타겟이 아니라 플래그 라이브러리 목록 변수다).
_kVariableRe = re.compile(
    r'^\s*(?:set|list)\s*\(\s*(?:APPEND\s+)?(sw_[a-zA-Z0-9_]+)',
    re.IGNORECASE | re.MULTILINE,
)

#: 이름이 아니라 **모양**을 적은 것 — 네이밍 표의 "규칙" 칸이다. 정의를 찾을 대상이 아니다.
_kNamingPattern = {"sw_camelCase", "sw_snake_case", "sw_PascalCase"}


def collectDefinedFunctionInternal(cmakeRoot: Path, repositoryRoot: Path) -> set[str]:
    """`cmake/` 와 프로젝트 `CMakeLists.txt` 들이 만드는 `sw_*` 이름 전부 — 함수·매크로·타겟·변수."""
    setDefined: set[str] = set()

    listPath = list(cmakeRoot.rglob("*.cmake"))
    listPath += [
        path for path in repositoryRoot.rglob("CMakeLists.txt")
        if "vcpkg" not in path.parts and "build" not in path.parts
    ]
    listPath += [path for path in repositoryRoot.rglob("*.cmake") if "ThirdParty" in path.parts]

    for path in listPath:
        content = path.read_text(encoding="utf-8", errors="ignore")
        setDefined.update(_kDefinitionRe.findall(content))
        setDefined.update(_kTargetRe.findall(content))
        setDefined.update(_kVariableRe.findall(content))

    return setDefined


def collectTreeFileInternal(readmeText: str) -> list[tuple[int, str]]:
    """트리 그림에 적힌 파일 이름과 그 줄 번호."""
    listEntry: list[tuple[int, str]] = []
    bInFence = False

    for lineNumber, line in enumerate(readmeText.splitlines(), start=1):
        if line.strip().startswith("```"):
            bInFence = not bInFence
            continue
        if not bInFence:
            continue
        if match := _kTreeFileRe.match(line):
            listEntry.append((lineNumber, match.group(1)))

    return listEntry


class CheckCmakeReadmeGate(LintGate):
    """`cmake/README.md` 가 없는 파일·없는 함수를 가리키고 있지 않은지."""

    description = "cmake/README.md 가 가리키는 파일·함수가 실재하는지 검사"
    buildComment = "Checking that cmake/README.md still describes reality..."
    timeoutSeconds = 20
    violationHeader = "cmake/README.md 가 낡았습니다"
    hint = "  문서가 없는 파일·함수를 가리키고 있습니다. cmake/README.md 를 코드에 맞추세요."
    selfTestCases = [
        {
            "name": "없는 함수를 가리키는 표",
            "files": {
                "cmake/README.md": "# cmake\n\n| 함수 | 용도 |\n|---|---|\n| `sw_thisNeverExisted` | 없는 것 |\n",
                "cmake/Engine/Probe.cmake": "function(sw_doProbe)\nendfunction()\n",
            },
        },
        {
            "name": "없는 파일을 가리키는 트리",
            "files": {
                "cmake/README.md": "# cmake\n\n```\ncmake/\n├── Probe/\n│   ├── Missing.cmake  — 없는 파일\n```\n",
                "cmake/Engine/Probe.cmake": "function(sw_doProbe)\nendfunction()\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="무시됩니다 (문서 하나를 봅니다)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        cmakeRoot = repositoryRoot / "cmake"
        readmePath = cmakeRoot / "README.md"
        if not readmePath.is_file():
            raise GateError(f"{readmePath} 가 없습니다 — 검사가 성립하지 않습니다")

        readmeText = readmePath.read_text(encoding="utf-8")
        listViolation: list[str] = []

        # 1) 트리에 적힌 파일이 실재하는가.
        listTreeEntry = collectTreeFileInternal(readmeText)
        setPresentName = {path.name for path in cmakeRoot.rglob("*") if path.is_file()}
        for lineNumber, fileName in listTreeEntry:
            if fileName not in setPresentName:
                listViolation.append(
                    f"cmake/README.md:{lineNumber} 트리에 적힌 '{fileName}' 가 cmake/ 에 없습니다"
                )

        # 2) 문서가 이름을 부른 `sw_*` 가 정의되어 있는가.
        setDefined = collectDefinedFunctionInternal(cmakeRoot, repositoryRoot)
        setMentioned = set()
        for lineNumber, line in enumerate(readmeText.splitlines(), start=1):
            for name in _kQuotedFunctionRe.findall(line):
                if name in _kNamingPattern or name in setDefined or name in setMentioned:
                    continue
                setMentioned.add(name)
                listViolation.append(
                    f"cmake/README.md:{lineNumber} '{name}' 는 어디에도 정의되어 있지 않습니다"
                )

        return GateResult(
            listViolation=listViolation,
            summary=f"{len(listTreeEntry)} tree entries, {len(setDefined)} sw_* functions",
        )


main = CheckCmakeReadmeGate.run


if __name__ == "__main__":
    sys.exit(main())
