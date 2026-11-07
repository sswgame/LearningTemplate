#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PlatformChains.py

플랫폼 `#if` 사슬의 모양을 본다.

**왜 린트인가.** 전처리 갈래는 형식으로 막을 수 없다. `#if Windows #else` 는 "Windows 가 아니면 POSIX" 를 가정하고, `#if Windows #elif Linux` 뒤에 `#else` 가 없으면 새 플랫폼에서 몸이 조용히
비어, 새 플랫폼이 컴파일은 되고 틀린 가지를 타며 찾는 길이 런타임 실패뿐이다(2026-11-07 조사 — 92 + 24 곳). 언리얼 `PLATFORM_*` 처럼 빠진 플랫폼은 컴파일 오류로 낸다.

규칙:
1. 첫 조건이 플랫폼 매크로뿐인 사슬에 갈래가 둘 이상이면 끝은 `#else` + `#error` 다. 몸이 하나뿐인 `#if`(한 플랫폼만의 include · 파일 전체 가드)는 괜찮다.
2. `!defined( SW_PLATFORM_* )` 를 쓰지 않는다. 부정은 새 플랫폼까지 삼킨다. Windows 가 아닌 곳은 `SW_PLATFORM_POSIX` 다.

첫 조건에 다른 매크로가 섞인 사슬(`SW_PLATFORM_LINUX && SW_WITH_CLIENT_CODE`, 내장 함수 고르기)은 기능 검사라 보지 않는다.
예외는 `Config/Dev/PlatformChains.json` 에 이유와 함께 적는다(데스크톱 전용 폴더 · 플랫폼 폴더 · 매크로 검사 파일).

`py -3 -m Scripts platform-chains [--self-check]` 로 돌리고, 커밋 훅(`PreCommit.py`)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kConfigPath = "Config/Dev/PlatformChains.json"
kExtensions = (".h", ".cpp", ".inl")
kDirective = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")
kPlatformOnly = re.compile(r"^!?defined\( SW_PLATFORM_\w+ \)(?: \|\| !?defined\( SW_PLATFORM_\w+ \))*$")
kNegated = re.compile(r"!\s*defined\(\s*SW_PLATFORM_\w+\s*\)")


def findProblemsInText(path: str, text: str) -> list[str]:
    """글 하나의 사슬 모양 위반을 돌려준다."""
    listProblem: list[str] = []
    lines = text.split("\n")
    stack: list[list[tuple[str, str, int]]] = []
    for lineNumber, line in enumerate(lines, 1):
        match = kDirective.match(line)
        if match is None:
            continue
        keyword = match.group(1)
        condition = match.group(2).split("//")[0].strip()
        if keyword in ("if", "ifdef", "ifndef", "elif") and kNegated.search(condition):
            listProblem.append(f"{path}:{lineNumber}: `!defined( SW_PLATFORM_* )` 는 새 플랫폼까지 삼킨다 — SW_PLATFORM_POSIX 나 플랫폼을 이름으로 적는다")
        if keyword in ("if", "ifdef", "ifndef"):
            stack.append([(keyword, condition, lineNumber)])
        elif keyword in ("elif", "else"):
            if stack:
                stack[-1].append((keyword, condition, lineNumber))
        elif stack:
            chain = stack.pop()
            if len(chain) < 2 or kPlatformOnly.match(chain[0][1]) is None:
                continue
            last = chain[-1]
            if last[0] != "else":
                listProblem.append(f"{path}:{chain[0][2]}: 플랫폼 사슬에 `#else` 가 없어 새 플랫폼에서 몸이 조용히 빈다 — `#else` + `#error` 로 끝낸다")
            elif not any(body.lstrip().startswith("#error") for body in lines[last[2]:lineNumber - 1]):
                listProblem.append(f"{path}:{last[2]}: 플랫폼 사슬의 `#else` 가 몸을 가진다(남은 플랫폼을 가정한다) — `#elif defined( SW_PLATFORM_POSIX )` 처럼 이름으로 적고 `#else` + `#error` 로 끝낸다")
    return listProblem


def isExempt(path: str, table: dict) -> bool:
    if any(path.startswith(prefix) for prefix in table["_exemptPaths"]):
        return True
    return any(part in table["_platformFolders"] for part in path.split("/")[:-1])


def findProblems(listSource: Iterable[tuple[str, str]], table: dict) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        if not isExempt(path, table):
            listProblem.extend(findProblemsInText(path, text))
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix in kExtensions and "graphify-out" not in path.parts:
            try:
                text = path.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            if "SW_PLATFORM_" in text:
                listSource.append((path.relative_to(projectRoot).as_posix(), text))
    return listSource


def checkPlatformChains(projectRoot: Path) -> list[str]:
    table = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return findProblems(readSources(projectRoot), table)


def runSelfCheck() -> None:
    table = {"_exemptPaths": {"Source/Editor/": "이유"}, "_platformFolders": ["Linux"]}
    kError = '#else\n    #error "Unsupported platform"\n#endif\n'
    clean = (
        "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#endif\n"
        + "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#elif defined( SW_PLATFORM_POSIX )\nint b;\n" + kError
        + "#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE )\nint a;\n#else\nint b;\n#endif\n"
        + "#if defined( SW_DEBUG )\nint a;\n#else\nint b;\n#endif\n"
    )
    assert findProblems([("Source/a.cpp", clean)], table) == [], "허용 모양은 통과해야 한다"
    assert len(findProblems([("Source/a.cpp", "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#else\nint b;\n#endif\n")], table)) == 1, "#else 몸"
    assert len(findProblems([("Source/a.cpp", "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#elif defined( SW_PLATFORM_LINUX )\nint b;\n#endif\n")], table)) == 1, "#else 없음"
    assert len(findProblems([("Source/a.cpp", "#if !defined( SW_PLATFORM_WINDOWS )\nint a;\n#endif\n")], table)) == 1, "부정"
    bad = "#if defined( SW_PLATFORM_WINDOWS )\nint a;\n#else\nint b;\n#endif\n"
    assert findProblems([("Source/Editor/a.cpp", bad), ("Source/Engine/Linux/a.cpp", bad)], table) == [], "예외 폴더"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="플랫폼 #if 사슬이 빠진 플랫폼을 컴파일 오류로 내는지 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[PlatformChains] self-check OK")
        return ExitCode.Passed
    listProblem = checkPlatformChains(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[PlatformChains] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
