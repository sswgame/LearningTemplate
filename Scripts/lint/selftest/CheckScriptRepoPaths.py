#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/selftest/CheckScriptRepoPaths.py

스크립트에 적은 저장소 경로가 **실제 파일 · 폴더를 가리키는지** 본다.

경로는 문자열이라 형식으로 막을 수 없고, 파일을 옮기면 스크립트의 문자열만 조용히 낡는다(`_kThemeSourcePrefix = "Source/Editor/Common/GUI/EditorThemeTokens."` 는
파일이 `Common/Config/` 로 옮겨 간 뒤에도 남아, 그 파일이 검사 대상으로 되돌아왔다). 예외 표는 기반(`LintGate`)이 낡은 줄을 알리지만 표가 아닌 상수는
아무도 안 본다. 그래서 `Scripts/**/*.py` 전체에서 `Source/` · `Test/` · `Resource/` 로 시작하는 글자 리터럴(docstring · f-string · 공백 · `:` · `{}` 가
든 글자는 경로가 아니라 건너뛴다) 가운데 **경로로 쓰인 것**을 AST 로 훑는다. 경로로 쓰인 것은 다음이다(이름 규칙에 기대지 않는다).

  - 모듈 · 클래스 수준 대입의 값(글자, 또는 글자만 든 튜플 · 목록 · 집합 · `frozenset(…)`) — `selfTestCases` 는 가상 저장소의 조각이라 뺀다.
  - `Path(…)` · `os.path.join(…)` · `.exists()` · `.is_file()` · `.is_dir()` · `.glob()` · `.rglob()` · `.read_text()` · `.relative_to()` 의 인자.
  - `/` 연산의 피연산자, `==` · `!=` · `in` 비교의 피연산자(튜플 · 목록 · 집합 속 원소 포함).
  - `.startswith(…)` 의 인자 — 앞머리라서 파일 이름 중간에서 끊겨도 된다.

존재 확인:
  - `/` 로 끝나면 그 폴더가 있어야 한다.
  - `.` · `_` · `-` 로 끝나거나 `.startswith` 인자면 파일 이름의 앞머리다 — 그 폴더에 그 앞머리로 시작하는 항목이 있어야 한다.
  - `*` · `?` · `[` 가 든 글롭은 패턴이라(선택 파일 · 파일 이름 패턴) 첫 글롭 칸 앞의 폴더가 있는지만 본다.
  - 그 밖에는 그 파일 · 폴더가 있어야 한다.

가상 경로(셀프테스트 조각이 임시 저장소에 만드는 파일)와 선택 파일은 `rules/CheckScriptRepoPaths.toml [exemption]` 에 이유와 함께 둔다.
키가 `.py` 로 끝나면 그 스크립트 전체(가상 경로를 많이 든 셀프테스트), 아니면 그 글자 하나다.

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
_kNotPathRe = re.compile(r"[\s:{}]")
_kGlobRe = re.compile(r"[*?\[]")
_kPrefixTail = (".", "_", "-")
_kSetFactoryName = frozenset({"frozenset", "set", "tuple", "list"})
_kPathFactoryName = frozenset({"Path", "PurePath", "PurePosixPath", "PureWindowsPath"})
_kPathMethodName = frozenset({"join", "joinpath", "exists", "is_file", "is_dir", "glob", "rglob", "read_text", "read_bytes", "relative_to"})
_kSkipAssignName = frozenset({"selfTestCases"})
#: 경로의 쓰임 — 상수(대입의 값) · 정확한 경로(Path · 비교 · 나눗셈) · 앞머리(`startswith`).
_kUseConstant = "constant"
_kUseExact = "exact"
_kUsePrefix = "prefix"


def isMissingPathInternal(repositoryRoot: Path, text: str, bPrefix: bool = False) -> bool:
    """`text` 가 가리키는 파일 · 폴더 · 앞머리 · 글롭이 저장소에 없는가."""
    if _kGlobRe.search(text):
        listPart = text.split("/")
        listFixed = [part for part in listPart[:next(index for index, part in enumerate(listPart) if _kGlobRe.search(part))]]
        return not (repositoryRoot / "/".join(listFixed)).is_dir()
    if text.endswith("/"):
        return not (repositoryRoot / text).is_dir()
    if bPrefix or text.endswith(_kPrefixTail):
        parent, _, head = text.rpartition("/")
        folder = repositoryRoot / parent
        return not folder.is_dir() or not any(entry.name.startswith(head) for entry in folder.iterdir())
    return not (repositoryRoot / text).exists()


def collectSkippedNodesInternal(tree: ast.Module) -> set[int]:
    """docstring · f-string 조각 · `selfTestCases` 대입 속 노드(`id`)."""
    setSkip: set[int] = set()
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)) and node.body:
            first = node.body[0]
            if isinstance(first, ast.Expr) and isinstance(first.value, ast.Constant):
                setSkip.add(id(first.value))
        if isinstance(node, ast.JoinedStr):
            setSkip.update(id(child) for child in ast.walk(node))
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            listTarget = node.targets if isinstance(node, ast.Assign) else [node.target]
            if any(isinstance(target, ast.Name) and target.id in _kSkipAssignName for target in listTarget):
                setSkip.update(id(child) for child in ast.walk(node))
    return setSkip


def findPathUseInternal(node: ast.Constant, mapParent: dict[int, ast.AST]) -> str | None:
    """글자 리터럴이 경로로 쓰이면 쓰임(`_kUse…`), 아니면 None."""
    child: ast.AST = node
    parent = mapParent.get(id(child))
    bCollection = False
    while isinstance(parent, (ast.Tuple, ast.List, ast.Set)) or (
            isinstance(parent, ast.Call) and isinstance(parent.func, ast.Name) and parent.func.id in _kSetFactoryName):
        bCollection = True
        child = parent
        parent = mapParent.get(id(child))
    if isinstance(parent, (ast.Assign, ast.AnnAssign)):
        listTarget = parent.targets if isinstance(parent, ast.Assign) else [parent.target]
        bTopLevel = isinstance(mapParent.get(id(parent)), (ast.Module, ast.ClassDef))
        if bTopLevel and parent.value is child and all(isinstance(target, ast.Name) for target in listTarget):
            return _kUseConstant
        return None
    if bCollection and not isinstance(parent, ast.Compare):
        return None
    if isinstance(parent, ast.Compare):
        return _kUseExact
    if isinstance(parent, ast.BinOp) and isinstance(parent.op, ast.Div):
        return _kUseExact
    if isinstance(parent, ast.Call) and not bCollection:
        func = parent.func
        if isinstance(func, ast.Name) and func.id in _kPathFactoryName:
            return _kUseExact
        if isinstance(func, ast.Attribute):
            if func.attr == "startswith":
                return _kUsePrefix
            if func.attr in _kPathMethodName and (func.attr != "join" or isinstance(func.value, ast.Attribute)):
                return _kUseExact
    return None


def findStalePathsInternal(repositoryRoot: Path, relPath: str, text: str) -> list[str]:
    """한 스크립트 파일에서 없는 저장소 경로를 든 리터럴을 찾습니다. 예외 표는 키 한 개가 글자 하나 또는 스크립트 하나다."""
    gate = CheckScriptRepoPathsGate
    tree = ast.parse(text)
    setSkip = collectSkippedNodesInternal(tree)
    mapParent = {id(child): parent for parent in ast.walk(tree) for child in ast.iter_child_nodes(parent)}
    bFileExempt = relPath in gate.mapExemption
    if bFileExempt:
        gate.seeExemption(relPath)
    listViolation: list[str] = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Constant) and isinstance(node.value, str)) or id(node) in setSkip:
            continue
        literal = node.value
        if not literal.startswith(_kRepoRoot) or _kNotPathRe.search(literal):
            continue
        use = findPathUseInternal(node, mapParent)
        if use is None or not isMissingPathInternal(repositoryRoot, literal, use == _kUsePrefix):
            continue
        if bFileExempt:
            gate.useExemption(relPath)
        elif literal in gate.mapExemption:
            gate.useExemption(literal)
        else:
            listViolation.append(f"{relPath}:{node.lineno}: 경로 '{literal}' 가 저장소에 없습니다 — 옮겼거나 지웠다, 실제 경로로 고칩니다")
    for literal in gate.mapExemption:
        if not literal.endswith(".py") and literal in text:
            gate.seeExemption(literal)
    return listViolation


class CheckScriptRepoPathsGate(LintGate):
    """린트를 보는 린트 — 대상은 `Scripts/` 의 파이썬 전부다."""

    description = "스크립트가 경로로 쓰는 Source/ · Test/ · Resource/ 글자가 실제 파일 · 폴더를 가리키는지 검사(옮긴 뒤 낡은 경로)"
    buildComment = "Checking that repository paths in scripts still exist..."
    timeoutSeconds = 60
    violationHeader = "저장소에 없는 경로를 쓰는 스크립트"
    preCommitSkipReason = "트리 전체 대조 — 파일을 옮기면 바뀌지 않은 스크립트의 경로가 낡는다(ctest -L lint · CI)"
    # 경로가 문자열이라 형식으로 막을 수 없는 틈이다 — 옮긴 파일을 가리키던 문자열이 조용히 낡는 것을 이 게이트가 잡는다.
    selfTestCases = [
        {"name": "옮겨 간 파일을 가리키는 앞머리 상수", "expectedViolation": "경로 'Source/Editor/Common/GUI/EditorThemeTokens.'",
         "files": {"Scripts/lint/gate/A.py": "_kThemeSourcePrefix = (\"Source/Editor/Common/GUI/EditorThemeTokens.\",)\n",
                   "Source/Editor/Common/Config/EditorThemeTokens.h": "// 옮긴 뒤\n"}},
        {"name": "없는 파일 상수(공용 라이브러리)", "expectedViolation": "경로 'Source/Core/Gone.h'",
         "files": {"Scripts/common/B.py": "kHeaderRelativePath = \"Source/Core/Gone.h\"\n_kOk = \"Source/Core/\"\n", "Source/Core/Real.h": "// 있다\n"}},
        {"name": "함수 안 Path 연산과 다른 폴더의 스크립트", "expectedViolation": "경로 'Test/Gone/Case.cpp'",
         "files": {"Scripts/qa/C.py": "def f(root):\n    return (root / \"Test/Gone/Case.cpp\").read_text()\n", "Test/Real/Case.cpp": "// 있다\n"}},
        {"name": "함수 안 비교와 startswith", "expectedViolation": "경로 'Resource/gone/shaders/'",
         "files": {"Scripts/dev/D.py": "def f(relative):\n    return relative.startswith(\"Source/Real/\") and relative == \"Resource/gone/shaders/\"\n", "Source/Real/A.h": "// 있다\n"}},
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = [path for path in sorted((repositoryRoot / "Scripts").rglob("*.py")) if "__pycache__" not in path.parts]
        if not listPath:
            raise GateError("Scripts/ 에 스크립트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다")
        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(findStalePathsInternal(repositoryRoot, path.relative_to(repositoryRoot).as_posix(), path.read_text(encoding="utf-8")))
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} scripts")


main = CheckScriptRepoPathsGate.run

if __name__ == "__main__":
    sys.exit(main())
