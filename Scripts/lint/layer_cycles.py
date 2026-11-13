#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/layer_cycles.py

엔진 층 표(`Config/Rules/Layers/EngineLayers.toml` 의 `[tier]`)에서 **같은 층** 의 두 폴더(표의 키)가 서로 include 하는지 본다.

**왜 린트인가.** 위 층 include 는 빌드가 막는다(층마다 부분을 짓고 위 층 헤더 자리에 `#error` 껍데기). 같은 층은 한 부분이라 껍데기가 없다 —
폴더마다 부분을 나누면 층이 수십 개가 되고 층 번호가 폴더 순서를 흉내 낼 뿐이다. 그래서 같은 층 안에서는 한 방향 include 는 두고,
**두 방향(순환)** 만 여기서 막는다. 순환이 보이면 한쪽을 위 층으로 올리거나(층 표), 형식 · 창구를 아래로 내려 방향을 하나로 만든다.

아직 순환이 남은 층은 표의 `[sameTierCycle]`("층 번호" = "까닭")에 적는다 — 적지 않은 층은 모두 검사한다(새 층의 기본은 검사).
키 · 층 찾기는 생성기(`Scripts/generate/` 의 층 생성기)와 같은 규칙이다: `Source/Engine/` 아래 경로 접두어, 가장 긴 것이 이긴다.

  py -3 -m Scripts lint layer-cycles [--self-check] [--census Engine|Editor]

`--census` 는 막지 않고 층 안 폴더 쌍의 include 수(양방향은 `<->`)를 찍는다(에디터 층 표도 같은 모양으로 센다).
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from collections import Counter
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import open_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402

EXTENSIONS = (".h", ".hpp", ".inl", ".cpp", ".c", ".xxx")
ROOT_LAYER_NAME = "<root>"   # 에디터 표: `Source/Editor` 바로 아래 파일


def find_key(map_tier: dict[str, int], relative: str) -> str | None:
    """`relative` 를 덮는 가장 긴 키(폴더 또는 파일). 없으면 None."""
    best: str | None = None
    for key in map_tier:
        if (relative == key or relative.startswith(key + "/")) and (best is None or len(key) > len(best)):
            best = key
    return best


def find_includes(module: str, text: str) -> list[str]:
    """글 하나가 include 하는 `<module>/` 아래 경로(모듈 기준) — 색인(`FactCache`)에 두는 사실이다."""
    return re.findall(r'^\s*#\s*include\s*"' + re.escape(module) + r'/([^"]+)"', text, re.M)


def count_include_pairs(map_tier: dict[str, int], map_include: dict[str, list[str]]) -> Counter[tuple[str, str]]:
    """같은 층의 다른 키 사이 include 수 — (include 하는 키, include 되는 키). `map_include` 는 모듈 기준 경로 → include 목록."""
    counter: Counter[tuple[str, str]] = Counter()
    for relative, list_include in map_include.items():
        source = find_key(map_tier, relative) or (ROOT_LAYER_NAME if "/" not in relative and ROOT_LAYER_NAME in map_tier else None)
        if source is None:
            continue
        for include in list_include:
            target = find_key(map_tier, include)
            if target is not None and target != source and map_tier[target] == map_tier[source]:
                counter[(source, target)] += 1
    return counter


def find_cycles(map_tier: dict[str, int], counter: Counter[tuple[str, str]], set_allowed_tier: set[int]) -> list[str]:
    list_problem: list[str] = []
    for (source, target), count in sorted(counter.items()):
        back = counter.get((target, source), 0)
        if back and source < target and map_tier[source] not in set_allowed_tier:
            list_problem.append(f"층 {map_tier[source]}: {source} ↔ {target} ({count}/{back}) — 같은 층 폴더가 서로 include 한다. 한쪽을 위 층으로 올리거나 창구를 아래로 내려 방향을 하나로")
    return list_problem


def read_table(project_root: Path, module: str) -> dict:
    return tomllib.loads((project_root / "Config" / "Rules" / "Layers" / f"{module}Layers.toml").read_text(encoding="utf-8"))


def count_pairs(map_tier: dict[str, int], module: str, list_source: Iterable[tuple[str, str]]) -> Counter[tuple[str, str]]:
    return count_include_pairs(map_tier, {relative: find_includes(module, text) for relative, text in list_source})


def read_pairs(project_root: Path, module: str, map_tier: dict[str, int]) -> Counter[tuple[str, str]]:
    """같은 층 키 사이 include 수(`count_include_pairs`) — 파일마다의 include 는 색인(`FactCache`)에 두고, 판정은 파일마다 접은 쌍의 수(요약)만 읽는다."""
    head = f"Source/{module}/"
    list_path = [path for path in list_repo_files(project_root) if path.startswith(head) and path.endswith(EXTENSIONS)]
    facts = open_facts(project_root, f"layer-cycles-{module}", [__file__, str(project_root / "Config" / "Rules" / "Layers" / f"{module}Layers.toml")], list_path,
                      lambda _, text: find_includes(module, text), errors="replace",
                      contribute=lambda path, list_include: [f"{source}\t{target}" for (source, target), count in count_include_pairs(map_tier, {path[len(head):]: list_include}).items()
                                                            for _ in range(count)])
    return Counter({tuple(token.split("\t")): count for token, count in facts.summary.items()})


def check_layer_cycles(project_root: Path, scope: LintScope) -> list[str]:
    """순환은 두 방향이 모두 있어야 보이므로 판정은 늘 저장소 전체 색인으로 낸다. 커밋 훅은 Engine C++ 이 바뀌었을 때만 돈다."""
    if not scope.whole_repo and not scope.touches(("Source/Engine",), EXTENSIONS):
        return []
    table = read_table(project_root, "Engine")
    map_tier: dict[str, int] = table["tier"]
    set_allowed_tier = {int(tier) for tier in table.get("sameTierCycle", {})}
    return find_cycles(map_tier, read_pairs(project_root, "Engine", map_tier), set_allowed_tier)


def print_census(project_root: Path, module: str) -> None:
    map_tier: dict[str, int] = read_table(project_root, module)["tier"]
    counter = read_pairs(project_root, module, map_tier)
    for (source, target), count in sorted(counter.items(), key=lambda item: (map_tier[item[0][0]], item[0])):
        back = counter.get((target, source), 0)
        if back and source > target:
            continue
        arrow = f"<-> {target} {count}/{back}" if back else f"-> {target} {count}"
        print(f"  층 {map_tier[source]}: {source} {arrow}")


def run_self_check() -> None:
    map_tier = {"A": 6, "B": 6, "C": 7, "A/Low": 5}
    list_source = [("A/x.cpp", '#include "Engine/B/y.h"\n'), ("B/y.cpp", '#include "Engine/A/x.h"\n'),
                  ("C/z.cpp", '#include "Engine/A/x.h"\n'), ("A/Low/w.cpp", '#include "Engine/B/y.h"\n')]
    counter = count_pairs(map_tier, "Engine", list_source)
    assert counter == Counter({("A", "B"): 1, ("B", "A"): 1}), "다른 층 · 더 긴 키(A/Low)는 세지 않는다"
    assert len(find_cycles(map_tier, counter, set())) == 1, "A ↔ B 는 하나로 낸다"
    assert find_cycles(map_tier, counter, {6}) == [], "적은 층은 두고 본다"
    assert find_cycles(map_tier, count_pairs(map_tier, "Engine", list_source[:1]), set()) == [], "한 방향은 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="같은 엔진 층의 폴더끼리 서로 include 하는지 본다")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--census", choices=("Engine", "Editor"), help="막지 않고 층 안 폴더 쌍 include 수를 찍는다")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[layer-cycles] self-check OK")
        return ExitCode.PASSED
    if arguments.census:
        print_census(get_project_root(), arguments.census)
        return ExitCode.PASSED
    list_problem = check_layer_cycles(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[layer-cycles] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
