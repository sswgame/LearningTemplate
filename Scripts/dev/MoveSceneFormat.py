#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
저장소의 씬 파일(`*.scene.xml`)을 다음 씬 형식 판으로 다시 씁니다.

왜 있는가: 엔진은 옛 판 씬을 읽는 이관기를 두지 않는다(`SceneDocument::kAssetType._formatVersion` 이 아닌 판은 읽지 않는다). 판을 올릴 때 데이터를 이 도구로
한 번에 다시 쓰고, 엔진에는 지금 판의 리더 하나만 남긴다.

판 1 → 2: 씬 루트의 `<entities>` 앞에 월드 설정(`<WorldSettings>`, 스트리밍하지 않음)을 넣고 `formatVersion` 을 2 로 올린다. 이미 2 인 파일은 그대로 둔다.
글 그대로 고친다(XML 을 다시 쓰지 않는다) — 들여쓰기 · 주석 · 줄 끝이 diff 에 섞이지 않게.

사용법:
  py -3 Scripts/dev/MoveSceneFormat.py            # Resource/ 아래의 추적되는 씬 모두
  py -3 Scripts/dev/MoveSceneFormat.py a.scene.xml b.scene.xml
  py -3 Scripts/dev/MoveSceneFormat.py --check    # 고칠 파일이 있으면 1
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from common.Paths import getProjectRoot  # noqa: E402

_kTargetVersion = 2
_kRootRe = re.compile(r'(<Scene\b[^>]*\bformatVersion=")(\d+)(")')
_kEntitiesRe = re.compile(r"^([ \t]*)<entities\b", re.MULTILINE)


def upgradeSceneText(text: str) -> str | None:
    """씬 글을 판 2 로 올린 글을 돌려줍니다. 이미 판 2 면 None 입니다. 모르는 판이면 ValueError 입니다."""
    rootMatch = _kRootRe.search(text)
    if rootMatch is None:
        raise ValueError("no <Scene formatVersion=...> root")
    version = int(rootMatch.group(2))
    if version == _kTargetVersion:
        return None
    if version != 1:
        raise ValueError(f"formatVersion {version} has no rewrite to {_kTargetVersion}")
    newline = "\r\n" if "\r\n" in text else "\n"
    entitiesMatch = _kEntitiesRe.search(text, rootMatch.end())
    if entitiesMatch is None:
        raise ValueError("no <entities> under <Scene>")
    indent = entitiesMatch.group(1)
    block = (
        f'{indent}<WorldSettings _bStreaming="false" _unloadMargin="0" _maxCellLoadsPerFrame="0">{newline}'
        f'{indent}\t<_cellGrid _cellSize="0" _loadingRadius="0" />{newline}'
        f"{indent}</WorldSettings>{newline}"
    )
    upgraded = text[: entitiesMatch.start()] + block + text[entitiesMatch.start() :]
    return _kRootRe.sub(lambda match: f"{match.group(1)}{_kTargetVersion}{match.group(3)}", upgraded, count=1)


def collectScenePaths(root: pathlib.Path) -> list[pathlib.Path]:
    """Resource/ 아래의 씬 파일을 이름순으로 돌려줍니다."""
    return sorted((root / "Resource").rglob("*.scene.xml"))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="씬 파일을 다음 씬 형식 판으로 다시 씁니다(판 1 → 2: 월드 설정).")
    parser.add_argument("paths", nargs="*", help="고칠 씬 파일(없으면 Resource/ 아래 모두)")
    parser.add_argument("--check", action="store_true", help="쓰지 않고 고칠 파일이 있으면 1 로 끝난다")
    args = parser.parse_args(argv)

    root = getProjectRoot()
    listPath = [pathlib.Path(path) for path in args.paths] if args.paths else collectScenePaths(root)
    changedCount = 0
    failedCount = 0
    for path in listPath:
        data = path.read_bytes()
        try:
            upgraded = upgradeSceneText(data.decode("utf-8"))
        except ValueError as error:
            print(f"[MoveSceneFormat] {path}: {error}")
            failedCount += 1
            continue
        if upgraded is None:
            continue
        changedCount += 1
        print(f"[MoveSceneFormat] {'would rewrite' if args.check else 'rewrote'} {path}")
        if args.check is False:
            path.write_bytes(upgraded.encode("utf-8"))
    print(f"[MoveSceneFormat] {changedCount} scene(s) {'to rewrite' if args.check else 'rewritten'}, {failedCount} failed")
    if failedCount > 0:
        return 1
    return 1 if args.check and changedCount > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
