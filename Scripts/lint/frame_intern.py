#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/frame_intern.py

틱 · 갱신 · 칠하기 함수 안의 `hashed_string( "글자" )` 를 찾는다.

**왜 린트인가.** 리터럴에서 이름을 만드는 일은 형식이 막지 못한다(암시적 변환까지 허용되는 정상 코드다). 그런데 프레임마다 돌면
글자 → FNV → 샤드 뮤텍스를 지나고, 틱은 워커라 경합한다. 함수 안 `static const hashed_string` 이나 데이터 칸으로 옮기면 한 번이다.
형식 대안(자리마다 한 번 만드는 리터럴 — C++20 고정 문자열 템플릿 인자, 또는 C++17 의 Godot `SNAME` 같은 매크로 + 람다 정적)은 2026-11-12 검토에서 미뤘다:
표준이 C++17(`SW_CPP_STANDARD`)이고, 리터럴 암시적 생성자를 지우면 깨지는 암시적 호출 자리(명시 2572 곳 밖)는 컴파일러로만 찾을 수 있으며, 자리 정적은 이름 풀 재시작(`NamePoolSubsystem`) 뒤 옛 번호를 든다.
예외는 `Config/Rules/Lint/FrameInternExceptions.json` 에 `경로::함수` 와 까닭을 적는다.

`py -3 -m Scripts lint frame-intern [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.function_spans import find_function_spans  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_texts  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/FrameInternExceptions.json"
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
FRAME_FUNCTION_NAME = re.compile(r"(?:\w+::)+(onTick|tick\w*|update\w*|paint\w*)")   # 프레임 함수 정의 이름(정의는 FunctionSpans 가 찾는다)
LITERAL_NAME = re.compile(r"hashed_string\s*[({]\s*\"")


def find_literal_names(text: str) -> list[tuple[str, str]]:
    """글 하나에서 (함수 이름, 줄) 을 돌려준다. `static` 줄은 한 번만 만들어지므로 뺀다."""
    code = COMMENT.sub("", text)
    list_found: list[tuple[str, str]] = []
    for span in find_function_spans(code):
        name_match = FRAME_FUNCTION_NAME.fullmatch(span.name)
        if name_match is None:
            continue
        for line in code[span.open_index + 1:span.close_index].split("\n"):
            if LITERAL_NAME.search(line) and "static" not in line:
                list_found.append((name_match.group(1), line.strip()))
    return list_found


def find_problems(list_source: Iterable[tuple[str, str]], exceptions: dict) -> list[str]:
    list_problem: list[str] = []
    for path, text in list_source:
        for function, line in find_literal_names(text):
            if f"{path}::{function}" not in exceptions:
                list_problem.append(f"{path}::{function}: 프레임 함수에서 리터럴로 이름을 만든다 — 함수 안 static const 로 옮긴다: {line}")
    return list_problem


def read_sources(project_root: Path, scope: LintScope) -> list[tuple[str, str]]:
    return read_texts(project_root, scope.select(("Source",), (".cpp",), ("Tests",)))


def check_frame_intern(project_root: Path, scope: LintScope) -> list[str]:
    exceptions = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))["_exceptions"]
    return find_problems(read_sources(project_root, scope), exceptions)


def run_self_check() -> None:
    bad = 'void Foo::onTick( float32 deltaTime )\n{\n    use( hashed_string( "A" ) );\n}\n'
    assert len(find_problems([("a.cpp", bad)], {})) == 1, "틱 안의 리터럴은 잡아야 한다"
    assert find_problems([("a.cpp", bad)], {"a.cpp::onTick": "까닭"}) == [], "예외는 통과해야 한다"
    clean = 'void Foo::onTick( float32 deltaTime )\n{\n    static const hashed_string s_name( "A" );\n    use( s_name );\n}\nvoid Foo::load()\n{\n    use( hashed_string( "B" ) );\n}\n'
    assert find_problems([("a.cpp", clean)], {}) == [], "static 과 프레임 밖 함수는 통과해야 한다"
    comment = 'void Foo::updateView()\n{\n    // hashed_string( "A" )\n}\n'
    assert find_problems([("a.cpp", comment)], {}) == [], "주석은 통과해야 한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="프레임 함수 안의 리터럴 hashed_string 을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[FrameIntern] self-check OK")
        return ExitCode.PASSED
    list_problem = check_frame_intern(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[FrameIntern] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
