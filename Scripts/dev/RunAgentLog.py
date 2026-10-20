#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunAgentLog.py
@brief `docs/12_Agents.md` 의 '토큰 측정값' 표에 한 줄을 더합니다(표의 마지막 행 뒤, 파일의 줄 끝 보존).

    py -3 -m Scripts agent-log --task "인스펙터 선택기" --piece "구현 + 테스트" --agent "worker(sonnet, medium)" --tokens "약 16만" --calls 65 --note "13분"

날짜는 오늘이다. `--tokens` 는 숫자(`160000`) 또는 `약 N만`, `--calls` 는 정수다. 표를 못 찾거나 값이 틀리면 아무것도 쓰지 않고 종료 1 이다.
"""

from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Paths import getProjectRoot  # noqa: E402

kAgentsDocRelPath = "docs/12_Agents.md"
_kTableHeaderRe = re.compile(r"^\|\s*날짜\s*\|\s*일\s*\|\s*조각\s*\|")
_kTokensRe = re.compile(r"^(\d+|약 \d+(\.\d+)?만)$")


class AgentLogError(Exception):
    """값이 틀렸거나 표를 못 찾음 — 메시지는 사용자에게 그대로 보인다."""


def formatRow(date: str, task: str, piece: str, agent: str, tokens: str, calls: int, note: str) -> str:
    """표의 한 행. 칸 값에 `|` 나 줄바꿈이 있으면 표가 깨지므로 거부한다."""
    if not _kTokensRe.match(tokens):
        raise AgentLogError(f"--tokens `{tokens}` 는 숫자 또는 `약 N만` 이어야 합니다")
    if calls < 0:
        raise AgentLogError("--calls 는 0 이상입니다")
    listCell = [date, task, piece, agent, tokens, str(calls), note]
    for cell in listCell:
        if "|" in cell or "\n" in cell or "\r" in cell:
            raise AgentLogError(f"칸 값에 `|` 나 줄바꿈을 쓸 수 없습니다: {cell!r}")
    return "| " + " | ".join(listCell) + " |"


def insertRow(text: str, row: str) -> str:
    """'토큰 측정값' 표(머리 줄 `| 날짜 | 일 | 조각 |`)의 마지막 행 뒤에 `row` 를 넣는다. 줄 끝은 원본을 따른다."""
    listLine = text.splitlines(keepends=True)
    listHeader = [index for index, line in enumerate(listLine) if _kTableHeaderRe.match(line)]
    if len(listHeader) != 1:
        raise AgentLogError(f"'토큰 측정값' 표 머리 줄(`| 날짜 | 일 | 조각 |`)이 {len(listHeader)}개입니다(하나여야 합니다)")
    last = listHeader[0]
    while last + 1 < len(listLine) and listLine[last + 1].startswith("|"):
        last += 1
    if last < listHeader[0] + 1:
        raise AgentLogError("표에 구분 줄이 없습니다")
    lastLine = listLine[last]
    eol = lastLine[len(lastLine.rstrip("\r\n")):]
    if not eol:  # 파일 끝에 줄바꿈 없이 끝난 표 — 앞 줄의 줄 끝을 따른다
        previous = listLine[last - 1]
        eol = previous[len(previous.rstrip("\r\n")):] or "\n"
        listLine[last] = lastLine + eol
    listLine.insert(last + 1, row + eol)
    return "".join(listLine)


def main(argv: list[str] | None = None, today: datetime.date | None = None) -> int:
    parser = argparse.ArgumentParser(description="docs/12_Agents.md 의 '토큰 측정값' 표에 한 줄을 더한다.")
    parser.add_argument("--task", required=True, help="일 이름")
    parser.add_argument("--piece", required=True, help="조각")
    parser.add_argument("--agent", required=True, help="에이전트 종류(모델, effort)")
    parser.add_argument("--tokens", required=True, help="숫자 또는 '약 N만'")
    parser.add_argument("--calls", required=True, type=int, help="도구 호출 수")
    parser.add_argument("--note", default="", help="비고")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    docPath = (args.root or getProjectRoot()) / kAgentsDocRelPath
    try:
        row = formatRow((today or datetime.date.today()).isoformat(), args.task, args.piece, args.agent, args.tokens, args.calls, args.note)
        if not docPath.is_file():
            raise AgentLogError(f"{docPath} 가 없습니다")
        newText = insertRow(docPath.read_bytes().decode("utf-8"), row)
    except AgentLogError as error:
        print(f"[agent-log] 오류(아무것도 쓰지 않음): {error}", file=sys.stderr)
        return 1
    docPath.write_bytes(newText.encode("utf-8"))
    print(f"[agent-log] {kAgentsDocRelPath} 표 끝에 더함: {row}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
