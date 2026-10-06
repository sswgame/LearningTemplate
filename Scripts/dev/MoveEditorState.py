#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MoveEditorState.py
@brief 체크아웃마다 한 번: 에디터가 예전 자리(`Config/Editor/` · 팩 안 gv 프리셋)에 쓰던 로컬 상태를 `Saved/Editor/` 로 옮깁니다.

    py -3 Scripts/dev/MoveEditorState.py              # 이 저장소
    py -3 Scripts/dev/MoveEditorState.py --dry-run    # 옮길 것만 보인다
    py -3 Scripts/dev/MoveEditorState.py --root D:/Other/Checkout

에디터가 쓰는 상태(도킹 · 레이아웃 · 캔버스 · 테마 · gv 프리셋)는 이제 `Saved/Editor/` 에만 있다(docs/07_Configuration.md).
엔진에는 옛 자리를 읽는 길이 없다(별칭 금지) — 옮기지 않으면 그 PC 의 레이아웃 · 프리셋이 첫 실행에서 기본값으로 돌아간다.
git 이 무시하는 파일이라 pull 로는 옮겨지지 않는다 — 작업하는 PC 마다 한 번 돌린다. 이미 `Saved/Editor/` 에 같은 이름이 있으면 건드리지 않고 알린다.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot  # noqa: E402

# `Config/Editor/` 에서 옮기는 이름 — 앱이 쓰던 상태만(사람이 고치는 `Config/Editor/*.json` 도구 값 · 임포트 규칙은 그 자리에 남는다).
_kListEditorStateName = ("imgui.ini", "windows.ini", "AnimGraph.json", "DialogueGraphEditor.json", "AnimGraphData.json",
                         "DialogueGraphData.json", "SpriteClip.json", "EditorConfig.json", "Layouts")


def collectMovesInternal(root: Path) -> list[tuple[Path, Path]]:
    """옮길 (원본, 대상) 쌍 — 원본이 있는 것만."""
    sourceFolder = root / "Config/Editor"
    targetFolder = root / "Saved/Editor"
    listMove = [(sourceFolder / name, targetFolder / name) for name in _kListEditorStateName if (sourceFolder / name).exists()]
    for preset in sorted(root.glob("Resource/game/*/data/presets/globalvars/*.gvpreset.xml")):
        packName = preset.parents[3].name
        listMove.append((preset, targetFolder / "GlobalVariablePresets" / packName / preset.name))
    return listMove


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="에디터 로컬 상태를 옛 자리에서 Saved/Editor/ 로 옮긴다(체크아웃마다 한 번).")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    parser.add_argument("--dry-run", action="store_true", help="옮기지 않고 옮길 것만 보인다")
    args = parser.parse_args(argv)

    root = (args.root or getProjectRoot()).resolve()
    listMove = collectMovesInternal(root)
    if not listMove:
        print(f"[MoveEditorState] 옮길 것 없음 ({root})")
        return 0
    for source, target in listMove:
        if target.exists():
            print(f"건너뜀(이미 있음): {target.relative_to(root)}")
            continue
        print(f"{'옮길 것' if args.dry_run else '옮김'}: {source.relative_to(root)} -> {target.relative_to(root)}")
        if not args.dry_run:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(source), str(target))
    return 0


if __name__ == "__main__":
    sys.exit(main())
