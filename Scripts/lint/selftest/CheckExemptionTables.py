#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/selftest/CheckExemptionTables.py

게이트의 예외는 `LintGate.mapExemption` 한 자리에만 둔다 — 이유가 빈 줄 · 낡은 줄 · 표 크기를 기반이 본다.

예외 표가 게이트마다 따로(`_kAllowedReader` · `_kWiringFiles` · `_kSetAnsiAllowed` …) 있으면 모양이 갈리고, 대상이 사라져도 줄이 조용히
남는다(시계 · nodiscard 게이트 둘만 낡은 줄을 알렸다). 그래서 `gate/` 의 파이썬 소스에서 다음 둘을 막는다.

  1) 모듈 수준 · 클래스 수준 대입 가운데 이름이 `_k…Allow/Allowed/Exempt/Exception/Wiring/Ubiquitous/Deferred/Skipped…` 이고 값이 표
     (사전 · 집합 · 튜플 · 목록 리터럴, `frozenset(…)` · `set(…)` · `dict(…)` · `tuple(…)` 호출)인 것 — `mapExemption` 으로 옮긴다.
  2) 게이트 클래스의 `mapExemption` 이 50 줄을 넘는다 — 예외가 규칙보다 많다, 규칙을 고친다.

정규식 · 앞말 문자열(`_kWiringPrefix = "wiring:"`)처럼 표가 아닌 것은 막지 않는다.

  python Scripts/lint/selftest/CheckExemptionTables.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kTableNameRe = re.compile(r"^_?k\w*(Allow|Allowed|Exempt|Exception|Wiring|Ubiquitous|Deferred|Skipped)\w*$", re.IGNORECASE)
_kTableFactoryName = frozenset({"frozenset", "set", "dict", "tuple", "list"})
_kMaxExemptionRow = 50


def isTableValueInternal(value: ast.expr | None) -> bool:
    """대입한 값이 표(컬렉션 리터럴 · 컬렉션 생성 호출)인가."""
    if isinstance(value, (ast.Dict, ast.Set, ast.Tuple, ast.List)):
        return True
    return isinstance(value, ast.Call) and isinstance(value.func, ast.Name) and value.func.id in _kTableFactoryName


def iterAssignmentsInternal(listStatement: list[ast.stmt]):
    """대입문의 (이름, 값, 줄)을 돌려줍니다 — 모듈 수준과 클래스 몸통만."""
    for statement in listStatement:
        if isinstance(statement, ast.Assign):
            for target in statement.targets:
                if isinstance(target, ast.Name):
                    yield target.id, statement.value, statement.lineno
        elif isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name):
            yield statement.target.id, statement.value, statement.lineno


def findTableViolationsInternal(relPath: str, text: str) -> list[str]:
    """한 게이트 파일에서 따로 든 예외 표와 너무 큰 `mapExemption` 을 찾습니다."""
    listViolation: list[str] = []
    tree = ast.parse(text)
    listScope: list[list[ast.stmt]] = [tree.body] + [node.body for node in tree.body if isinstance(node, ast.ClassDef)]
    for listStatement in listScope:
        for name, value, lineNumber in iterAssignmentsInternal(listStatement):
            if _kTableNameRe.match(name) and isTableValueInternal(value):
                listViolation.append(f"{relPath}:{lineNumber}: 예외 표 '{name}' 를 따로 둡니다 — LintGate.mapExemption 에 이유와 함께 둡니다"
                                     f"(이유 · 낡은 줄 · 표 크기를 기반이 본다)")
            if name == "mapExemption" and isinstance(value, ast.Dict) and len(value.keys) > _kMaxExemptionRow:
                listViolation.append(f"{relPath}:{lineNumber}: mapExemption 이 {len(value.keys)} 줄입니다(> {_kMaxExemptionRow}) — "
                                     f"예외가 규칙보다 많다, 규칙을 고칩니다")
    return listViolation


class CheckExemptionTablesGate(LintGate):
    """린트를 보는 린트 — 대상은 `Scripts/lint/gate/` 다."""

    description = "게이트의 예외가 LintGate.mapExemption 한 자리에만 있는지(따로 든 허용 표 · 너무 큰 표) 검사"
    buildComment = "Checking that gate exemptions live in LintGate.mapExemption..."
    timeoutSeconds = 30
    violationHeader = "mapExemption 밖의 예외 표"
    selfTestSkipReason = "린트를 보는 린트 — 조각을 들지 않는다(대상이 린트 폴더 자체다)"
    preCommitSkipReason = "셀프테스트는 훅에서 돌지 않는다(ctest -L lint · CI)"

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = sorted((repositoryRoot / "Scripts" / "lint" / "gate").glob("*.py"))
        if not listPath:
            raise GateError("Scripts/lint/gate/ 에 게이트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")
        listViolation: list[str] = []
        for path in listPath:
            relPath = path.relative_to(repositoryRoot).as_posix()
            listViolation.extend(findTableViolationsInternal(relPath, path.read_text(encoding="utf-8")))
        return GateResult(listViolation=listViolation, summary=f"gate/ {len(listPath)} files")


main = CheckExemptionTablesGate.run

if __name__ == "__main__":
    sys.exit(main())
