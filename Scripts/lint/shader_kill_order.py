#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/shader_kill_order.py

픽셀 버리기(`clip` · `discard` · `SW_APPEARANCE_CLIP`) 뒤에 미분을 쓰는 읽기(암묵 밉 `Sample` · `SampleBias` · `SampleCmp`, `ddx` · `ddy` · `fwidth`)가 오는 함수를 막는다.
Vulkan 1.2 타깃의 `clip` 은 OpKill 이라 버린 픽셀이 쿼드 이웃의 미분에서 빠져 이웃의 밉 선택이 틀린다(binding.hlsli 8-1 절). 1.3 의 demote 를 요구하지 않으려고 순서로 막는다.
**왜 형식이 아니라 린트인가.** HLSL 에는 "이 함수는 미분을 쓴다" 를 형식으로 적을 자리가 없고, 순서는 한 함수 본문 안의 문장 순서다.

- 미분을 쓰는 함수: 본문에 위 읽기가 있거나 그런 함수를 부르는 함수(셰이더 전체의 호출 닫힘). 이름은 `::` 뒤의 마지막 이름으로 본다 — 같은 이름의 정의가 여럿(`#if` 갈래)이면 하나라도 쓰면 쓴다고 본다.
- 버리는 함수: 본문에 버리기가 있거나 그런 함수를 부르는 함수(`Appearance::clipDither`). `SW_APPEARANCE_CLIP(` 는 그 자체로 버리기다.
- 위반: 한 함수 본문에서 첫 버리기 뒤에 미분을 쓰는 읽기 · 호출이 있다. 전처리 갈래는 보지 않는다(보수적).

`py -3 -m Scripts lint shader-kill-order [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import Facts, compute_version, CACHE_FOLDER, open_facts, write_json  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402

EXTENSIONS = (".hlsl", ".hlsli")
COMMENT_PATTERN = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
PREPROCESSOR_PATTERN = re.compile(r"^[ \t]*#[^\n]*(?:\\\n[^\n]*)*", re.M)
FUNCTION_PATTERN = re.compile(r"(?:^|[;}\n])[ \t]*(?:static[ \t]+|inline[ \t]+|precise[ \t]+)*[\w:<>,]+[ \t]+([A-Za-z_][\w:]*)[ \t]*\(([^;{}()]|\([^;{}()]*\))*\)\s*(?::\s*\w+\s*)?\{")
DERIVATIVE_PATTERN = re.compile(r"\.(?:Sample|SampleBias|SampleCmp)\s*\(|\b(?:ddx|ddy|ddx_coarse|ddy_coarse|ddx_fine|ddy_fine|fwidth)\s*\(")
KILL_PATTERN = re.compile(r"\bclip\s*\(|\bdiscard\b|\bSW_APPEARANCE_CLIP\s*\(")
CALL_PATTERN = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
KEYWORDS = {"if", "for", "while", "switch", "return", "clip", "discard"}


def find_closing_brace(code: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code)


def collect_functions(path: str, text: str) -> list[tuple[str, str, str]]:
    """글 하나의 (경로, 마지막 이름, 본문) 들을 돌려준다. 전처리 줄은 지운다(갈래를 모두 본문으로 본다)."""
    code = PREPROCESSOR_PATTERN.sub("", COMMENT_PATTERN.sub("", text))
    list_function: list[tuple[str, str, str]] = []
    for match in FUNCTION_PATTERN.finditer(code):
        name = match.group(1).split("::")[-1]
        if name in KEYWORDS:
            continue
        open_index = match.end() - 1
        list_function.append((path, name, code[open_index + 1:find_closing_brace(code, open_index)]))
    return list_function


def close_over(list_function: list[tuple[str, str, str]], pattern: re.Pattern) -> set[str]:
    """본문에 @p pattern 이 있거나 그런 함수를 부르는 함수 이름의 닫힘이다."""
    setName = {name for _, name, body in list_function if pattern.search(body)}
    changed = True
    while changed:
        changed = False
        for _, name, body in list_function:
            if name in setName:
                continue
            if any(call in setName for call in CALL_PATTERN.findall(body)):
                setName.add(name)
                changed = True
    return setName


def find_first_kill(body: str, set_kill: set[str]) -> int:
    first = len(body)
    match = KILL_PATTERN.search(body)
    if match:
        first = match.start()
    for call in CALL_PATTERN.finditer(body):
        if call.group(1) in set_kill and call.start() < first:
            first = call.start()
    return first if first < len(body) else -1


def find_problems(list_source: Iterable[tuple[str, str]]) -> list[str]:
    return find_problems_in_functions([function for path, text in list_source for function in collect_functions(path, text)])


def judge_function(path: str, name: str, body: str, set_kill: set[str], set_derivative: set[str]) -> str | None:
    """함수 하나의 위반 줄(없으면 None) — 버리기 · 미분 닫힘(`set_kill` · `set_derivative`)과 그 함수 본문만 본다."""
    kill_index = find_first_kill(body, set_kill)
    if kill_index < 0:
        return None
    rest = body[kill_index:]
    rest = rest[rest.find("(") + 1:] if rest.startswith(("clip", "SW_APPEARANCE_CLIP")) or rest[:1].isalpha() else rest
    derivative = DERIVATIVE_PATTERN.search(rest)
    late_call = next((call.group(1) for call in CALL_PATTERN.finditer(rest) if call.group(1) in set_derivative and call.group(1) != name), None)
    if not (derivative or late_call):
        return None
    what = derivative.group(0).strip() if derivative else f"{late_call}()"
    return f"{path}: {name} 이 픽셀을 버린 뒤 미분을 쓰는 읽기({what})를 한다 — 버리기를 출력을 다 만든 뒤로 옮긴다(binding.hlsli 8-1 절)"


def find_problems_in_functions(list_function: list[tuple[str, str, str]]) -> list[str]:
    """(경로, 이름, 몸) 목록 — 버리기 · 미분을 부르는 함수 닫힘은 파일을 건너므로 셰이더 전체를 넘긴다(자체 검사의 입구)."""
    set_derivative = close_over(list_function, DERIVATIVE_PATTERN)
    set_kill = close_over(list_function, KILL_PATTERN)
    return [problem for path, name, body in list_function if (problem := judge_function(path, name, body, set_kill, set_derivative)) is not None]


def fold_functions(path: str, list_function: list[list[str]]) -> list[str]:
    """파일 하나의 함수마다 요약 토큰 — (경로, 차례, 이름, 본문 해시, 직접 버리기, 직접 미분, 부르는 이름들). 본문은 조각에만 둔다."""
    return ["\t".join([path, str(index), name, hashlib.sha1(body.encode("utf-8")).hexdigest()[:16], "1" if KILL_PATTERN.search(body) else "0",
                       "1" if DERIVATIVE_PATTERN.search(body) else "0", ",".join(sorted(set(CALL_PATTERN.findall(body))))])
            for index, (name, body) in enumerate(list_function)]


@dataclass(frozen=True)
class FunctionRow:
    path: str
    index: int
    name: str
    body_hash: str
    has_kill: bool
    has_derivative: bool
    set_call: frozenset[str]


def parse_row(token: str) -> FunctionRow:
    path, index, name, body_hash, kill, derivative, calls = token.split("\t")
    return FunctionRow(path, int(index), name, body_hash, kill == "1", derivative == "1", frozenset(calls.split(",")) - {""})


def close_over_rows(list_row: list[FunctionRow], is_direct: Callable[[FunctionRow], bool]) -> set[str]:
    """`close_over` 와 같은 닫힘을 역방향 간선(부르는 이름 → 그 이름을 부르는 함수 이름)으로 — 본문 없이 요약만으로."""
    map_caller: dict[str, set[str]] = {}
    for row in list_row:
        for call in row.set_call:
            map_caller.setdefault(call, set()).add(row.name)
    setName = {row.name for row in list_row if is_direct(row)}
    list_pending = list(setName)
    while list_pending:
        for caller in map_caller.get(list_pending.pop(), ()):
            if caller not in setName:
                setName.add(caller)
                list_pending.append(caller)
    return setName


def judge_incremental(list_row: list[FunctionRow], map_cached: dict[str, str], read_bodies: Callable[[list[str]], dict[str, list[list[str]]]],
                     list_path_order: Sequence[str]) -> tuple[list[str], dict[str, str]]:
    """
    요약(함수마다의 행)으로 닫힘을 내고, 버리기에 닿는 함수(직접 버리거나 버리는 이름을 부른다)만 판정한다. 판정은 (경로, 차례, 이름, 본문 해시, 부르는 이름 중 닫힘에 든 것)을
    열쇠로 `map_cached` 에서 꺼내고, 없을 때만 그 파일의 본문(`read_bodies`)을 읽는다 — 바뀐 파일의 함수와, 닫힘 소속이 바뀐 이름을 부르는 함수만 다시 판정한다.
    돌려주는 것은 (위반 줄, 새 판정 캐시).
    """
    set_kill = close_over_rows(list_row, lambda row: row.has_kill)
    set_derivative = close_over_rows(list_row, lambda row: row.has_derivative)
    map_key: dict[str, FunctionRow] = {}
    for row in list_row:
        if row.has_kill or row.set_call & set_kill:
            map_key[json.dumps([row.path, row.index, row.name, row.body_hash, sorted(row.set_call & set_kill), sorted(row.set_call & set_derivative)])] = row
    list_miss = sorted({row.path for key, row in map_key.items() if key not in map_cached})
    map_body = read_bodies(list_miss) if list_miss else {}
    map_result: dict[str, str] = {}
    for key, row in map_key.items():
        if key in map_cached:
            map_result[key] = map_cached[key]
        else:
            name, body = map_body[row.path][row.index]
            map_result[key] = judge_function(row.path, name, body, set_kill, set_derivative) or ""
    map_order = {path: order for order, path in enumerate(list_path_order)}
    list_problem = [map_result[key] for key, row in sorted(map_key.items(), key=lambda item: (map_order.get(item[1].path, len(map_order)), item[1].path, item[1].index))
                   if map_result[key]]
    return list_problem, map_result


def check_shader_kill_order(project_root: Path, scope: LintScope) -> list[str]:
    """
    Content · Source 의 셰이더 함수 — 함수마다의 요약 행(`fold_functions`)만 읽어 버리기 · 미분 닫힘을 내고(역방향 간선), 버리기에 닿는 함수의 판정은
    `judgement.json` 에 둔다. 본문 조각은 판정이 없는(바뀐 파일 · 닫힘 소속이 바뀐 이름을 부르는) 함수의 파일만 읽는다(`judge_incremental`).
    """
    if not scope.whole_repo and not scope.touches(("Content", "Source"), EXTENSIONS):
        return []
    list_path = [path for path in list_repo_files(project_root)
                if path.startswith(("Content/", "Source/")) and path.endswith(EXTENSIONS) and "ThirdParty" not in path.split("/")]

    def open_functions(want: Iterable[str]) -> Facts:
        return open_facts(project_root, "shader-kill-order", [__file__], list_path, lambda path, text: [[name, body] for _, name, body in collect_functions(path, text)],
                         want=want, contribute=fold_functions)

    list_row = [parse_row(token) for token in open_functions(()).summary]
    judgement_path = project_root / CACHE_FOLDER / "shader-kill-order" / "judgement.json"
    version = compute_version([__file__])   # 판정 글이 바뀌면 캐시한 판정을 버린다
    try:
        document = json.loads(judgement_path.read_text(encoding="utf-8"))
        map_cached = document["results"] if document.get("version") == version else {}
    except (OSError, ValueError, KeyError):
        map_cached = {}
    list_problem, map_result = judge_incremental(list_row, map_cached, lambda list_miss: open_functions(list_miss).map_fact, list_path)
    if map_result != map_cached:
        judgement_path.parent.mkdir(parents=True, exist_ok=True)
        write_json(judgement_path, {"version": version, "results": map_result})
    return list_problem


def run_self_check() -> None:
    helper = "float4 readAlbedo( float2 uv )\n{\n    return g_Albedo.Sample( g_Sampler, uv );\n}\n"
    killer = "static void Appearance::clipDither( float a, float2 p )\n{\n    clip( a - 0.5f );\n}\n"
    bad = helper + killer + "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    Appearance::clipDither( 0.5f, uv );\n    return readAlbedo( uv );\n}\n"
    good = helper + killer + "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    const float4 albedo = readAlbedo( uv );\n    Appearance::clipDither( 0.5f, uv );\n    return albedo;\n}\n"
    direct = "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    clip( uv.x - 0.5f );\n    return float4( ddx( uv ), 0.0f, 1.0f );\n}\n"
    level = "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    discard;\n    return g_Albedo.SampleLevel( g_Sampler, uv, 0.0f );\n}\n"
    assert len(find_problems([("a.hlsl", bad)])) == 1, find_problems([("a.hlsl", bad)])
    assert find_problems([("a.hlsl", good)]) == [], find_problems([("a.hlsl", good)])
    assert len(find_problems([("a.hlsl", direct)])) == 1
    assert find_problems([("a.hlsl", level)]) == []  # 밉을 적은 읽기는 미분을 쓰지 않는다
    assert find_problems([("a.hlsl", "// clip( x ); g.Sample( s, uv );\nfloat f() { return 1.0f; }\n")]) == []
    # 요약 + 판정 캐시(증분)가 전체 판정과 같다 — 처음(캐시 없음), 그대로(본문을 읽지 않는다), 버리기 함수를 고친 뒤(그 이름을 부르는 함수만 다시 판정).
    map_source = {"a.hlsl": helper + killer, "b.hlsl": "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    Appearance::clipDither( 0.5f, uv );\n    return readAlbedo( uv );\n}\n",
                 "c.hlsl": good.replace("PSMain", "PSGood"), "d.hlsl": direct}

    def incremental(map_text: dict[str, str], map_cached: dict[str, str]) -> tuple[list[str], dict[str, str], list[str]]:
        map_fact = {path: [[name, body] for _, name, body in collect_functions(path, text)] for path, text in map_text.items()}
        list_read: list[str] = []
        list_row = [parse_row(token) for path in sorted(map_fact) for token in fold_functions(path, map_fact[path])]
        list_problem, map_result = judge_incremental(list_row, map_cached, lambda list_miss: (list_read.extend(list_miss), {path: map_fact[path] for path in list_miss})[1], sorted(map_text))
        assert list_problem == find_problems(sorted(map_text.items())), (list_problem, find_problems(sorted(map_text.items())))
        return list_problem, map_result, list_read

    list_problem, map_cached, list_read = incremental(map_source, {})
    assert len(list_problem) == 2 and list_read, list_problem
    assert incremental(map_source, map_cached)[2] == [], "바뀐 것이 없으면 본문을 읽지 않는다"
    map_split = {"h.hlsl": helper, "k.hlsl": killer, "p.hlsl": map_source["b.hlsl"], "d.hlsl": direct}
    _, map_cached, _ = incremental(map_split, {})
    list_problem, _, list_read = incremental(dict(map_split, **{"h.hlsl": helper.replace("g_Albedo.Sample( g_Sampler, uv )", "float4( uv, 0.0f, 1.0f )")}), map_cached)
    assert list_read == ["p.hlsl"] and len(list_problem) == 1, (list_read, list_problem)   # 미분 닫힘에서 빠진 이름을 부르는 함수의 파일만 다시 읽는다


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="픽셀 버리기 뒤의 미분 읽기를 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[ShaderKillOrder] self-check OK")
        return ExitCode.PASSED
    list_problem = check_shader_kill_order(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[ShaderKillOrder] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
