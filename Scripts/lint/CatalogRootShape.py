#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CatalogRootShape.py

게임 데이터 루트의 원소를 손으로 고르는 모양을 막는다. 2026-11-07 — 카탈로그 68 곳이 저마다 고르면서 대소문자 판정 · 모르는 원소 처리(오류 · 경고 · 말없이 넘김) · 로그 id 가 갈렸다.
**왜 형식이 아니라 린트인가.** 길은 형식으로 냈다(`XMLCatalogElement` · `XMLCatalogReadResult`). `XMLNode::findChild` 는 어디서든 부를 수 있어 다른 길을 형식으로 막지 못한다.

1. `XMLReadUtil::isElement` 를 부르지 않는다 — 대소문자를 무시해 표의 철자 판정과 갈린다.
2. `for ( XMLNode x = root.findChild(...` 로 루트 자식을 돌지 않는다 — `XMLCatalogReadResult` 를 지난다. 까닭이 있는 파일은 `Config/Rules/Lint/CatalogRootLoopFiles.json` 에 이유와 함께 적는다.

`py -3 -m Scripts lint catalog-root [--files …|--all|--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 staged 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import functools
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.CppShape import isTarget, stripCode  # noqa: E402
from lint.LintScope import LintScope, makeFileScope, makeWholeScope  # noqa: E402
from lint.Parallel import flatMapInProcesses  # noqa: E402


kExceptionFile = "Config/Rules/Lint/CatalogRootLoopFiles.json"
kScope = ("Source/GameFramework/", "Source/Games/")
kRootLoop = re.compile(r"\bfor\s*\(\s*XMLNode\s+\w+\s*=\s*root\.findChild\(")
kIsElement = re.compile(r"\bXMLReadUtil::isElement\(")


def inspectText(text: str, relative: str, setException: frozenset[str]) -> list[str]:
    """글 하나를 봅니다(자체 시험이 파일 없이 부를 수 있게 나눴다)."""
    listViolation: list[str] = []
    for index, line in enumerate(stripCode(text).split("\n")):
        if kIsElement.search(line):
            listViolation.append(f"{relative}:{index + 1}: `XMLReadUtil::isElement` 는 대소문자를 무시한다 — 루트면 `XMLCatalogElement` 표, 안쪽 원소면 `StringUtil::equals` 를 쓴다")
        if kRootLoop.search(line) and relative not in setException:
            listViolation.append(f"{relative}:{index + 1}: 루트 자식을 손으로 돈다 — `XMLCatalogReadResult` 를 지난다(까닭이 있으면 {kExceptionFile} 에 이유와 함께 적는다)")
    return listViolation


def inspectListedFile(projectRoot: Path, setException: frozenset[str], file: str) -> list[str]:
    """파일 하나를 봅니다. 대상이 아니면 빈 목록입니다(프로세스로 넘기므로 모듈 최상위에 둔다). 테스트는 보지 않습니다."""
    path = Path(file).resolve()
    try:
        relative = path.relative_to(projectRoot).as_posix()
    except ValueError:
        return []
    if not isTarget(relative) or not relative.startswith(kScope) or "/Tests/" in relative or not path.exists():
        return []
    # 표를 돌리는 쪽 자신은 손으로 돈다.
    if relative.endswith("GameData/XMLCatalogTable.h"):
        return []
    return inspectText(path.read_text(encoding="utf-8", errors="replace"), relative, setException)


def loadExceptions(projectRoot: Path) -> frozenset[str]:
    """손으로 도는 것을 허용한 파일 목록입니다. 없으면 예외로 멈춘다(빈 목록으로 대신하지 않는다)."""
    data = json.loads((projectRoot / kExceptionFile).read_text(encoding="utf-8"))
    return frozenset(entry["_path"] for entry in data["_listFile"])


def checkCatalogRootShape(projectRoot: Path, scope: LintScope) -> list[str]:
    """@p listFile 가운데 대상 C++ 파일을 보고 어긋남을 `경로:줄: 설명` 으로 돌려준다."""
    listFile = scope.listAbsolute(projectRoot, (".h", ".cpp"))
    return flatMapInProcesses(functools.partial(inspectListedFile, projectRoot, loadExceptions(projectRoot)), listFile)


def selfCheck() -> None:
    """규칙이 잡을 것을 잡고 놓아 줄 것을 놓는지 봅니다."""
    kNone: frozenset[str] = frozenset()
    loop = 'uint32 A::loadRoot( const XMLNode& root )\n{\n    for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )\n    {\n    }\n}\n'
    assert len(inspectText(loop, "x.cpp", kNone)) == 1
    assert inspectText(loop, "x.cpp", frozenset({"x.cpp"})) == []
    named = 'for ( XMLNode node = root.findChild( "Item" ); node; node = node.findNextSibling( "Item" ) )\n'
    assert len(inspectText(named, "x.cpp", kNone)) == 1
    inner = 'for ( XMLNode child = node.findChild( "Key" ); child; child = child.findNextSibling( "Key" ) )\n'
    assert inspectText(inner, "x.cpp", kNone) == []
    chain = 'if ( XMLReadUtil::isElement( name, "Ghost" ) )\n    return true;\n'
    assert len(inspectText(chain, "x.cpp", kNone)) == 1
    assert len(inspectText(chain, "x.cpp", frozenset({"x.cpp"}))) == 1
    assert inspectText("// for ( XMLNode node = root.findChild(); node; )\n", "x.cpp", kNone) == []


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="게임 데이터 루트의 원소를 손으로 고르는 모양을 본다")
    parser.add_argument("--files", nargs="*", default=None, help="볼 파일(없으면 Source 전체)")
    parser.add_argument("--all", action="store_true", help="Source 전체")
    parser.add_argument("--self-check", action="store_true", help="규칙 자체를 시험한다")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        selfCheck()
        print("[CatalogRootShape] self-check OK")
        return 0
    projectRoot = Path(__file__).resolve().parents[2]
    listViolation = checkCatalogRootShape(projectRoot, makeFileScope(projectRoot, args.files) if args.files else makeWholeScope(projectRoot))
    for line in listViolation:
        print(line)
    print(f"[CatalogRootShape] {'FAIL' if listViolation else 'OK'} — {len(listViolation)}건")
    return 1 if listViolation else 0


if __name__ == "__main__":
    sys.exit(main())
