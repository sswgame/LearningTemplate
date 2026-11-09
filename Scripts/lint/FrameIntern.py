#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FrameIntern.py

틱 · 갱신 · 칠하기 함수 안의 `hashed_string( "글자" )` 를 찾는다.

**왜 린트인가.** 리터럴에서 이름을 만드는 일은 형식이 막지 못한다(암시적 변환까지 허용되는 정상 코드다). 그런데 프레임마다 돌면
글자 → FNV → 샤드 뮤텍스를 지나고, 틱은 워커라 경합한다. 함수 안 `static const hashed_string` 이나 데이터 칸으로 옮기면 한 번이다.
예외는 `Config/Rules/Lint/FrameInternExceptions.json` 에 `경로::함수` 와 까닭을 적는다.

`py -3 -m Scripts lint frame-intern [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
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

kConfigPath = "Config/Rules/Lint/FrameInternExceptions.json"
kComment = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
kDefinition = re.compile(r"^[ \t]*[\w:<>,&\*\s]*?\b\w+::(onTick|tick\w*|update\w*|paint\w*)\s*\([^;{]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{", re.M)
kLiteralName = re.compile(r"hashed_string\s*[({]\s*\"")


def findLiteralNames(text: str) -> list[tuple[str, str]]:
    """글 하나에서 (함수 이름, 줄) 을 돌려준다. `static` 줄은 한 번만 만들어지므로 뺀다."""
    code = kComment.sub("", text)
    listFound: list[tuple[str, str]] = []
    for match in kDefinition.finditer(code):
        depth = 1
        cursor = match.end()
        while cursor < len(code) and depth > 0:
            if code[cursor] == "{":
                depth += 1
            elif code[cursor] == "}":
                depth -= 1
            cursor += 1
        for line in code[match.end():cursor].split("\n"):
            if kLiteralName.search(line) and "static" not in line:
                listFound.append((match.group(1), line.strip()))
    return listFound


def findProblems(listSource: Iterable[tuple[str, str]], exceptions: dict) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        for function, line in findLiteralNames(text):
            if f"{path}::{function}" not in exceptions:
                listProblem.append(f"{path}::{function}: 프레임 함수에서 리터럴로 이름을 만든다 — 함수 안 static const 로 옮긴다: {line}")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*.cpp"):
        if "Tests" in path.parts:
            continue
        try:
            listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
        except (UnicodeDecodeError, OSError):
            continue
    return listSource


def checkFrameIntern(projectRoot: Path) -> list[str]:
    exceptions = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))["_exceptions"]
    return findProblems(readSources(projectRoot), exceptions)


def runSelfCheck() -> None:
    bad = 'void Foo::onTick( float32 deltaTime )\n{\n    use( hashed_string( "A" ) );\n}\n'
    assert len(findProblems([("a.cpp", bad)], {})) == 1, "틱 안의 리터럴은 잡아야 한다"
    assert findProblems([("a.cpp", bad)], {"a.cpp::onTick": "까닭"}) == [], "예외는 통과해야 한다"
    clean = 'void Foo::onTick( float32 deltaTime )\n{\n    static const hashed_string s_name( "A" );\n    use( s_name );\n}\nvoid Foo::load()\n{\n    use( hashed_string( "B" ) );\n}\n'
    assert findProblems([("a.cpp", clean)], {}) == [], "static 과 프레임 밖 함수는 통과해야 한다"
    comment = 'void Foo::updateView()\n{\n    // hashed_string( "A" )\n}\n'
    assert findProblems([("a.cpp", comment)], {}) == [], "주석은 통과해야 한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="프레임 함수 안의 리터럴 hashed_string 을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[FrameIntern] self-check OK")
        return ExitCode.Passed
    listProblem = checkFrameIntern(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[FrameIntern] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
