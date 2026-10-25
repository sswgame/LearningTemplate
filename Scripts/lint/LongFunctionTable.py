# -*- coding: utf-8 -*-
"""
Scripts/lint/LongFunctionTable.py

`docs/plans/LongFunctions.md` 3절 표가 지금 코드와 맞는지 봅니다. `plans --check` 가 부릅니다.

[왜 필요한가]
표를 손으로 적은 수치(줄 수)로 두었더니 옛 수치가 남아, 이미 나뉜 함수를 작업자가 다시 맡았다. 그래서 표에는 줄 수를 적지 않는다
(고칠 때마다 어긋난다 - 줄 수는 `RunFunctionSize.py` 가 그때그때 보인다). 표가 맡는 것은 사람이 정한 판정(유지 / 나눔과 이유)이고, 이 점검이
"본문 100 줄 초과 함수 집합" 과 표의 행 집합이 같은지를 본다.
- 코드에 있는데 표에 없다 → 판정 없는 긴 함수(새로 길어졌거나 표가 빠뜨렸다).
- 표에 있는데 코드에 없다 → 이미 나뉘었거나 이름이 바뀌었다(행을 지운다).
- 판정 칸이 `유지` · `나눔` 으로 시작하지 않는다.

표 형식: `| \\`파일\\` | \\`함수\\` | 판정 |` (3절 안, 파일과 함수는 백틱). 함수 이름은 `RunFunctionSize.py` 가 내는 이름 그대로다.
"""

from __future__ import annotations

import re
from pathlib import Path

from lint.FunctionSizeScan import collectLongFunctions, kDefaultMinLines, kDefaultRoots

kDocRelPath = "docs/plans/LongFunctions.md"
_kSectionHeading = "## 3. 목록"
_kRowRe = re.compile(r"^\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|\s*(.*?)\s*\|\s*$")
_kVerdictPrefixes = ("유지", "나눔")


def readTableRows(text: str) -> list[tuple[int, str, str, str]]:
    """3절 표의 행을 (줄 번호, 파일, 함수, 판정) 으로 읽습니다. 3절이 없으면 빈 목록입니다."""
    listRow: list[tuple[int, str, str, str]] = []
    bInSection = False
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        if line.startswith("## "):
            bInSection = line.startswith(_kSectionHeading)
            continue
        if not bInSection:
            continue
        match = _kRowRe.match(line)
        if match:
            listRow.append((lineNumber, match.group(1), match.group(2), match.group(3)))
    return listRow


def checkLongFunctionTable(repositoryRoot: Path) -> list[str]:
    """표와 코드의 어긋남을 `파일:줄: 내용` 줄 목록으로 돌려줍니다(없으면 빈 목록)."""
    docPath = repositoryRoot / kDocRelPath
    if not docPath.is_file():
        return [f"{kDocRelPath}: 파일이 없습니다"]
    listRow = readTableRows(docPath.read_text(encoding="utf-8"))
    listLong, _listUnbalanced, _fileCount = collectLongFunctions(repositoryRoot, kDefaultMinLines, kDefaultRoots)
    setCurrent = {(function.path, function.name) for function in listLong}
    mapRowKey: dict[tuple[str, str], int] = {}
    listViolation: list[str] = []
    for lineNumber, path, name, verdict in listRow:
        mapRowKey.setdefault((path, name), lineNumber)
        if not verdict.startswith(_kVerdictPrefixes):
            listViolation.append(f"{kDocRelPath}:{lineNumber}: `{name}` 의 판정이 `유지` · `나눔` 으로 시작하지 않습니다")
    for (path, name), lineNumber in mapRowKey.items():
        if (path, name) not in setCurrent:
            listViolation.append(f"{kDocRelPath}:{lineNumber}: `{name}` ({path}) 은 이제 본문 {kDefaultMinLines} 줄 이하이거나 없습니다 - 행을 지웁니다")
    for function in listLong:
        if (function.path, function.name) not in mapRowKey:
            listViolation.append(f"{kDocRelPath}: 표에 없는 긴 함수 `{function.name}` ({function.path}:{function.line}, {function.bodyLines} 줄) - 판정 행을 더합니다")
    return listViolation
