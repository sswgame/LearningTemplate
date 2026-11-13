#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/required_name_fields.py

데이터에 있어야 할 이름 · 경로 칸이 C++ 기본값으로 숨는 모양을 막는다.

`PROPERTY( … )` 칸의 형식이 이름 · 경로 계열(형식 목록은 `Config/Rules/Lint/RequiredNameFieldExceptions.json` 의 `_types`)이고, 헤더 초기값이나
같은 타입 생성자(같은 폴더 `.cpp` · `.inl`)의 초기화 목록에 비어 있지 않은 문자열 리터럴이 있으면서 주석에 `Required` · `Transient` 가 없으면 오류다.
예: Shooter3D 아바타의 발 뼈 `_leftFootBone{ "foot.l" }` 은 프리팹이 값을 적지 않아 실제 스켈레톤(`foot_l`)에 없는 이름으로 측정이 늘 0 이었다.

고치는 길: 데이터마다 다른 값이면 C++ 기본값을 지우고 `Required` 를 달아 데이터 파일에 적는다(빠지면 `Serialization.RequiredMissing`).
정말 엔진 상수(데이터가 아닌 값)이면 예외 표에 `경로::타입::칸` 과 까닭을 적는다. 표에서 사라진 칸의 항목도 오류다(표가 썩지 않게).

**왜 린트인가.** 리터럴 초기값은 합법 C++ 이고, 형식으로 막으려면 이름 칸마다 리터럴을 받지 않는 새 형식을 리플렉션 생성기 · 직렬화기에 들여야 한다
(생성기는 NEXT 5-4 W-R3 설계와 겹친다). 초기값이 "형식일 뿐인 빈 값"인지 "데이터가 덮을 기본값"인지는 형식이 말하지 못한다.

`py -3 -m Scripts lint required-name-fields [--all] [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
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
from lint.index_cache import load_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/RequiredNameFieldExceptions.json"
PROPERTY_HEAD = re.compile(r"^[ \t]*PROPERTY\s*\(", re.M)
CLASS_HEAD = re.compile(r"\b(?:class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)?(\w+)\s*(?:final\s*)?(?::[^{;]*)?$")
OPT_OUT = re.compile(r"\b(?:Required|required|Transient|transient|NonSerialized|nonSerialized)\b(?!\s*=\s*false)")
LITERAL = re.compile(r"\"[^\"\n]+\"")
TYPE_NOISE = re.compile(r"\b(?:const|std::|sw::|mutable)\s*|\s+")


def blank_code(text: str) -> tuple[str, str]:
    """(주석만 지운 글, 주석 · 문자열을 지운 글) — 둘 다 같은 길이라 자리를 함께 쓴다."""
    list_kept: list[str] = []
    list_code: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i) or text.startswith("/*", i):
            end = text.find("\n", i) if text[i + 1] == "/" else text.find("*/", i) + 2
            end = n if end <= i else end
            blank = re.sub(r"[^\n]", " ", text[i:end])
            list_kept.append(blank)
            list_code.append(blank)
            i = end
        elif text[i] in "\"'":
            end = i + 1
            while end < n and text[end] != text[i] and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, n)
            list_kept.append(text[i:end])
            list_code.append(text[i] + " " * (end - i - 2) + text[end - 1] if end - i >= 2 else text[i:end])
            i = end
        else:
            list_kept.append(text[i])
            list_code.append(text[i])
            i += 1
    return "".join(list_kept), "".join(list_code)


def skip_group(code: str, start: int) -> int:
    """`code[start]` 이 여는 괄호일 때 짝 닫는 괄호 다음 자리."""
    depth = 0
    for i in range(start, len(code)):
        if code[i] in "({[":
            depth += 1
        elif code[i] in ")}]":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(code)


def find_class_at(code: str, list_offset: list[int]) -> dict[int, str]:
    """자리마다 그 자리를 감싼 가장 안쪽 class · struct 이름."""
    result: dict[int, str] = {}
    stack: list[str | None] = []
    boundary = 0
    pending = sorted(list_offset)
    cursor = 0
    for i, c in enumerate(code):
        while cursor < len(pending) and pending[cursor] <= i:
            result[pending[cursor]] = next((name for name in reversed(stack) if name), "")
            cursor += 1
        if c == "{":
            match = CLASS_HEAD.search(code[boundary:i].strip())
            stack.append(match.group(1) if match else None)
            boundary = i + 1
        elif c == "}":
            if stack:
                stack.pop()
            boundary = i + 1
        elif c == ";":
            boundary = i + 1
    return result


def find_fields(kept: str, code: str, set_type: set[str], tuple_prefix: tuple[str, ...]) -> list[tuple[str, str, str, int]]:
    """헤더의 이름 계열 PROPERTY 칸 (타입, 칸, 헤더 초기값, 줄) — Required · Transient 는 뺀다."""
    list_field: list[tuple[int, str, str, int]] = []
    for match in PROPERTY_HEAD.finditer(code):
        args_end = skip_group(code, match.end() - 1)
        if OPT_OUT.search(code[match.end():args_end]):
            continue
        decl_end = code.find(";", args_end)
        decl = code[args_end:decl_end]
        head = re.match(r"\s*([^{=;]*?)\s*\b(\w+)\s*(?=[{=]|$)", decl)
        if not head or "(" in head.group(1):
            continue
        type_name = TYPE_NOISE.sub("", head.group(1))
        if type_name not in set_type and not type_name.startswith(tuple_prefix):
            continue
        initial = kept[args_end + head.end():decl_end]
        list_field.append((match.start(), head.group(2), initial, code.count("\n", 0, match.start()) + 1))
    map_class = find_class_at(code, [offset for offset, _, _, _ in list_field])
    return [(map_class[offset], name, initial, line) for offset, name, initial, line in list_field]


def find_constructor_initials(kept: str, code: str, class_name: str) -> dict[str, list[str]]:
    """`.cpp` 글 하나에서 `Class::Class( … ) : _a{ … }, _b( … )` 의 칸마다 초기값 글."""
    result: dict[str, list[str]] = {}
    for match in re.finditer(rf"\b{class_name}::{class_name}\s*\(", code):
        cursor = skip_group(code, match.end() - 1)
        tail = re.match(r"\s*(?:noexcept\s*)?:", code[cursor:])
        if not tail:
            continue
        cursor += tail.end()
        while True:
            member = re.match(r"\s*([\w:]+)\s*(?=[({])", code[cursor:])
            if not member:
                break
            group_start = cursor + member.end()
            group_end = skip_group(code, group_start)
            result.setdefault(member.group(1), []).append(kept[group_start:group_end])
            comma = re.match(r"\s*,", code[group_end:])
            if not comma:
                break
            cursor = group_end + comma.end()
    return result


CONSTRUCTOR_HEAD = re.compile(r"\b(\w+)::\1\s*\(")


def is_target(path: str) -> bool:
    return path.startswith("Source/") and path.endswith((".h", ".cpp", ".inl")) and "Tests" not in path.split("/") and not path.endswith(".generated.h")


def extract_facts(path: str, text: str, config: dict) -> dict:
    """파일 하나의 사실 — 헤더는 이름 계열 PROPERTY 칸(타입, 칸, 헤더 초기값, 줄), 본문(`.cpp` · `.inl`)은 생성자마다 칸 → 초기값 글."""
    kept, code = blank_code(text)
    if path.endswith(".h"):
        property = "PROPERTY" in text
        return {"fields": [list(field) for field in find_fields(kept, code, set(config["_types"]), tuple(config["_typePrefixes"]))] if property else []}
    map_constructor = {class_name: find_constructor_initials(kept, code, class_name) for class_name in dict.fromkeys(CONSTRUCTOR_HEAD.findall(code))}
    return {"constructors": {class_name: initials for class_name, initials in map_constructor.items() if initials}}


def evaluate_facts(map_fact: dict[str, dict], config: dict, whole_repo: bool = True) -> list[str]:
    """map_fact: 경로 → `extract_facts`(경로 순). 헤더 칸의 생성자 초기값은 같은 폴더의 본문에서만 찾는다."""
    exceptions: dict[str, str] = config["_exceptions"]
    list_problem: list[str] = []
    set_seen: set[str] = set()
    for header_path, fact in map_fact.items():
        list_field = fact.get("fields")
        if not list_field:
            continue
        folder = header_path.rpartition("/")[0]
        map_initial: dict[str, dict[str, list[str]]] = {}
        for class_name, name, initial, line in list_field:
            if class_name not in map_initial:
                map_initial[class_name] = {}
                for source_path, source_fact in map_fact.items():
                    if class_name and source_path.rpartition("/")[0] == folder and class_name in source_fact.get("constructors", {}):
                        for member, list_text in source_fact["constructors"][class_name].items():
                            map_initial[class_name].setdefault(member, []).extend(list_text)
            literal = LITERAL.search(initial) or next((found for found in map(LITERAL.search, map_initial[class_name].get(name, [])) if found), None)
            if not literal:
                continue
            key = f"{header_path}::{class_name}::{name}"
            set_seen.add(key)
            if key not in exceptions:
                list_problem.append(f"{header_path}:{line}: {class_name}::{name} = {literal.group(0)} — 데이터에 있어야 할 이름이면 기본값을 지우고 PROPERTY( Required ) + 데이터 파일에, 엔진 상수면 예외 표({CONFIG_PATH})에 까닭과 함께")
    for key in exceptions if whole_repo else ():   # 낡은 예외는 저장소 전체를 봐야 안다
        if key not in set_seen:
            list_problem.append(f"{CONFIG_PATH}: '{key}' 는 더 이상 위반이 아니다 — 표에서 지운다")
    return list_problem


def find_problems(map_header: dict[str, str], map_source: dict[str, str], config: dict, whole_repo: bool = True) -> list[str]:
    """map_header · mapSource: 경로 → 글(`.cpp` 는 헤더와 같은 폴더의 것만 본다) — 자체 검사의 입구."""
    map_text = {**map_header, **map_source}
    return evaluate_facts({path: extract_facts(path, map_text[path], config) for path in sorted(map_text)}, config, whole_repo)


def check_required_name_fields(project_root: Path, scope: LintScope) -> list[str]:
    """
    scope 의 파일이 있던 폴더(지운 파일 포함 — .cpp 를 지우면 헤더 칸의 생성자 초기값이 사라진다)의 헤더 · 본문을 본다. 파일마다의 사실(칸 · 생성자 초기값)은
    내용 해시 색인(`index_cache`)에 두어, 스테이지 파일 하나에 그 폴더를 다시 읽지 않는다.
    """
    config = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    set_folder = {path.rpartition("/")[0] for path in (*scope.list_file, *scope.list_removed) if is_target(path)}
    if not set_folder:
        return []
    list_path = [path for path in list_repo_files(project_root) if is_target(path)]
    map_fact = load_facts(project_root, "required-name-fields", [__file__, str(project_root / CONFIG_PATH)], list_path,
                        lambda path, text: extract_facts(path, text, config), errors="strict",
                        want=[path for path in list_path if path.rpartition("/")[0] in set_folder], shard_of=lambda path: path.rpartition("/")[0])   # 한 폴더는 한 조각
    return evaluate_facts(map_fact, config, scope.whole_repo)


def run_self_check() -> None:
    config = {"_types": ["string", "hashed_string"], "_typePrefixes": ["AssetRef<"], "_exceptions": {}}
    header = (
        "namespace sw\n{\n    class SW_API Foo : public Component\n    {\n"
        "        PROPERTY( Category = \"A\", Tooltip = \"Required later\" )\n        string _bone;\n"
        "        PROPERTY()\n        hashed_string _slot{ \"Head\" };\n"
        "        PROPERTY( Required )\n        string _must;\n"
        "        PROPERTY( Transient )\n        hashed_string _cache{ \"X\" };\n"
        "        PROPERTY()\n        float32 _speed;\n"
        "        PROPERTY()\n        string _empty;\n"
        "    };\n}\n"
    )
    source = "namespace sw\n{\n    Foo::Foo()\n        : _bone{ \"foot.l\" } // 주석 \"x\"\n        , _must{ \"m\" }\n        , _speed( 1.0f )\n        , _empty{}\n    {\n    }\n}\n"
    list_problem = find_problems({"a/Foo.h": header}, {"a/Foo.cpp": source}, config)
    assert len(list_problem) == 2, list_problem
    assert "Foo::_bone" in list_problem[0] and "Foo::_slot" in list_problem[1], list_problem
    config["_exceptions"] = {"a/Foo.h::Foo::_slot": "엔진 상수", "a/Foo.h::Foo::_gone": "까닭"}
    list_problem = find_problems({"a/Foo.h": header}, {"a/Foo.cpp": source}, config)
    assert len(list_problem) == 2 and "_gone" in list_problem[1], list_problem
    assert find_problems({"a/Foo.h": header}, {"b/Foo.cpp": source}, {**config, "_exceptions": {"a/Foo.h::Foo::_slot": "까닭"}}) == [], "다른 폴더의 .cpp 는 보지 않는다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 기본값으로 숨은 이름 · 경로 PROPERTY 칸을 찾는다")
    parser.add_argument("--all", action="store_true", help="전체 소스(기본과 같다 — 다른 규칙과 인자를 맞춘다)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[RequiredNameFields] self-check OK")
        return ExitCode.PASSED
    list_problem = check_required_name_fields(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[RequiredNameFields] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
