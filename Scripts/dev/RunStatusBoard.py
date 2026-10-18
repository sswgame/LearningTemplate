#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunStatusBoard.py
@brief 최상단 README 의 '지금 진행 상황' 표를 계획 문서 머리 줄 · 로드맵 · 후보 문서에서 만듭니다.

    py -3 -m Scripts status-board                      # 표를 찍는다
    py -3 -m Scripts status-board --write README.md    # 파일의 두 표지 사이만 바꾼다(표지가 없으면 오류)
    py -3 -m Scripts status-board --check README.md    # 사이 내용이 생성 결과와 다르면 종료 1 과 다른 줄을 보인다

표는 손으로 고치지 않는다. 입력 · 약속은 `common/StatusBoard.py` 가 원본이다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot  # noqa: E402
from common.Plans import PlanFormatError  # noqa: E402
from common.StatusBoard import StatusBoardError, buildBoard, diffBoard, replaceBoard  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="README 의 '지금 진행 상황' 표를 계획 문서 · 로드맵 · 후보 문서에서 만든다.")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--write", type=Path, metavar="FILE", help="이 파일의 표지 사이를 생성 결과로 바꾼다")
    group.add_argument("--check", type=Path, metavar="FILE", help="이 파일의 표지 사이가 생성 결과와 같은지 본다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    root = args.root or getProjectRoot()
    try:
        board = buildBoard(root)
        if args.write:
            text = args.write.read_text(encoding="utf-8")
            args.write.write_text(replaceBoard(text, board, args.write.name), encoding="utf-8")
            print(f"[status-board] {args.write} 를 갱신했습니다")
        elif args.check:
            listDiff = diffBoard(args.check.read_text(encoding="utf-8"), board, args.check.name)
            if listDiff:
                print(f"[status-board] {args.check} 의 표가 낡았습니다 — `py -3 -m Scripts status-board --write {args.check}`", file=sys.stderr)
                print("\n".join(listDiff), file=sys.stderr)
                return 1
            print("[status-board] 표가 최신입니다")
        else:
            print(board)
    except (PlanFormatError, StatusBoardError) as error:
        print(f"[status-board] 오류: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
