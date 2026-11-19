"""
헤더 팬인 측정 — 헤더마다 그것을 (전이적으로) 포함하는 .cpp 수와, 미리 컴파일 헤더(pch) 밖에서 새로 끌어오는 헤더 수를 센다(빌드 없이 소스만 읽는다).

  py -3 -m Scripts header-fanin                 # 점수(팬인 × pch 밖 포함 수) 상위 20
  py -3 -m Scripts header-fanin --top 40 --json build/header_fanin.json

점수는 "이 헤더를 고치면 다시 컴파일되는 TU 수 × 그 TU 마다 이 헤더 때문에 더 읽는 헤더 수" 다. pch(`Engine/pch.h` · `Core/pch.h`)가 끄는 헤더는
TU 마다 이미 읽히므로 점수에서 뺀다. 헤더 다이어트(DesignBacklog "헤더 팬인 다이어트")의 전후 비교 숫자다 — 커밋 전후에 같은 명령으로 잰다.
include 는 `#include "..."` 만 본다(조건부 포함 · 매크로 경로는 모두 포함한 것으로 센다 — 위로 어림한 값이다).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

from common.paths import get_project_root

_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
_SUFFIXES = (".h", ".hpp", ".inl", ".cpp")
_PCH_HEADERS = ("Engine/pch.h", "Core/pch.h")


def collect_edges(source_root: Path) -> dict[str, list[str]]:
    """Source 기준 상대 경로 → 그 파일이 포함하는 저장소 파일들."""
    files = {path.relative_to(source_root).as_posix(): path for path in source_root.rglob("*") if path.suffix in _SUFFIXES and "ThirdParty" not in path.parts}
    edges: dict[str, list[str]] = {}
    for relative, path in files.items():
        text = path.read_text(encoding="utf-8", errors="replace")
        targets = []
        for include in _INCLUDE.findall(text):
            sibling = (Path(relative).parent / include).as_posix()
            if include in files:
                targets.append(include)
            elif sibling in files:
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


def measure(source_root: Path) -> list[dict[str, object]]:
    """헤더마다 {header, fan_in, non_pch, score} — 점수 내림차순."""
    edges = collect_edges(source_root)
    closures = compute_closures(edges)
    pch_closure: set[str] = set()
    for pch in _PCH_HEADERS:
        if pch in closures:
            pch_closure |= closures[pch] | {pch}
    fan_in: dict[str, int] = defaultdict(int)
    for node, closure in closures.items():
        if node.endswith(".cpp"):
            for header in closure:
                fan_in[header] += 1
    rows = []
    for header, count in fan_in.items():
        if header.endswith(".cpp") or header in pch_closure:
            continue
        non_pch = len(closures.get(header, frozenset()) - pch_closure)
        rows.append({"header": header, "fan_in": count, "non_pch": non_pch, "score": count * non_pch})
    rows.sort(key=lambda row: (-int(row["score"]), str(row["header"])))
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts header-fanin", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--json", default=None, help="모든 줄을 JSON 으로 쓴다(전후 비교용)")
    args = parser.parse_args(argv)
    rows = measure(get_project_root() / "Source")
    print(f"{'score':>8} {'fan-in':>7} {'non-pch':>8}  header")
    for row in rows[: args.top]:
        print(f"{row['score']:>8} {row['fan_in']:>7} {row['non_pch']:>8}  {row['header']}")
    print(f"total score {sum(int(row['score']) for row in rows)}")
    if args.json:
        Path(args.json).parent.mkdir(parents=True, exist_ok=True)
        Path(args.json).write_text(json.dumps(rows, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
