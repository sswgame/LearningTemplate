#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/selftest/CheckLintScanRoots.py

게이트가 대상 뿌리를 스스로 조립하지 않는지 본다 — 우리 C++ 의 뿌리는 `common.kLintTargetRelDirs` 하나다.

뿌리를 게이트마다 따로 적으면 조합이 갈린다(일곱 조합이 있었고, `Tools/OnlineLoadBot` 은 시계 게이트 하나만 보고 있었다). 그래서
`gate/` 의 파이썬 소스에서 다음 둘을 막는다.

  1) 글자 리터럴만 든 튜플 · 집합 · 목록이 뿌리 폴더 이름(`Source` · `Test` · `Tools` · `Tools/…` · `cmake` · `ThirdParty`)으로만 이뤄졌고
     `"Source"` 와 함께 `"Test"` · `"Tools"` · `"Tools/…"` 를 든다 — `kLintTargetRelDirs`(또는 `kLintProductRelDirs` · `kLintTargetBaseDirNames`)를 쓴다.
  2) 폴더 제외 집합(`|` 로 합치는 집합 리터럴)에 `"Tools"` 를 넣는다 — 내려받은 도구는 `kNotOurDirNames` 가 이미 빼고, 우리 도구가
     그 아래 있다(`kNotOurCodeDirNames`).

`Source` 의 일부만 보는 규칙(`"Source/GameFramework"` …)과 `("Source",)` 하나, 문서 경로처럼 `Scripts` · `Config` 까지 든 최상위 이름 표는
규칙이 그 자리의 것이라 막지 않는다.

  python Scripts/lint/selftest/CheckLintScanRoots.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import ast
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 뿌리 튜플을 이루는 이름 — 이것 말고 다른 낱말이 들면 뿌리 표가 아니라 다른 표다(문서 경로의 최상위 이름 등).
_kRootVocabulary = frozenset({"Source", "Test", "Tools", "cmake", "ThirdParty"})


def isRootNameInternal(value: str) -> bool:
    """뿌리 폴더 이름(또는 `Tools/<도구>`)인가."""
    return value in _kRootVocabulary or value.startswith("Tools/")


def findRootLiteralsInternal(relPath: str, text: str) -> list[str]:
    """한 게이트 파일에서 뿌리를 손으로 조립한 리터럴을 찾습니다."""
    listViolation: list[str] = []
    tree = ast.parse(text)
    for node in ast.walk(tree):
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
            for operand in (node.left, node.right):
                if isinstance(operand, ast.Set) and any(isinstance(element, ast.Constant) and element.value == "Tools" for element in operand.elts):
                    listViolation.append(f"{relPath}:{operand.lineno}: 'Tools' 폴더를 통째로 뺍니다 — 우리 도구가 그 아래 있습니다(common.kNotOurCodeDirNames)")
        if isinstance(node, (ast.Tuple, ast.Set, ast.List)) is False:
            continue
        listValue = [element.value for element in node.elts if isinstance(element, ast.Constant) and isinstance(element.value, str)]
        if len(listValue) != len(node.elts) or not listValue or not all(isRootNameInternal(value) for value in listValue):
            continue
        bHasSource = "Source" in listValue
        bHasOther = any(value == "Test" or value == "Tools" or value.startswith("Tools/") for value in listValue)
        if bHasSource and bHasOther:
            listViolation.append(f"{relPath}:{node.lineno}: 대상 뿌리를 직접 적었습니다 {tuple(listValue)} — common.kLintTargetRelDirs 를 쓰세요")
    return listViolation


class CheckLintScanRootsGate(LintGate):
    """린트를 보는 린트 — 대상은 `Scripts/lint/gate/` 다."""

    description = "게이트가 대상 뿌리를 손으로 조립하지 않는지(kLintTargetRelDirs 하나) 검사"
    buildComment = "Checking that gates take their scan roots from kLintTargetRelDirs..."
    timeoutSeconds = 30
    violationHeader = "손으로 조립한 대상 뿌리"
    selfTestSkipReason = "린트를 보는 린트 — 조각을 들지 않는다(대상이 린트 폴더 자체다)"
    preCommitSkipReason = "셀프테스트는 훅에서 돌지 않는다(ctest -L lint · CI)"

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = sorted((repositoryRoot / "Scripts" / "lint" / "gate").glob("*.py"))
        if not listPath:
            raise GateError("Scripts/lint/gate/ 에 게이트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")
        listViolation: list[str] = []
        for path in listPath:
            relPath = path.relative_to(repositoryRoot).as_posix()
            listViolation.extend(findRootLiteralsInternal(relPath, path.read_text(encoding="utf-8")))
        return GateResult(listViolation=listViolation, summary=f"gate/ {len(listPath)} files")


main = CheckLintScanRootsGate.run

if __name__ == "__main__":
    sys.exit(main())
