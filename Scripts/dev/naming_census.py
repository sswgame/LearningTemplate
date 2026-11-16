#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/naming_census.py

`Source` 의 모든 선언 이름(형식 · 열거자 · 별칭 · 함수 · 멤버 · 상수 · 변수 · 매크로)과 파일 · 폴더 이름을 낱말로 쪼개 낱말마다
종류별 이름 수 · 대표 이름 · 대표 파일을 낸다 — 이름 점검을 눈에 띈 것이 아니라 전수로 하려고 쓴다(`walk` · `aim` 이 멤버 · 파일 이름 가운데로 새어 들었다).
쪼개기와 선언 모으기는 `lint/naming_words.py` 와 같은 함수다(린트의 `_words` 판정과 같은 낱말을 센다).

`py -3 -m Scripts naming-census --out <폴더> [--only-unknown]`
- `words.tsv`  : 낱말 · 표 분류(a 업계 · b 게임 · scoped 경로 한정 · c 표에 없음) · 이름 수 · 종류별 수 · 바깥 이름 수 · 대표 이름 · 대표 파일
- `words.json` : 낱말 → 이름 → [종류, 첫 파일] (판정 · 개명 목록을 만들 때 읽는다)
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — 콘솔 UTF-8
from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import make_whole_scope  # noqa: E402
from lint.naming_words import collect_source_names, is_external_name, DECLARATION_KINDS, read_table, split_lower_words  # noqa: E402


def build_census(list_name: list[tuple[str, str, str]]) -> dict[str, dict[str, tuple[str, str]]]:
    """(종류, 이름, 파일) → 낱말 → 이름 → (종류, 첫 파일)."""
    census: dict[str, dict[str, tuple[str, str]]] = defaultdict(dict)
    for kind, name, path in list_name:
        for word in split_lower_words(name, kind):
            census[word].setdefault(name, (kind, path))
    return census


def write_census(out_dir: Path, census: dict[str, dict[str, tuple[str, str]]], table: dict) -> int:
    word_class = {word: "a" for word in table["_words"]["a"]} | {word: "b" for word in table["_words"]["b"]} | {word: "scoped" for word in table["_scopedWords"]}
    externals = [re.compile(pattern) for pattern in table["_externalPatterns"]]
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = ["word\tclass\tnames\t" + "\t".join(DECLARATION_KINDS) + "\texternal\tsamples\tsampleFile"]
    for word in sorted(census, key=lambda key: (-len(census[key]), key)):
        names = census[word]
        per_kind = {kind: 0 for kind in DECLARATION_KINDS}
        for kind, _ in names.values():
            per_kind[kind] += 1
        external = sum(1 for name in names if is_external_name(name, externals))
        samples = sorted(names, key=len)[:4]
        lines.append(f"{word}\t{word_class.get(word, 'c')}\t{len(names)}\t" + "\t".join(str(per_kind[kind]) for kind in DECLARATION_KINDS)
                     + f"\t{external}\t{' '.join(samples)}\t{names[samples[0]][1]}")
    (out_dir / "words.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    data = {word: {name: list(where) for name, where in sorted(names.items())} for word, names in sorted(census.items())}
    (out_dir / "words.json").write_text(json.dumps(data, ensure_ascii=False, indent=0), encoding="utf-8", newline="\n")
    return len(census)


def run_self_check() -> None:
    census = build_census([("member", "_walkSpeed", "a.h"), ("function", "hoverMark", "a.h"), ("file", "SceneWalkKey", "Source/a/SceneWalkKey.h")])
    assert set(census["walk"]) == {"_walkSpeed", "SceneWalkKey"} and census["walk"]["_walkSpeed"] == ("member", "a.h"), census
    assert "hover" in census and "mark" in census and "_" not in "".join(census), census


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts naming-census", description="Source 의 모든 이름을 낱말로 쪼개 종류별로 센다")
    parser.add_argument("--out", help="결과 폴더(words.tsv · words.json)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[NamingCensus] self-check OK")
        return ExitCode.PASSED
    if not arguments.out:
        parser.error("--out 이 필요하다")
    project_root = get_project_root()
    census = build_census(collect_source_names(project_root, make_whole_scope(project_root)))
    count = write_census(Path(arguments.out), census, read_table(project_root))
    print(f"[NamingCensus] 낱말 {count}개 → {arguments.out}")
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
