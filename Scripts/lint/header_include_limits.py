#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/header_include_limits.py

헤더 포함 상한(래칫) — 다이어트한 헤더(docs/agents/CppStyle.md Includes and layers)가 pch 밖에서 (전이적으로) 끌어오는 헤더 수를
표(`Config/Rules/Lint/HeaderIncludeLimits.json` 의 `_limits` — 헤더 → 상한 · 까닭)의 상한과 견준다. 수는 `header-fanin` 의 `non-pch` 칸과 같은 계산이다(`dev.header_fanin`).
- 수가 상한보다 **늘면** 진다 — 전방 선언(`Engine/Object/ObjectFwd.h` · `Engine/Reflection/ReflectionFwd.h`)이나 `.inl` · .cpp 로 옮긴다.
- 수가 **줄면** 표도 같이 낮춰야 선다 — 옮긴 자리에 포함이 다시 들어오지 못한다(`magic-literals` 래칫과 같은 꼴).

    py -3 -m Scripts lint header-include-limits [--self-check]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Mapping, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from dev.header_fanin import SUFFIXES, find_includes, measure_non_pch, resolve_edges  # noqa: E402
from lint.index_cache import open_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402
from lint.rule_tables import ExceptionTable  # noqa: E402

RULE_ID = "header-include-limits"
_HEADER_SUFFIXES = (".h", ".hpp", ".inl", ".xxx")


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
    facts = open_facts(project_root, "header-include-limits", [__file__, str(Path(__file__).resolve().parents[1] / "dev" / "header_fanin.py")], list_path,
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
