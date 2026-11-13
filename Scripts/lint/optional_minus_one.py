#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/optional_minus_one.py

`optional<정수>` · `expected<정수>` 자리에 리터럴 `-1` 을 **값** 으로 넣는 곳을 찾는다 — 반환 · 대입 · 초기화 · 기본 인자 · 인자.

**왜 린트인가.** 표지 상수(`constant::invalid_index::k*`)는 따로 된 형식이라 `Expected.h` 가 그 변환을 지워 컴파일에서 막는다. 리터럴 `-1` 은 정수라
`optional<int32>` 의 값으로 합법이다 — 형식이 "없음" 을 뜻한 -1 과 값 -1 을 가르지 못한다(`SkillTreeState::findIndex` 가 -1 을 값으로 돌려줘 `_list[-1]`).
없음은 `nullopt`(optional) · `unexpected(...)`(expected) 다. 값 -1 이 정말 맞는 자리(부호 있는 축의 값)는 이름 단 상수로 적는다.

찾는 범위(글 연산의 한계): 선언은 같은 파일과 짝 파일(같은 이름의 `.h` · `.cpp`)에서 본다 — 파일마다의 선언 · 후보 자리는 내용 해시 색인(`FactCache`)에 둔다. 다른 파일에 선언된 함수의 인자 · 칸은 보지 못한다.
같은 이름이 짝 안에서 다른 형식으로도 선언됐으면 어느 것인지 몰라 넘긴다.

`py -3 -m Scripts lint optional-minus-one [--self-check]`, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import load_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402
from lint.cpp_text import blank_comments_and_strings  # noqa: E402

INT = r"(?:const\s+)?(?:sw::)?(?:u?int(?:8|16|32|64)|size_t|usize|isize|int)"
OPTIONAL_TYPE = r"(?:sw::)?(?:optional|expected)\s*<\s*" + INT + r"\s*(?:,[^<>;]*(?:<[^<>;]*>)?[^<>;]*)?>"
MINUS_ONE = r"(?:-\s*1(?![\w.])|static_cast\s*<[^>]*>\s*\(\s*-\s*1\s*\))"
ANY_MINUS_ONE = re.compile(MINUS_ONE)
INIT_OR_DEFAULT = re.compile(OPTIONAL_TYPE + r"\s*&?\s*\w+\s*(?:=|\{|\()\s*" + MINUS_ONE)
OPTIONAL_NAME = re.compile(OPTIONAL_TYPE + r"\s*&?\s*(\w+)\s*(?=[;={(,)\[]|$)", re.M)
ASSIGN = re.compile(r"(?<![\w>:])(?:[\w\]\)]+\s*(?:\.|->)\s*)?(\w+)\s*(?:=|\{)\s*" + MINUS_ONE)
FUNCTION_HEAD = re.compile(r"(?:^|[\s;{}])" + OPTIONAL_TYPE + r"\s+(?:[\w:~]+::)?(\w+)\s*\(", re.M)
RETURN_MINUS_ONE = re.compile(r"\breturn\s+" + MINUS_ONE + r"\s*;")
LAMBDA_HEAD = re.compile(r"\]\s*(?:\([^()]*(?:\([^()]*\)[^()]*)*\))?\s*(?:mutable\s*)?(?:->\s*[^{;]+)?\{")
SIGNATURE = re.compile(r"\b(\w+)\s*\(([^;{}()]*(?:optional|expected)\s*<[^;{}()]*)\)\s*(?:const\s*)?(?:override\s*)?[;{=]", re.S)
OTHER_TYPE = r"(?:" + INT + r"|float32|float64|bool|auto|dbus_int32_t|long|short)"


def find_close(code: str, open_index: int, open_char: str, close_char: str) -> int:
    """`code[openIndex]` 가 여는 괄호일 때 짝 닫는 괄호의 위치(없으면 글 끝)."""
    depth = 0
    for cursor in range(open_index, len(code)):
        if code[cursor] == open_char:
            depth += 1
        elif code[cursor] == close_char:
            depth -= 1
            if depth == 0:
                return cursor
    return len(code)


def split_top_level(text: str) -> list[str]:
    """최상위 쉼표로 가른다(`()` · `[]` · `{}` · `<>` 안의 쉼표는 두고)."""
    list_part: list[str] = []
    depth = 0
    begin = 0
    for cursor, char in enumerate(text):
        if char in "([{<":
            depth += 1
        elif char in ")]}>":
            depth = max(0, depth - 1)
        elif char == "," and depth == 0:
            list_part.append(text[begin:cursor])
            begin = cursor + 1
    list_part.append(text[begin:])
    return [part.strip() for part in list_part if part.strip()]


def get_line_of(code: str, index: int) -> int:
    return code.count("\n", 0, index) + 1


def blank_lambda_bodies(body: str) -> str:
    """함수 본문 안 람다 본문을 빈칸으로 — 람다의 `return -1` 은 람다의 반환 형식이다."""
    chars = list(body)
    for match in LAMBDA_HEAD.finditer(body):
        start = match.end() - 1
        end = find_close(body, start, "{", "}")
        for cursor in range(start + 1, min(end, len(chars))):
            if chars[cursor] != "\n":
                chars[cursor] = " "
    return "".join(chars)


def collect_declarations(code: str) -> tuple[set[str], dict[str, set[int]]]:
    """(optional 로 선언한 이름, 함수 이름 → optional 매개변수 위치)."""
    set_optional = {match.group(1) for match in OPTIONAL_NAME.finditer(code)}
    map_param: dict[str, set[int]] = {}
    for match in SIGNATURE.finditer(code):
        for index, param in enumerate(split_top_level(match.group(2))):
            if re.match(r"(?:const\s+)?" + OPTIONAL_TYPE + r"\s*&?\s*\w+", param):
                map_param.setdefault(match.group(1), set()).add(index)
    return set_optional, map_param


OTHER_DECLARATION = re.compile(r"(?=\b" + OTHER_TYPE + r"\s*&?\s*(\w+)\s*[;={(,)\[])")
CALL_HEAD = re.compile(r"\b(\w+)\s*\(")


def extract_facts(text: str) -> dict:
    """
    파일 하나의 사실 — 짝(같은 이름의 .h · .cpp · .inl)의 판정은 이 사실의 합으로 낸다(`judge_stem`). 스테이지 파일 하나에 짝 파일을 다시 읽지 않게
    내용 해시 색인(`FactCache`)에 둔다. 선언(optional 이름 · 매개변수 자리 · 다른 형식으로 선언된 이름)과, 짝의 선언에 따라 위반이 되는 자리 후보를 담는다.
    """
    code = blank_comments_and_strings(text)
    set_optional, map_param = collect_declarations(code)
    list_init: list[int] = []
    list_assign: list[list] = []
    for line_index, line in enumerate(code.split("\n")):
        if ANY_MINUS_ONE.search(line) is None:
            continue
        if INIT_OR_DEFAULT.search(line):
            list_init.append(line_index + 1)
        list_assign.extend([line_index + 1, match.group(1)] for match in ASSIGN.finditer(line))
    list_return: list[list] = []
    if RETURN_MINUS_ONE.search(code):
        for match in FUNCTION_HEAD.finditer(code):
            close_paren = find_close(code, match.end() - 1, "(", ")")
            after = re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?\{", code[close_paren + 1:close_paren + 200])
            if after is None:
                continue
            body_start = close_paren + 1 + after.end()
            body_end = find_close(code, body_start - 1, "{", "}")
            body = blank_lambda_bodies(code[body_start:body_end])
            list_return.extend([get_line_of(code, body_start + found.start()), match.group(1)] for found in RETURN_MINUS_ONE.finditer(body))
    list_call: list[list] = []
    if ANY_MINUS_ONE.search(code):
        for match in CALL_HEAD.finditer(code):
            open_index = match.end() - 1
            list_argument = split_top_level(code[open_index + 1:find_close(code, open_index, "(", ")")])
            list_index = [index for index, argument in enumerate(list_argument) if re.fullmatch(MINUS_ONE, argument)]
            if list_index:
                list_call.append([get_line_of(code, match.start()), match.group(1), list_index])
    return {"minus": ANY_MINUS_ONE.search(text) is not None, "optional": "optional" in text or "expected" in text,
            "declared": sorted(set_optional), "params": {name: sorted(set_index) for name, set_index in map_param.items()},
            "other": sorted({match.group(1) for match in OTHER_DECLARATION.finditer(code)}),
            "init": list_init, "assign": list_assign, "return": list_return, "call": list_call}


def judge_stem(map_fact: dict[str, dict]) -> list[str]:
    """짝 하나(경로 → 사실)의 위반 — 선언은 짝 전체에서 모은다."""
    if not any(fact["minus"] for fact in map_fact.values()) or not any(fact["optional"] for fact in map_fact.values()):
        return []   # -1 이 없거나 optional · expected 가 없는 짝은 볼 것이 없다
    set_optional = {name for fact in map_fact.values() for name in fact["declared"]}
    set_other = {name for fact in map_fact.values() for name in fact["other"]}
    map_param: dict[str, set[int]] = {}
    for fact in map_fact.values():
        for name, list_index in fact["params"].items():
            map_param.setdefault(name, set()).update(list_index)
    list_problem: list[str] = []
    for path, fact in map_fact.items():
        map_line: dict[int, list[str]] = {}
        for line in fact["init"]:
            map_line.setdefault(line, []).append(f"{path}:{line}: optional · expected 의 초기값 · 기본 인자가 -1 이다 — 없음은 nullopt · unexpected")
        for line, name in fact["assign"]:
            if name in set_optional and name not in set_other:
                map_line.setdefault(line, []).append(f"{path}:{line}: optional · expected 칸 '{name}' 에 -1 을 값으로 넣는다 — 없음은 nullopt(reset) · unexpected")
        list_problem += [message for line in sorted(map_line) for message in map_line[line]]
        list_problem += [f"{path}:{line}: optional · expected 를 돌려주는 {name} 이 -1 을 값으로 돌려준다 — 없음은 nullopt · unexpected" for line, name in fact["return"]]
        for line, name, list_index in fact["call"]:
            list_problem += [f"{path}:{line}: {name} 의 optional · expected 인자({index + 1} 번째)에 -1 을 넘긴다 — 없음은 nullopt"
                            for index in sorted(map_param.get(name, ())) if index in list_index]
    return list_problem


def judge_facts(map_fact: dict[str, dict]) -> list[str]:
    """경로 → 사실. 짝(확장자를 뺀 같은 경로)마다 판정하고 한 줄에 한 번만 낸다(초기화가 대입으로도 보인다)."""
    map_stem: dict[str, dict[str, dict]] = {}
    for path, fact in map_fact.items():
        map_stem.setdefault(path.rsplit(".", 1)[0], {})[path] = fact
    map_by_line: dict[str, str] = {}
    for map_pair in map_stem.values():
        for problem in judge_stem(map_pair):
            map_by_line.setdefault(problem.split(": ", 1)[0], problem)
    return sorted(map_by_line.values())


def find_problems(map_source: dict[str, str]) -> list[str]:
    """경로 → 글 — 자체 검사의 입구."""
    return judge_facts({path: extract_facts(text) for path, text in map_source.items()})


def check_optional_minus_one(project_root: Path, scope: LintScope) -> list[str]:
    """scope 의 파일과 짝(같은 이름의 .h · .cpp · .inl) — 짝 파일의 사실은 색인에서 꺼낸다."""
    suffixes = (".cpp", ".h", ".inl")
    set_stem = {path.rsplit(".", 1)[0] for path in scope.select(("Source",), suffixes)}
    if not set_stem:
        return []
    list_path = [path for path in list_repo_files(project_root) if path.startswith("Source/") and path.endswith(suffixes)]
    map_fact = load_facts(project_root, "optional-minus-one", [__file__], list_path, lambda _, text: extract_facts(text),
                        want=[path for path in list_path if path.rsplit(".", 1)[0] in set_stem], shard_of=lambda path: path.rsplit(".", 1)[0])   # 한 짝은 한 조각
    return judge_facts(map_fact)


def run_self_check() -> None:
    def problems(text: str, header: str = "") -> list[str]:
        map_source = {"a.cpp": text}
        if header:
            map_source["a.h"] = header
        return find_problems(map_source)

    assert len(problems("optional<int32> f() { if ( x ) return 3; return -1; }\n")) == 1, "반환"
    assert len(problems("expected<int32> A::f( int32 a )\n{\n    return -1;\n}\n")) == 1, "expected 반환 · 한정 이름"
    assert problems("optional<int32> f() { auto g = []() { return -1; }; return nullopt; }\n") == [], "람다의 반환은 람다 것이다"
    assert problems("int32 f() { return -1; }\n") == [], "정수 반환은 통과한다"
    assert len(problems("void f( optional<int32> slot = -1 );\n")) == 1, "기본 인자"
    assert len(problems("void g() { h( 1, -1 ); }\n", "void h( int32 a, optional<int32> b );\n")) == 1, "짝 헤더의 매개변수 위치"
    assert problems("void g() { h( -1, nullopt ); }\n", "void h( int32 a, optional<int32> b );\n") == [], "정수 자리의 -1 · nullopt 는 통과한다"
    assert len(problems("void g() { _slot = -1; }\n", "struct S { optional<uint32> _slot{}; };\n")) == 1, "칸 대입"
    assert problems("void g() { _slot = -1; }\n", "struct S { optional<uint32> _slot{}; };\nstruct T { int32 _slot; };\n") == [], "같은 이름이 정수로도 있으면 넘긴다"
    assert problems("optional<int32> f() { return -1.0f; }\n") == [], "실수는 다른 자리다"
    assert problems("// optional<int32> f() { return -1; }\n") == [], "주석은 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="optional · expected 자리에 리터럴 -1 을 값으로 넣는 곳을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[OptionalMinusOne] self-check OK")
        return ExitCode.PASSED
    list_problem = check_optional_minus_one(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[OptionalMinusOne] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
