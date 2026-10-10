#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/GenerateReflectionAnnotationKeys.py

편집기용 어노테이션 키 선언 헤더(`Source/Engine/Reflection/ReflectionAnnotationKeys.h`)를 씁니다.
내용은 `Scripts/common/ReflectionAnnotationKeys.py` 가 `AnnotationMeta.txt` · `ReflectUnits.h` · `PredefinedContainerKind.xxx` 에서 만듭니다.
손으로 돌리고 결과를 커밋합니다. 커밋한 파일이 원본과 다르면 `CheckReflectionAnnotationKeys` 게이트가 실패합니다.

사용법: py -3 Scripts/generate/GenerateReflectionAnnotationKeys.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ReflectionAnnotationKeys import buildAnnotationKeysFiles  # noqa: E402


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="편집기용 리플렉션 어노테이션 키 선언 헤더를 씁니다")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[2]), help="저장소 루트")
    args = parser.parse_args(argv)
    root = Path(args.root)
    for relative, data in buildAnnotationKeysFiles(root).items():
        path = root / relative
        # 체크아웃 줄끝이 다를 수 있다(core.autocrlf) — 줄끝만 다르면 그대로 둔다.
        if path.is_file() and path.read_bytes().replace(b"\r\n", b"\n") == data:
            print(f"그대로: {relative}")
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"썼습니다: {relative} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
