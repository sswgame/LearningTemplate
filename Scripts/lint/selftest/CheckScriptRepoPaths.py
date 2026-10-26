#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/selftest/CheckScriptRepoPaths.py

스크립트 상수에 적은 저장소 경로가 **실제 파일 · 폴더를 가리키는지** 본다.

경로는 문자열이라 형식으로 막을 수 없고, 파일을 옮기면 게이트의 상수만 조용히 낡는다(`_kThemeSourcePrefix = "Source/Editor/Common/GUI/EditorThemeTokens."` 는
파일이 `Common/Config/` 로 옮겨 간 뒤에도 남아, 그 파일이 검사 대상으로 되돌아왔다). 예외 표는 기반(`LintGate`)이 낡은 줄을 알리지만 표가 아닌 상수
(`_kMathHome` · `_kLowPchRelative` · `kHeaderRelativePath`)는 아무도 안 본다. 그래서 `Scripts/lint/gate/*.py` · `Scripts/common/*.py` 의 모듈 · 클래스 수준
`k…` 상수 가운데 값(글자, 또는 글자만 든 튜플 · 목록 · 집합 · `frozenset(…)`)이 `Source/` · `Test/` · `Resource/` 로 시작하는 경로를 한다:

  - `/` 로 끝나면 그 폴더가 있어야 한다.
  - `.` · `_` · `-` 로 끝나면 파일 이름의 앞머리다 — 그 폴더에 그 앞머리로 시작하는 항목이 있어야 한다.
  - `*` · `?` · `[` 가 든 글롭은 하나 이상 맞아야 한다.
  - 그 밖에는 그 파일 · 폴더가 있어야 한다.

`{}` · 공백 · `:` 가 든 글자(서식 · 문장)는 경로가 아니라 건너뛴다. 임시 저장소(`.git` 이 없는 셀프테스트 루트)에서도 같은 규칙이다.

  python Scripts/lint/selftest/CheckScriptRepoPaths.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402

_kRepoRoot = ("Source/", "Test/", "Resource/")
_kConstantNameRe = re.compile(r"^_?k[A-Z]\w*$")
_kNotPathRe = re.compile(r"[\s:{}]")
_kGlobRe = re.compile(r"[*?\[]")
_kPrefixTail = (".", "_", "-")
_kSetFactoryName = frozenset({"frozenset", "set", "tuple", "list"})


def iterPathLiteralsInternal(value: ast.expr | None) -> list[str]:
    """상수 값에서 글자를 꺼냅니다 — 글자 하나, 또는 글자만 든 컬렉션 리터럴 · 컬렉션 생성 호출(원소 하나라도 글자가 아니면 빈 목록)."""
    if isinstance(value, ast.Call) and isinstance(value.func, ast.Name) and value.func.id in _kSetFactoryName and len(value.args) == 1:
        value = value.args[0]
    if isinstance(value, ast.Constant) and isinstance(value.value, str):
        return [value.value]
    if isinstance(value, (ast.Tuple, ast.List, ast.Set)):
        listText = [element.value for element in value.elts if isinstance(element, ast.Constant) and isinstance(element.value, str)]
        return listText if len(listText) == len(value.elts) else []
    return []


def isMissingPathInternal(repositoryRoot: Path, text: str) -> bool:
    """`text` 가 가리키는 파일 · 폴더 · 앞머리 · 글롭이 저장소에 없는가."""
    if _kGlobRe.search(text):
        return next(repositoryRoot.glob(text), None) is None
    if text.endswith("/"):
        return not (repositoryRoot / text).is_dir()
    if text.endswith(_kPrefixTail):
        parent, _, head = text.rpartition("/")
        folder = repositoryRoot / parent
        return not folder.is_dir() or not any(entry.name.startswith(head) for entry in folder.iterdir())
    return not (repositoryRoot / text).exists()


def findStalePathsInternal(repositoryRoot: Path, relPath: str, text: str) -> list[str]:
    """한 스크립트 파일에서 없는 저장소 경로를 든 상수를 찾습니다."""
    listViolation: list[str] = []
    tree = ast.parse(text)
    for listStatement in [tree.body] + [node.body for node in tree.body if isinstance(node, ast.ClassDef)]:
        for statement in listStatement:
            if isinstance(statement, ast.Assign):
                listName = [target.id for target in statement.targets if isinstance(target, ast.Name)]
            elif isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name):
                listName = [statement.target.id]
            else:
                continue
            for name in listName:
                if not _kConstantNameRe.match(name):
                    continue
                for literal in iterPathLiteralsInternal(statement.value):
                    if not literal.startswith(_kRepoRoot) or _kNotPathRe.search(literal):
                        continue
                    if literal in CheckScriptRepoPathsGate.mapExemption:
                        CheckScriptRepoPathsGate.useExemption(literal)
                        continue
                    if isMissingPathInternal(repositoryRoot, literal):
                        listViolation.append(f"{relPath}:{statement.lineno}: {name} 의 경로 '{literal}' 가 저장소에 없습니다 — 옮겼거나 지웠다, 실제 경로로 고칩니다")
    return listViolation


class CheckScriptRepoPathsGate(LintGate):
    """린트를 보는 린트 — 대상은 `Scripts/lint/gate/` · `Scripts/common/` 의 상수다."""

    description = "스크립트 상수의 Source/ · Test/ · Resource/ 경로가 실제 파일 · 폴더를 가리키는지 검사(옮긴 뒤 낡은 경로)"
    buildComment = "Checking that repository paths in script constants still exist..."
    timeoutSeconds = 30
    violationHeader = "저장소에 없는 경로를 든 스크립트 상수"
    preCommitSkipReason = "트리 전체 대조 — 파일을 옮기면 바뀌지 않은 스크립트의 상수가 낡는다(ctest -L lint · CI)"
    # 경로가 문자열이라 형식으로 막을 수 없는 틈이다 — 옮긴 파일을 가리키던 상수가 조용히 낡는 것을 이 게이트가 잡는다.
    selfTestCases = [
        {"name": "옮겨 간 파일을 가리키는 앞머리 상수", "expectedViolation": "_kThemeSourcePrefix 의 경로 'Source/Editor/Common/GUI/EditorThemeTokens.'",
         "files": {"Scripts/lint/gate/A.py": "_kThemeSourcePrefix = (\"Source/Editor/Common/GUI/EditorThemeTokens.\",)\n",
                   "Source/Editor/Common/Config/EditorThemeTokens.h": "// 옮긴 뒤\n"}},
        {"name": "없는 파일 상수(공용 라이브러리)", "expectedViolation": "kHeaderRelativePath 의 경로 'Source/Core/Gone.h'",
         "files": {"Scripts/common/B.py": "kHeaderRelativePath = \"Source/Core/Gone.h\"\n_kOk = \"Source/Core/\"\n", "Source/Core/Real.h": "// 있다\n"}},
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = sorted((repositoryRoot / "Scripts" / "lint" / "gate").glob("*.py")) + sorted((repositoryRoot / "Scripts" / "common").glob("*.py"))
        if not listPath:
            raise GateError("Scripts/lint/gate/ · Scripts/common/ 에 스크립트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")
        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(findStalePathsInternal(repositoryRoot, path.relative_to(repositoryRoot).as_posix(), path.read_text(encoding="utf-8")))
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} scripts")


main = CheckScriptRepoPathsGate.run

if __name__ == "__main__":
    sys.exit(main())
