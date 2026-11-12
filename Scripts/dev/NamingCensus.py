#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/NamingCensus.py

`Source` 의 모든 선언 이름(형식 · 열거자 · 별칭 · 함수 · 멤버 · 상수 · 변수 · 매크로)과 파일 · 폴더 이름을 낱말로 쪼개 낱말마다
종류별 이름 수 · 대표 이름 · 대표 파일을 낸다 — 이름 점검을 눈에 띈 것이 아니라 전수로 하려고 쓴다(`walk` · `aim` 이 멤버 · 파일 이름 가운데로 새어 들었다).
쪼개기와 선언 모으기는 `lint/NamingWords.py` 와 같은 함수다(린트의 `_words` 판정과 같은 낱말을 센다).

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
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import makeWholeScope  # noqa: E402
from lint.NamingWords import collectSourceNames, isExternalName, kDeclarationKinds, readTable, splitLowerWords  # noqa: E402


def buildCensus(listName: list[tuple[str, str, str]]) -> dict[str, dict[str, tuple[str, str]]]:
    """(종류, 이름, 파일) → 낱말 → 이름 → (종류, 첫 파일)."""
    census: dict[str, dict[str, tuple[str, str]]] = defaultdict(dict)
    for kind, name, path in listName:
        for word in splitLowerWords(name, kind):
            census[word].setdefault(name, (kind, path))
    return census


def writeCensus(outDir: Path, census: dict[str, dict[str, tuple[str, str]]], table: dict) -> int:
    wordClass = {word: "a" for word in table["_words"]["a"]} | {word: "b" for word in table["_words"]["b"]} | {word: "scoped" for word in table["_scopedWords"]}
    externals = [re.compile(pattern) for pattern in table["_externalPatterns"]]
    outDir.mkdir(parents=True, exist_ok=True)
    lines = ["word\tclass\tnames\t" + "\t".join(kDeclarationKinds) + "\texternal\tsamples\tsampleFile"]
    for word in sorted(census, key=lambda key: (-len(census[key]), key)):
        names = census[word]
        perKind = {kind: 0 for kind in kDeclarationKinds}
        for kind, _ in names.values():
            perKind[kind] += 1
        external = sum(1 for name in names if isExternalName(name, externals))
        samples = sorted(names, key=len)[:4]
        lines.append(f"{word}\t{wordClass.get(word, 'c')}\t{len(names)}\t" + "\t".join(str(perKind[kind]) for kind in kDeclarationKinds)
                     + f"\t{external}\t{' '.join(samples)}\t{names[samples[0]][1]}")
    (outDir / "words.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    data = {word: {name: list(where) for name, where in sorted(names.items())} for word, names in sorted(census.items())}
    (outDir / "words.json").write_text(json.dumps(data, ensure_ascii=False, indent=0), encoding="utf-8", newline="\n")
    return len(census)


def runSelfCheck() -> None:
    census = buildCensus([("member", "_walkSpeed", "a.h"), ("function", "hoverMark", "a.h"), ("file", "SceneWalkKey", "Source/a/SceneWalkKey.h")])
    assert set(census["walk"]) == {"_walkSpeed", "SceneWalkKey"} and census["walk"]["_walkSpeed"] == ("member", "a.h"), census
    assert "hover" in census and "mark" in census and "_" not in "".join(census), census


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts naming-census", description="Source 의 모든 이름을 낱말로 쪼개 종류별로 센다")
    parser.add_argument("--out", help="결과 폴더(words.tsv · words.json)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[NamingCensus] self-check OK")
        return ExitCode.Passed
    if not arguments.out:
        parser.error("--out 이 필요하다")
    projectRoot = getProjectRoot()
    census = buildCensus(collectSourceNames(projectRoot, makeWholeScope(projectRoot)))
    count = writeCensus(Path(arguments.out), census, readTable(projectRoot))
    print(f"[NamingCensus] 낱말 {count}개 → {arguments.out}")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
