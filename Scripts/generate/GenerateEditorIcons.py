#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/GenerateEditorIcons.py

에디터 아이콘 폰트(`Resource/editor/fonts/sweditoricons.ttf`)와 글리프 상수 헤더(`Source/Editor/Common/GUI/EditorIconGlyphs.h`)를 씁니다.
아이콘 그림과 폰트 형식은 `Scripts/common/EditorIconFont.py` 에 있습니다. 손으로 돌리고 결과를 커밋합니다.
커밋한 파일이 그림과 다르면 `CheckEditorIcons` 게이트가 실패합니다.

사용법: py -3 Scripts/generate/GenerateEditorIcons.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.EditorIconFont import buildEditorIconFiles, kListIcon  # noqa: E402


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="에디터 아이콘 폰트와 글리프 헤더를 씁니다")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="저장소 루트")
    args = parser.parse_args(argv)
    root = Path(args.root)
    for relative, data in buildEditorIconFiles().items():
        path = root / relative
        # 헤더는 체크아웃 줄끝이 다를 수 있다(core.autocrlf) — 줄끝만 다르면 그대로 둔다.
        if path.is_file() and path.read_bytes().replace(b"\r\n", b"\n") == data.replace(b"\r\n", b"\n"):
            print(f"그대로: {relative}")
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"썼습니다: {relative} ({len(data)} bytes)")
    print(f"아이콘 {len(kListIcon)}개")
    return 0


if __name__ == "__main__":
    sys.exit(main())
