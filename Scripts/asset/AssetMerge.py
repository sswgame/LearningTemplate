#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XML 에셋(씬 · 프리팹 · 머티리얼 · 카탈로그)의 의미 단위 비교와 3-way 병합 — git 의 병합 · 비교 드라이버로도 쓴다.

    py -3 -m Scripts asset-merge diff  <before.xml> <after.xml>              # 엔티티 · 컴포넌트 · 속성 단위 차이(같으면 0, 다르면 1)
    py -3 -m Scripts asset-merge merge <base> <ours> <theirs> [-o out] [--prefer ours|theirs]
    py -3 Scripts/asset/AssetMerge.py git-merge %O %A %B %P                  # git 병합 드라이버 — 결과를 %A 에 쓴다
    py -3 Scripts/asset/AssetMerge.py git-diff <path> <old> <oldHex> <oldMode> <new> <newHex> <newMode>   # GIT_EXTERNAL_DIFF 모양

병합 종료 코드: 0 = 충돌 없음, 1 = 충돌 있음(결과 파일에 `<!-- MERGE CONFLICT … -->` 주석 — 우리 쪽 값을 둔 채로), 2 = 읽을 수 없음.
`--prefer` 를 주면 충돌을 그쪽으로 풀고 0 으로 끝낸다(주석 없음). 읽을 수 없는(XML 이 아닌) 입력이면 git 은 보통 병합으로 돌아가야 하므로
드라이버는 2 를 돌려준다 — git 은 그 파일을 충돌로 남긴다.

git 에 붙이는 방법(강제하지 않는다 — Scripts/asset/README.md):
    git config merge.swasset.name   "SW XML asset merge"
    git config merge.swasset.driver "py -3 Scripts/asset/AssetMerge.py git-merge %O %A %B %P"
    git config diff.swasset.command  "py -3 Scripts/asset/AssetMerge.py git-diff"
    # .gitattributes
    *.scene.xml   merge=swasset diff=swasset
    *.prefab.xml  merge=swasset diff=swasset
    *.material    merge=swasset diff=swasset
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.XmlAssetMerge import XmlAsset, diffAssets, mergeAssets, parseXmlAsset, serializeXmlAsset  # noqa: E402


def readAssetInternal(path: Path) -> XmlAsset:
    return parseXmlAsset(path.read_text(encoding="utf-8-sig"))


def writeTextInternal(path: Path, text: str, bCrlf: bool) -> None:
    """원래 파일의 줄 끝(CRLF · LF)을 지켜 씁니다 — 줄 끝만 바뀐 파일은 git 이 통째로 바뀐 것으로 본다."""
    path.write_bytes((text.replace("\n", "\r\n") if bCrlf else text).encode("utf-8"))


def runDiffInternal(beforePath: Path, afterPath: Path, label: str | None = None) -> int:
    listChange = diffAssets(readAssetInternal(beforePath), readAssetInternal(afterPath))
    title = label or f"{beforePath} -> {afterPath}"
    if not listChange:
        print(f"{title}: no semantic changes")
        return 0
    print(f"{title}: {len(listChange)} change(s)")
    for change in listChange:
        print(f"  {change.describe()}")
    return 1


def runMergeInternal(basePath: Path, oursPath: Path, theirsPath: Path, outputPath: Path, prefer: str | None, label: str) -> int:
    try:
        base, ours, theirs = readAssetInternal(basePath), readAssetInternal(oursPath), readAssetInternal(theirsPath)
    except (ElementTree.ParseError, OSError, UnicodeDecodeError) as error:
        print(f"[AssetMerge] {label}: cannot read the three versions as XML ({error}) - leaving it to a manual merge", file=sys.stderr)
        return 2
    merged, listConflict = mergeAssets(base, ours, theirs, prefer)
    bCrlf = b"\r\n" in oursPath.read_bytes()
    writeTextInternal(outputPath, serializeXmlAsset(merged), bCrlf)
    if listConflict and prefer is None:
        print(f"[AssetMerge] {label}: {len(listConflict)} conflict(s) - search the file for 'MERGE CONFLICT':", file=sys.stderr)
        for conflict in listConflict:
            print(f"  {conflict.describe()}", file=sys.stderr)
        return 1
    resolved = f", {len(listConflict)} conflict(s) resolved as {prefer}" if listConflict else ""
    print(f"[AssetMerge] {label}: merged{resolved}")
    return 0


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="XML 에셋의 의미 비교 · 3-way 병합")
    subparsers = parser.add_subparsers(dest="command", required=True)
    diffParser = subparsers.add_parser("diff", help="두 판의 의미 비교")
    diffParser.add_argument("before", type=Path)
    diffParser.add_argument("after", type=Path)
    mergeParser = subparsers.add_parser("merge", help="3-way 병합")
    mergeParser.add_argument("base", type=Path)
    mergeParser.add_argument("ours", type=Path)
    mergeParser.add_argument("theirs", type=Path)
    mergeParser.add_argument("-o", "--out", type=Path, default=None, help="쓸 자리(기본: ours 를 덮어쓴다)")
    mergeParser.add_argument("--prefer", choices=("ours", "theirs"), default=None, help="충돌을 이쪽으로 푼다")
    gitMergeParser = subparsers.add_parser("git-merge", help="git 병합 드라이버: %%O %%A %%B %%P")
    gitMergeParser.add_argument("base", type=Path)
    gitMergeParser.add_argument("ours", type=Path)
    gitMergeParser.add_argument("theirs", type=Path)
    gitMergeParser.add_argument("path", nargs="?", default="")
    gitDiffParser = subparsers.add_parser("git-diff", help="GIT_EXTERNAL_DIFF: path old hex mode new hex mode")
    gitDiffParser.add_argument("listGitArgument", nargs="+")
    args = parser.parse_args(listArgument)

    if args.command == "diff":
        return runDiffInternal(args.before, args.after)
    if args.command == "merge":
        return runMergeInternal(args.base, args.ours, args.theirs, args.out or args.ours, args.prefer, str(args.ours))
    if args.command == "git-merge":
        return runMergeInternal(args.base, args.ours, args.theirs, args.ours, None, args.path or str(args.ours))
    # git-diff: git 은 비교가 끝나면 종료 코드를 보지 않는다 — 차이가 있어도 0 으로 끝낸다(1 이면 `git diff` 가 멈춘다).
    listGitArgument = args.listGitArgument
    if len(listGitArgument) < 5:
        print("[AssetMerge] git-diff expects: path old-file old-hex old-mode new-file ...", file=sys.stderr)
        return 2
    try:
        runDiffInternal(Path(listGitArgument[1]), Path(listGitArgument[4]), listGitArgument[0])
    except (ElementTree.ParseError, OSError, UnicodeDecodeError) as error:
        print(f"{listGitArgument[0]}: not comparable as XML ({error})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
