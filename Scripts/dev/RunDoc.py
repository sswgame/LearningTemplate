#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunDoc.py
@brief 큰 마크다운 문서를 통째로 읽지 않고 목차(제목 · 행 번호 · 절 크기)와 한 절만 봅니다(고치지 않는다).

    py -3 -m Scripts doc 09_Decisions                       # docs/09_Decisions.md 의 목차
    py -3 -m Scripts doc 09_Decisions --section 5-4         # 번호가 5-4 인 절의 본문만(하위 절 포함)
    py -3 -m Scripts doc docs/plans/Vfx.md --section V2     # 저장소 기준 경로도 된다. 제목 일부로 맞춘다

이유(2026-10-19 토큰 절감): 290KB 문서를 통째로 읽는 일을 규칙이 아니라 호출 형식으로 막는다(`.claude/hooks/ReadGuard.py`). 절을 자르는 구현은
`common/MarkdownSections.py` 하나다(`plans --file --section` 과 공용). 절이 없거나 여럿에 맞으면 후보를 보이고 종료 1 이다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot  # noqa: E402
from common.MarkdownSections import extractSection, formatOutline  # noqa: E402

kListSearchDir = ("", "docs", "docs/plans")


def resolveDoc(root: Path, name: str) -> Path | None:
    """저장소 기준 경로, 또는 docs/ · docs/plans/ 아래 이름(`.md` 생략 가능). 없으면 None."""
    for directory in kListSearchDir:
        for candidate in (name, name + ".md"):
            path = root / directory / candidate
            if path.is_file():
                return path
    return None


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="마크다운 문서의 목차(제목 · 행 번호 · 절 크기)나 한 절만 보인다.")
    parser.add_argument("file", help="docs/ 아래 이름(09_Decisions) 또는 저장소 기준 경로")
    parser.add_argument("--section", metavar="TITLE", help="제목에 TITLE 이 든 절의 본문만(5-4 같은 번호도 된다). 없거나 여럿이면 후보와 종료 1")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    root = args.root or getProjectRoot()
    path = resolveDoc(root, args.file)
    if path is None:
        print(f"[doc] 오류: `{args.file}` 문서가 없습니다(저장소 기준 경로 또는 docs/ 아래 이름)", file=sys.stderr)
        return 1
    listLine = path.read_text(encoding="utf-8").splitlines()
    if not args.section:
        print(formatOutline(listLine))
        return 0
    try:
        print("\n".join(extractSection(listLine, args.section)))
    except ValueError as error:
        print(f"[doc] 오류: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
