#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/selftest/CheckExemptionTables.py

게이트의 예외는 `Scripts/lint/rules/<게이트>.toml` 의 `[exemption]` 한 자리에만 둔다 — 기반(`LintGate`)이 읽어 `mapExemption` 을 채우고,
이유가 빈 줄 · 낡은 줄 · 표 크기를 기반이 본다.

예외 표가 게이트마다 따로(`_kAllowedReader` · `_kWiringFiles` · `_kSetAnsiAllowed` …) 있으면 모양이 갈리고, 대상이 사라져도 줄이 조용히
남는다(시계 · nodiscard 게이트 둘만 낡은 줄을 알렸다). 그래서 다음을 막는다.

  1) `gate/` 의 모듈 수준 · 클래스 수준 대입 가운데 이름이 `_k…Allow/Allowed/Exempt/Exception/Wiring/Ubiquitous/Deferred/Skipped…` 이고
     값이 표(사전 · 집합 · 튜플 · 목록 리터럴, `frozenset(…)` · `set(…)` · `dict(…)` · `tuple(…)` 호출)인 것 — `rules/` 의 `[exemption]` 으로 옮긴다.
  2) `gate/` · `selftest/` 의 클래스가 `mapExemption` 을 직접 대입한다 — 데이터 파일로 옮긴다.
  3) `rules/` 의 파일 — 읽는 스크립트(`Scripts/lint/**/<이름>.py`)가 없다(낡은 데이터), `[exemption]` 이 50 줄을 넘는다(예외가 규칙보다 많다,
     규칙을 고친다), `tomllib` 과 3.10 용 부분 집합 읽기(`common.RuleData.readTomlSubset`)의 결과가 다르다(CI 린트 잡의 파이썬은 3.10 이다).

정규식 · 앞말 문자열(`_kWiringPrefix = "wiring:"`)처럼 표가 아닌 것은 막지 않는다.

  python Scripts/lint/selftest/CheckExemptionTables.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path
from typing import Iterator

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common.RuleData import RuleDataError, kExemptionKey, kRuleDataRelDir, readTomlSubset, readTomlText  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kTableNameRe = re.compile(r"^_?k\w*(Allow|Allowed|Exempt|Exception|Wiring|Ubiquitous|Deferred|Skipped)\w*$", re.IGNORECASE)
_kTableFactoryName = frozenset({"frozenset", "set", "dict", "tuple", "list"})
_kMaxExemptionRow = 50


def isTableValueInternal(value: ast.expr | None) -> bool:
    """대입한 값이 표(컬렉션 리터럴 · 컬렉션 생성 호출)인가."""
    if isinstance(value, (ast.Dict, ast.Set, ast.Tuple, ast.List)):
        return True
    return isinstance(value, ast.Call) and isinstance(value.func, ast.Name) and value.func.id in _kTableFactoryName


def iterAssignmentsInternal(listStatement: list[ast.stmt]) -> Iterator[tuple[str, ast.expr | None, int]]:
    """대입문의 (이름, 값, 줄)을 돌려줍니다 — 모듈 수준과 클래스 몸통만."""
    for statement in listStatement:
        if isinstance(statement, ast.Assign):
            for target in statement.targets:
                if isinstance(target, ast.Name):
                    yield target.id, statement.value, statement.lineno
        elif isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name):
            yield statement.target.id, statement.value, statement.lineno


def findTableViolationsInternal(relPath: str, text: str, bGateFolder: bool) -> list[str]:
    """한 린트 파일에서 따로 든 예외 표(`gate/` 만)와 클래스에 직접 적은 `mapExemption` 을 찾습니다."""
    listViolation: list[str] = []
    tree = ast.parse(text)
    stem = relPath.rsplit("/", 1)[-1].removesuffix(".py")
    listScope: list[list[ast.stmt]] = [tree.body] + [node.body for node in tree.body if isinstance(node, ast.ClassDef)]
    for listStatement in listScope:
        for name, value, lineNumber in iterAssignmentsInternal(listStatement):
            if bGateFolder and _kTableNameRe.match(name) and isTableValueInternal(value):
                listViolation.append(f"{relPath}:{lineNumber}: 예외 표 '{name}' 를 따로 둡니다 — {kRuleDataRelDir}/{stem}.toml 의 "
                                     f"[{kExemptionKey}] 에 이유와 함께 둡니다(이유 · 낡은 줄 · 표 크기를 기반이 본다)")
            if name == "mapExemption" and listStatement is not tree.body:
                listViolation.append(f"{relPath}:{lineNumber}: mapExemption 을 클래스에 직접 적습니다 — {kRuleDataRelDir}/{stem}.toml 의 "
                                     f"[{kExemptionKey}] 에 둡니다(기반이 읽어 채운다)")
    return listViolation


def findRuleFileViolationsInternal(repositoryRoot: Path, path: Path) -> list[str]:
    """`rules/` 파일 하나 — 읽는 스크립트, 예외 표 크기, 두 읽기의 일치."""
    relPath = path.relative_to(repositoryRoot).as_posix()
    listViolation: list[str] = []
    if not any((repositoryRoot / "Scripts" / "lint").rglob(f"{path.stem}.py")):
        listViolation.append(f"{relPath}: 읽는 스크립트(Scripts/lint/**/{path.stem}.py)가 없습니다 — 옮겼으면 파일 이름을 맞추고, 지웠으면 이 파일도 지웁니다")
    try:
        text = path.read_text(encoding="utf-8")
        data = readTomlText(text, relPath)
    except (OSError, UnicodeDecodeError, RuleDataError) as exception:
        return listViolation + [f"{relPath}: {exception}"]
    exemption = data.get(kExemptionKey, {})
    if isinstance(exemption, dict) and len(exemption) > _kMaxExemptionRow:
        listViolation.append(f"{relPath}: [{kExemptionKey}] 이 {len(exemption)} 줄입니다(> {_kMaxExemptionRow}) — 예외가 규칙보다 많다, 규칙을 고칩니다")
    try:
        subset = readTomlSubset(text, relPath)
    except RuleDataError as exception:
        return listViolation + [f"{exception} — 파이썬 3.10(CI 린트 잡)에서 읽히지 않는다, common/RuleData.py 머리말의 부분 집합으로 씁니다"]
    if subset != data:
        listViolation.append(f"{relPath}: tomllib 과 부분 집합 읽기(readTomlSubset)의 결과가 다릅니다 — 파이썬 3.10(CI 린트 잡)에서 다르게 읽힌다")
    return listViolation


class CheckExemptionTablesGate(LintGate):
    """린트를 보는 린트 — 대상은 `Scripts/lint/gate/` 다."""

    description = "게이트의 예외가 rules/<게이트>.toml 한 자리에만 있는지(따로 든 허용 표 · 클래스의 mapExemption · 너무 큰 표 · 낡은 데이터 파일) 검사"
    buildComment = "Checking that gate exemptions live in Scripts/lint/rules..."
    timeoutSeconds = 30
    violationHeader = "rules/ 밖의 예외 표 · 규칙 데이터 문제"
    selfTestSkipReason = "린트를 보는 린트 — 조각을 들지 않는다(대상이 린트 폴더 자체다)"
    preCommitSkipReason = "셀프테스트는 훅에서 돌지 않는다(ctest -L lint · CI)"

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        lintDir = repositoryRoot / "Scripts" / "lint"
        listPath = sorted((lintDir / "gate").glob("*.py"))
        if not listPath:
            raise GateError("Scripts/lint/gate/ 에 게이트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")
        listViolation: list[str] = []
        for path in listPath + sorted((lintDir / "selftest").glob("*.py")):
            relPath = path.relative_to(repositoryRoot).as_posix()
            listViolation.extend(findTableViolationsInternal(relPath, path.read_text(encoding="utf-8"), path.parent.name == "gate"))
        listRulePath = sorted((repositoryRoot / kRuleDataRelDir).glob("*.toml"))
        listOther = [path for path in (repositoryRoot / kRuleDataRelDir).glob("*") if path.suffix != ".toml"]
        listViolation += [f"{path.relative_to(repositoryRoot).as_posix()}: {kRuleDataRelDir}/ 에는 .toml 만 둡니다" for path in listOther]
        for path in listRulePath:
            listViolation.extend(findRuleFileViolationsInternal(repositoryRoot, path))
        return GateResult(listViolation=listViolation, summary=f"gate/ {len(listPath)} files · rules/ {len(listRulePath)} files")


main = CheckExemptionTablesGate.run

if __name__ == "__main__":
    sys.exit(main())
