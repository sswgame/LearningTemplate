#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/header_include_limits.py

헤더 포함 상한(래칫) — 다이어트한 헤더(docs/agents/CppStyle.md Includes and layers)가 pch 밖에서 (전이적으로) 끌어오는 헤더 수를
표(`Config/Rules/Lint/HeaderIncludeLimits.json` 의 `_limits` — 헤더 → 상한 · 까닭)의 상한과 견준다.
pch(`Engine/pch.h` · `Core/pch.h`)가 끄는 헤더는 TU 마다 이미 읽히므로 세지 않고, include 는 `#include "..."` 만 본다(조건부 포함은 모두 포함한 것으로 센다).
- 수가 상한보다 **늘면** 진다 — 전방 선언(`Engine/Object/ObjectFwd.h` · `Engine/Reflection/ReflectionFwd.h`)이나 `.inl` · .cpp 로 옮긴다.
- 수가 **줄면** 표도 같이 낮춰야 선다 — 옮긴 자리에 포함이 다시 들어오지 못한다(`magic-literals` 래칫과 같은 꼴).

    py -3 -m Scripts lint header-include-limits [--self-check]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Mapping, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import open_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402
from lint.rule_tables import ExceptionTable  # noqa: E402

RULE_ID = "header-include-limits"
_HEADER_SUFFIXES = (".h", ".hpp", ".inl", ".xxx")
_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
SUFFIXES = (".h", ".hpp", ".inl", ".cpp")
_PCH_HEADERS = ("Engine/pch.h", "Core/pch.h")


def find_includes(text: str) -> list[str]:
    """`#include "..."` 의 글(경로를 풀기 전)."""
    return _INCLUDE.findall(text)


def resolve_edges(map_include: dict[str, list[str]]) -> dict[str, list[str]]:
    """Source 기준 상대 경로 → include 글 목록을 저장소 파일 목록으로 푼다(Source 기준 경로, 없으면 같은 폴더 기준 — 저장소 밖 헤더는 뺀다)."""
    edges: dict[str, list[str]] = {}
    for relative, list_include in map_include.items():
        targets = []
        for include in list_include:
            sibling = (Path(relative).parent / include).as_posix()
            if include in map_include:
                targets.append(include)
            elif sibling in map_include:
                targets.append(sibling)
        edges[relative] = targets
    return edges


def compute_closures(edges: dict[str, list[str]]) -> dict[str, frozenset[str]]:
    """파일마다 전이적으로 포함하는 파일 집합(순환은 끊는다)."""
    closures: dict[str, frozenset[str]] = {}

    def visit(node: str, stack: frozenset[str]) -> frozenset[str]:
        if node in closures:
            return closures[node]
        result: set[str] = set()
        for target in edges.get(node, []):
            if target in stack:
                continue
            result.add(target)
            result |= visit(target, stack | {node})
        closures[node] = frozenset(result)
        return closures[node]

    sys.setrecursionlimit(max(10000, sys.getrecursionlimit()))
    for node in edges:
        visit(node, frozenset())
    return closures


def collect_pch_closure(closures: dict[str, frozenset[str]]) -> set[str]:
    """pch 가 끄는 헤더(pch 자신 포함) — TU 마다 이미 읽혀 세지 않는다."""
    pch_closure: set[str] = set()
    for pch in _PCH_HEADERS:
        if pch in closures:
            pch_closure |= closures[pch] | {pch}
    return pch_closure


def measure_non_pch(edges: dict[str, list[str]], headers: Iterable[str]) -> dict[str, int | None]:
    """헤더마다 pch 밖에서 끌어오는 헤더 수(`non_pch` 칸) — 없는 헤더는 None."""
    closures = compute_closures(edges)
    pch_closure = collect_pch_closure(closures)
    return {header: (len(closures[header] - pch_closure) if header in closures else None) for header in headers}


def list_limits(table: ExceptionTable) -> dict[str, int]:
    """헤더(Source 기준) → 상한."""
    return {header: int(entry["_limit"]) for header, entry in table.get("_limits").items()}


def find_problems(map_count: Mapping[str, int | None], table: ExceptionTable) -> list[str]:
    list_problem = table.list_problems(whole_repo=False)   # 까닭 빈 줄 — 낡은 줄은 아래 "Source 에 없다" 가 낸다
    for header, limit in sorted(list_limits(table).items()):
        count = map_count.get(header)
        if count is None:
            list_problem.append(f"{table.path}: `{header}` 가 Source 에 없다 — 표에서 지운다")
        elif count > limit:
            list_problem.append(f"Source/{header}: pch 밖 포함 {count} > 상한 {limit} — 전방 선언(`ObjectFwd.h` · `ReflectionFwd.h`) · `.inl` · .cpp 로 옮긴다")
        elif count < limit:
            list_problem.append(f"Source/{header}: pch 밖 포함 {count} < 상한 {limit} — `{table.path}` 의 상한을 {count} 로 낮춘다")
    return list_problem


def read_edges(project_root: Path) -> dict[str, list[str]]:
    """Source 의 include 그래프 — 파일마다의 include 글은 색인(`index_cache`)에 두고, 판정은 저장소 전체 (파일, include) 요약으로 푼다."""
    head = "Source/"
    list_path = [path for path in list_repo_files(project_root) if path.startswith(head) and path.endswith(SUFFIXES) and "ThirdParty" not in path.split("/")]
    facts = open_facts(project_root, "header-include-limits", [__file__], list_path,
                       lambda _, text: find_includes(text), errors="replace", contribute=lambda path, list_include: [f"{path[len(head):]}	{include}" for include in list_include])
    map_include: dict[str, list[str]] = {path[len(head):]: [] for path in list_path}
    for token in facts.summary:
        relative, _, include = token.partition("	")
        if relative in map_include:
            map_include[relative].append(include)
    return resolve_edges(map_include)


def check_header_include_limits(project_root: Path, scope: LintScope) -> list[str]:
    # 상한은 헤더의 포함 사슬만 본다 — .cpp 만 바뀐 커밋은 수가 바뀌지 않는다
    table = ExceptionTable.load(project_root, RULE_ID, ("_limits",))
    if not scope.whole_repo and not scope.touches(roots=("Source",), suffixes=_HEADER_SUFFIXES, files=(table.path,)):
        return []
    return find_problems(measure_non_pch(read_edges(project_root), list_limits(table)), table)


def run_self_check() -> None:
    table = ExceptionTable.make(RULE_ID, _limits={"A.h": {"_limit": 3, "_reason": "까닭"}})
    assert find_problems({"A.h": 3}, table) == [], "상한과 같으면 통과해야 한다"
    assert "> 상한" in find_problems({"A.h": 4}, table)[0], "상한보다 늘면 져야 한다"
    assert "낮춘다" in find_problems({"A.h": 2}, table)[0], "줄면 표를 낮추게 해야 한다(래칫)"
    assert "없다" in find_problems({"A.h": None}, table)[0], "없는 헤더의 항목은 낡은 줄이다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="다이어트한 헤더의 pch 밖 포함 수를 상한 표와 견준다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if arguments.self_check:
        run_self_check()
        print("[HeaderIncludeLimits] self-check OK")
        return ExitCode.PASSED
    list_problem = check_header_include_limits(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[HeaderIncludeLimits] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
