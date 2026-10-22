#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/GenerateShapingTestFont.py

복합 문자 셰이핑 테스트용 글꼴(`Test/EngineTest/Text/Fonts/swshapingtest.ttf`)을 씁니다. 글꼴 내용은 `Scripts/common/ShapingTestFont.py` 에 있습니다.
손으로 돌리고 결과를 커밋합니다. 표준 라이브러리만 씁니다.

사용법: py -3 Scripts/generate/GenerateShapingTestFont.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ShapingTestFont import buildShapingTestFont  # noqa: E402

kFontRelativePath = "Test/EngineTest/Text/Fonts/swshapingtest.ttf"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="셰이핑 테스트용 글꼴을 씁니다")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="저장소 루트")
    args = parser.parse_args(argv)
    path = Path(args.root) / kFontRelativePath
    data = buildShapingTestFont()
    if path.is_file() and path.read_bytes() == data:
        print(f"그대로: {kFontRelativePath}")
        return 0
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    print(f"썼습니다: {kFontRelativePath} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
