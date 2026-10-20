"""
`Scripts/` 의 파이썬 파일 목록과 한 줄 설명 — `py -3 -m Scripts --list` 가 찍는다.

설명은 파일 docstring 의 첫 내용 줄이다(파일 경로 · `@file` 줄은 건너뛰고 `@brief ` 는 뗀다). AST 로 읽고 import 하지 않는다 — 목록을 보는 데
명령 모듈을 싣는 비용 · 부작용이 없다. 손으로 쓴 파일 목록 문서는 두지 않는다: 코드에서 만든 목록은 낡지 않는다(`docs/plans/ScriptsStructure.md`).
"""

from __future__ import annotations

import ast
import re
from dataclasses import dataclass
from pathlib import Path

#: docstring 의 첫 줄이 파일 경로(`Scripts/lint/PreCommitLint.py`)이거나 `@file` 이면 설명이 아니다.
_kPathLineRe = re.compile(r"^(?:@file\s+)?(?:Scripts/)?[\w/]+\.py$")


@dataclass(frozen=True)
class ScriptSummary:
    """파일 하나 — `Scripts/` 기준 경로, 점 이름, docstring 첫 줄."""

    relPath: str
    moduleName: str
    summary: str


def summarizeDocstring(docstring: str | None) -> str:
    """docstring 의 첫 내용 줄. 없으면 빈 글."""
    for line in (docstring or "").splitlines():
        text = line.strip()
        if not text or _kPathLineRe.match(text):
            continue
        return text.removeprefix("@brief ").strip()
    return ""


def readScriptSummaries(scriptRoot: Path, folder: str = "") -> list[ScriptSummary]:
    """`scriptRoot`(또는 그 아래 `folder`)의 `.py` 마다 한 줄. 경로 순서. 파싱할 수 없는 파일은 SyntaxError 로 멈춘다."""
    base = scriptRoot / folder if folder else scriptRoot
    if not base.is_dir():
        raise FileNotFoundError(f"Scripts 아래 폴더가 아닙니다: {folder}")
    listSummary: list[ScriptSummary] = []
    for path in sorted(base.rglob("*.py")):
        relative = path.relative_to(scriptRoot)
        if "__pycache__" in relative.parts:
            continue
        listPart = list(relative.with_suffix("").parts)
        if listPart[-1] == "__init__" and len(listPart) > 1:
            listPart.pop()
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        listSummary.append(ScriptSummary(relPath=relative.as_posix(), moduleName=".".join(listPart), summary=summarizeDocstring(ast.get_docstring(tree))))
    return listSummary
