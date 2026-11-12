#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FrameIntern.py

틱 · 갱신 · 칠하기 함수 안의 `hashed_string( "글자" )` 를 찾는다.

**왜 린트인가.** 리터럴에서 이름을 만드는 일은 형식이 막지 못한다(암시적 변환까지 허용되는 정상 코드다). 그런데 프레임마다 돌면
글자 → FNV → 샤드 뮤텍스를 지나고, 틱은 워커라 경합한다. 함수 안 `static const hashed_string` 이나 데이터 칸으로 옮기면 한 번이다.
형식 대안(자리마다 한 번 만드는 리터럴 — C++20 고정 문자열 템플릿 인자, 또는 C++17 의 Godot `SNAME` 같은 매크로 + 람다 정적)은 2026-11-12 검토에서 미뤘다:
표준이 C++17(`SW_CPP_STANDARD`)이고, 리터럴 암시적 생성자를 지우면 깨지는 암시적 호출 자리(명시 2572 곳 밖)는 컴파일러로만 찾을 수 있으며, 자리 정적은 이름 풀 재시작(`NamePoolSubsystem`) 뒤 옛 번호를 든다.
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
from lint.FunctionSpans import findFunctionSpans  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope, readTexts  # noqa: E402

kConfigPath = "Config/Rules/Lint/FrameInternExceptions.json"
kComment = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
kFrameFunctionName = re.compile(r"(?:\w+::)+(onTick|tick\w*|update\w*|paint\w*)")   # 프레임 함수 정의 이름(정의는 FunctionSpans 가 찾는다)
kLiteralName = re.compile(r"hashed_string\s*[({]\s*\"")


def findLiteralNames(text: str) -> list[tuple[str, str]]:
    """글 하나에서 (함수 이름, 줄) 을 돌려준다. `static` 줄은 한 번만 만들어지므로 뺀다."""
    code = kComment.sub("", text)
    listFound: list[tuple[str, str]] = []
    for span in findFunctionSpans(code):
        nameMatch = kFrameFunctionName.fullmatch(span.name)
        if nameMatch is None:
            continue
        for line in code[span.openIndex + 1:span.closeIndex].split("\n"):
            if kLiteralName.search(line) and "static" not in line:
                listFound.append((nameMatch.group(1), line.strip()))
    return listFound


def findProblems(listSource: Iterable[tuple[str, str]], exceptions: dict) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        for function, line in findLiteralNames(text):
            if f"{path}::{function}" not in exceptions:
                listProblem.append(f"{path}::{function}: 프레임 함수에서 리터럴로 이름을 만든다 — 함수 안 static const 로 옮긴다: {line}")
    return listProblem


def readSources(projectRoot: Path, scope: LintScope) -> list[tuple[str, str]]:
    return readTexts(projectRoot, scope.select(("Source",), (".cpp",), ("Tests",)))


def checkFrameIntern(projectRoot: Path, scope: LintScope) -> list[str]:
    exceptions = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))["_exceptions"]
    return findProblems(readSources(projectRoot, scope), exceptions)


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
    listProblem = checkFrameIntern(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[FrameIntern] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
