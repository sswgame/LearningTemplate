#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/shader_branch_shape.py

`if ( a && b ) { … } else if ( a ) { … }` 처럼 앞 갈래의 `&&` 조건과 같은 항을 뒤 `else if` 가 다시 묻는 사슬을 막는다.
NVIDIA GL(ARB_gl_spirv)은 DXC 가 낸 이 모양의 SPIR-V 를 잘못 컴파일해 a 가 거짓인 점도 a 쪽 갈래를 탔다 — 캐스케이드 상자 밖 바닥이
어두웠고 같은 SPIR-V 의 Vulkan 은 맞았다(커밋 46c0d6ad17). 고치는 모양은 겹친 분기다: `if ( a ) { if ( b ) { … } else { … } }`.
**왜 형식이 아니라 린트인가.** HLSL 에는 분기 모양을 형식으로 막을 자리가 없고, 문제는 드라이버가 SPIR-V 구조를 받는 방식이다.

- 사슬: `if ( … )` 뒤에 `else if ( … )` 가 이어진 갈래들. 조건은 맨 바깥 `&&` 로 나눈 항(공백 · 바깥 괄호를 뺀 글)으로 본다.
- 위반: 뒤 갈래의 항 하나가 앞 갈래 중 항이 둘 이상인 조건의 항과 같다. 전처리 줄은 지운다(보수적).

`py -3 -m Scripts lint shader-branch-shape [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_texts  # noqa: E402
from lint.shader_kill_order import COMMENT_PATTERN, EXTENSIONS, PREPROCESSOR_PATTERN  # noqa: E402

IF_PATTERN = re.compile(r"\bif\s*\(")
ELSE_PATTERN = re.compile(r"\s*else\b")
ELSE_IF_PATTERN = re.compile(r"\s*else\s+(?:SW_\w+\s+)?if\s*\(")


def find_closing(code: str, open_index: int, opener: str, closer: str) -> int:
    depth = 0
    for index in range(open_index, len(code)):
        if code[index] == opener:
            depth += 1
        elif code[index] == closer:
            depth -= 1
            if depth == 0:
                return index
    return len(code)


def skip_statement(code: str, index: int) -> int:
    """@p index 에서 시작하는 문장(블록 · 한 줄 · 겹친 if 사슬)의 끝 다음 자리를 돌려준다."""
    while index < len(code) and code[index].isspace():
        index += 1
    if index < len(code) and code[index] == "{":
        return find_closing(code, index, "{", "}") + 1
    match = re.compile(r"(?:SW_\w+\s+)?if\s*\(").match(code, index)
    if match:
        index = skip_statement(code, find_closing(code, match.end() - 1, "(", ")") + 1)
        if ELSE_PATTERN.match(code, index):
            return skip_statement(code, ELSE_PATTERN.match(code, index).end())
        return index
    depth = 0
    while index < len(code):
        if code[index] in "([":
            depth += 1
        elif code[index] in ")]":
            depth -= 1
        elif code[index] == ";" and depth == 0:
            return index + 1
        index += 1
    return index


def strip_outer(term: str) -> str:
    term = re.sub(r"\s+", "", term)
    while term.startswith("(") and find_closing(term, 0, "(", ")") == len(term) - 1:
        term = term[1:-1]
    return term


def split_conjuncts(condition: str) -> list[str]:
    condition = strip_outer(condition)
    list_term: list[str] = []
    depth = 0
    start = 0
    for index, char in enumerate(condition):
        if char in "([":
            depth += 1
        elif char in ")]":
            depth -= 1
        elif depth == 0 and condition.startswith("&&", index):
            list_term.append(condition[start:index])
            start = index + 2
    list_term.append(condition[start:])
    if any("||" in term and strip_outer(term) == term for term in list_term):
        return [condition]  # 맨 바깥에 || 가 있으면 && 항으로 나누지 않는다
    return [strip_outer(term) for term in list_term]


def find_problems_in_text(path: str, text: str) -> list[str]:
    code = PREPROCESSOR_PATTERN.sub("", COMMENT_PATTERN.sub("", text))
    list_problem: list[str] = []
    set_else_if_start: set[int] = set()
    for match in IF_PATTERN.finditer(code):
        if match.start() in set_else_if_start:
            continue
        list_condition: list[list[str]] = []
        open_index = match.end() - 1
        while True:
            close_index = find_closing(code, open_index, "(", ")")
            list_term = split_conjuncts(code[open_index + 1:close_index])
            shared = next((term for earlier in list_condition if len(earlier) > 1 for term in list_term if term in earlier), None)
            if shared is not None:
                line = code.count("\n", 0, open_index) + 1
                list_problem.append(f"{path}:{line}: else if 가 앞 갈래의 && 항 `{shared}` 를 다시 묻는다 — NVIDIA GL(ARB_gl_spirv)이 이 모양을 잘못 컴파일한다(46c0d6ad17). "
                                   f"`if ( {shared} ) {{ if ( 나머지 ) … else … }}` 로 겹친다")
            list_condition.append(list_term)
            next_match = ELSE_IF_PATTERN.match(code, skip_statement(code, close_index + 1))
            if not next_match:
                break
            if_start = IF_PATTERN.search(code, next_match.start())
            set_else_if_start.add(if_start.start())
            open_index = next_match.end() - 1
    return list_problem


def find_problems(list_source: Iterable[tuple[str, str]]) -> list[str]:
    list_problem: list[str] = []
    for path, text in list_source:
        list_problem.extend(find_problems_in_text(path, text))
    return list_problem


def check_shader_branch_shape(project_root: Path, scope: LintScope) -> list[str]:
    return find_problems(read_texts(project_root, scope.select(("Content", "Source"), EXTENSIONS, ("ThirdParty",))))


def run_self_check() -> None:
    bad = "float f( bool a, bool b )\n{\n    float x = 0;\n    if ( a && b )\n    {\n        x = 1;\n    }\n    else if ( a )\n        x = 2;\n    return x;\n}\n"
    bad_branch = "void f() { SW_BRANCH if ( ( inside ) && usesSoft() ) { x = 1; } else if ( c ) { } else SW_BRANCH if ( inside ) { x = 2; } }"
    bad_nested = "void f() { if ( a && b ) { if ( c ) { x = 1; } else { x = 3; } } else if ( b && d ) { x = 2; } }"
    good = "void f() { SW_BRANCH if ( a ) { SW_BRANCH if ( b ) { x = 1; } else { x = 2; } } }"
    good_or = "void f() { if ( a || b && c ) { x = 1; } else if ( a ) { x = 2; } }"
    good_separate = "void f() { if ( a && b ) { x = 1; } if ( a ) { x = 2; } }"
    good_single = "void f() { if ( a ) { x = 1; } else if ( a && b ) { x = 2; } }"
    assert len(find_problems([("a.hlsl", bad)])) == 1, find_problems([("a.hlsl", bad)])
    assert ":8:" in find_problems([("a.hlsl", bad)])[0]
    assert len(find_problems([("a.hlsl", bad_branch)])) == 1, find_problems([("a.hlsl", bad_branch)])
    assert len(find_problems([("a.hlsl", bad_nested)])) == 1, find_problems([("a.hlsl", bad_nested)])
    for text in (good, good_or, good_separate, good_single, "// if ( a && b ) {} else if ( a ) {}\n"):
        assert find_problems([("a.hlsl", text)]) == [], (text, find_problems([("a.hlsl", text)]))


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="`if ( a && b ) … else if ( a )` 분기 사슬을 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[ShaderBranchShape] self-check OK")
        return ExitCode.PASSED
    list_problem = check_shader_branch_shape(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[ShaderBranchShape] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
