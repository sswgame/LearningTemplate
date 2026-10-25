#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunDoc.py
@brief 큰 마크다운 문서를 통째로 읽지 않고 목차(제목 · 행 번호 · 절 크기)와 한 절만 봅니다(고치지 않는다).

    py -3 -m Scripts doc 09_Decisions                       # docs/09_Decisions.md 의 목차
    py -3 -m Scripts doc 09_Decisions --section 5-4         # 결정 기록 5-4 영역 파일(docs/decisions/5-4_*.md) 전체. 다른 문서는 번호가 5-4 인 절만
    py -3 -m Scripts doc docs/plans/Vfx.md --section V2     # 저장소 기준 경로도 된다. 제목 일부로 맞춘다

이유(2026-10-19 토큰 절감): 290KB 문서를 통째로 읽는 일을 규칙이 아니라 호출 형식으로 막는다(`.claude/hooks/ReadGuard.py`). 절을 자르는 구현은
`common/MarkdownSections.py` 하나다(`plans --file --section` 과 공용). 절이 없거나 여럿에 맞으면 후보를 보이고 종료 1 이다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Paths import getProjectRoot  # noqa: E402
from common.MarkdownSections import extractSection, formatOutline  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402

kListSearchDir = ("", "docs", "docs/plans", "docs/decisions")

#: 결정 기록 5절의 영역 번호(`5-4`) — 본문은 `docs/decisions/5-4_*.md` 에 있고 `09_Decisions.md` 에는 가리킴만 남는다.
kDecisionIndexName = "09_Decisions.md"
kDecisionAreaRe = re.compile(r"^5-\d+$")


def resolveDecisionArea(path: Path, section: str | None) -> Path | None:
    """`09_Decisions --section 5-4` 를 영역 파일로 돌린다. 해당이 없으면 None."""
    if path.name != kDecisionIndexName or section is None or kDecisionAreaRe.match(section) is None:
        return None
    listFile = sorted((path.parent / "decisions").glob(f"{section}_*.md"))
    return listFile[0] if len(listFile) == 1 else None


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
        return ExitCode.Violation
    areaPath = resolveDecisionArea(path, args.section)
    if areaPath is not None:
        print(f"[doc] {areaPath.relative_to(root).as_posix()} 전체", file=sys.stderr)
        print(areaPath.read_text(encoding="utf-8"), end="")
        return ExitCode.Passed
    listLine = path.read_text(encoding="utf-8").splitlines()
    if not args.section:
        print(formatOutline(listLine))
        return ExitCode.Passed
    try:
        print("\n".join(extractSection(listLine, args.section)))
    except ValueError as error:
        print(f"[doc] 오류: {error}", file=sys.stderr)
        return ExitCode.Violation
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
