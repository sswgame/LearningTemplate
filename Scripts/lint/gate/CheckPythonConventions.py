#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckPythonConventions.py

`AGENTS.md` 의 **Python** 명명 규칙을 검사합니다.

**이 저장소의 파이썬은 아무도 보고 있지 않았다.** `AGENTS.md` 는 세 언어(C++ · CMake · Python)의
규칙을 적어 두었는데 게이트는 `kCppAllExtensions` 만 훑는다 — 즉 **린트를 만드는 코드가 린트를
받지 않는 상태**로 12,000 줄이 쌓였다. 실제로 어긴 자리가 이미 있었다(`Scripts/dev/BackendSmoke.py`
의 `ppm_stats` · `BACKENDS` · `BACKGROUND`).

검사 규칙 (`AGENTS.md` → "### Python"):

- 공개 함수는 `camelCase`, 내부 헬퍼는 `camelCaseInternal`
- 모듈 상수는 `kPascalCase` 또는 `_kPascalCase`
- 모듈/파일 이름은 `PascalCase.py`

**AST 로 본다, 정규식이 아니라.** 함수 이름·모듈 수준 대입은 구문 트리가 정확히 답해 주는
질문이라 굳이 틀릴 이유가 없다. 문자열 안의 예시 코드를 위반으로 읽는 사고도 이걸로 사라진다.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: 파이썬이 이름을 정해 둔 자리 — 우리 규칙을 들이댈 수 없다.
_kDunderRe = re.compile(r'^__[a-z0-9_]+__$')

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.
_kExcludedPart = ("vcpkg", "build", "generated", ".git", "__pycache__", ".venv")

#: `PascalCase.py` 예외. 패키지 표시 파일과 `python -m` 진입점은 이름이 정해져 있다.
_kAllowedModuleName = {"__init__", "__main__"}

#: `camelCase` — 소문자로 시작하고 밑줄이 없다. 뒤에 `Internal` 이 붙는 것도 같은 모양이다.
_kCamelCaseRe = re.compile(r'^[a-z][a-zA-Z0-9]*$')

#: `kPascalCase` / `_kPascalCase` — `k` 다음이 대문자여야 한다 (`kdirRoot` 는 안 된다).
_kConstantNameRe = re.compile(r'^_?k[A-Z][a-zA-Z0-9]*$')

#: `PascalCase.py`
_kModuleNameRe = re.compile(r'^[A-Z][a-zA-Z0-9]*$')

#: 모듈 수준이라도 상수가 아닌 것들.
_kAllowedModuleVariable = {"main"}

#: 타입을 만드는 호출 — 그 결과는 상수가 아니라 **타입**이라 `PascalCase` 가 맞다.
_kTypeFactoryName = {"TypeVar", "NewType", "ParamSpec", "TypeVarTuple", "NamedTuple", "TypedDict"}


def isTypeDeclarationInternal(node: ast.Assign | ast.AnnAssign) -> bool:
    """
    이 대입이 **타입 선언**인가. 그렇다면 상수 규칙의 대상이 아니다.

    `TItem = TypeVar("TItem")` 과 `PathLike = str | Path` 는 둘 다 타입이다. 파이썬에서 타입은
    `PascalCase` 로 쓰는 것이 표준이고 (`typing` 자신이 그렇게 한다), `kPathlike` 로 바꾸면
    타입 힌트가 읽히지 않는다. `AGENTS.md` 의 "모듈 상수" 규칙이 겨누는 것은 값이지 타입이 아니다.
    """
    if isinstance(node, ast.AnnAssign):
        annotation = node.annotation
        if isinstance(annotation, ast.Name) and annotation.id in ("TypeAlias", "TypeAliasType"):
            return True

    value = node.value
    if value is None:
        return False

    # `TItem = TypeVar("TItem")`
    if isinstance(value, ast.Call):
        func = value.func
        name = func.id if isinstance(func, ast.Name) else getattr(func, "attr", "")
        if name in _kTypeFactoryName:
            return True

    # `PathLike = str | Path` · `Handler = Callable[[int], None]` — 타입 식으로만 이루어진 별칭.
    return isTypeExpressionInternal(value)


def isTypeExpressionInternal(node: ast.expr) -> bool:
    """식이 타입만으로 이루어져 있는가 (`str | Path`, `Optional[int]`, `list[str]`)."""
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr):
        return isTypeExpressionInternal(node.left) and isTypeExpressionInternal(node.right)

    if isinstance(node, ast.Subscript):
        return isTypeExpressionInternal(node.value)

    if isinstance(node, ast.Attribute):
        return node.attr[:1].isupper()

    if isinstance(node, ast.Name):
        # 타입 이름은 대문자로 시작하거나(`Path` · `Optional`) 내장 타입이다(`str` · `int`).
        return node.id[:1].isupper() or node.id in ("str", "int", "float", "bool", "bytes", "list", "dict", "set", "tuple", "None")

    return False


def isPrivateNameInternal(name: str) -> bool:
    return name.startswith("_")


def checkFunctionNameInternal(node: ast.FunctionDef | ast.AsyncFunctionDef, relPath: str) -> list[str]:
    """함수 이름 하나를 봅니다. 던더와 클래스 메서드 관례(`setUp` 등)는 건드리지 않습니다."""
    name = node.name
    if _kDunderRe.match(name):
        return []

    bare = name.lstrip("_")
    if not bare:
        return []

    if _kCamelCaseRe.match(bare):
        return []

    return [
        f"{relPath}:{node.lineno} 함수 '{name}' 는 camelCase 여야 합니다 "
        f"(내부 헬퍼는 camelCaseInternal) — AGENTS.md '### Python'"
    ]


def checkModuleConstantInternal(node: ast.Assign | ast.AnnAssign, relPath: str) -> list[str]:
    """
    모듈 수준 대입 하나를 봅니다.

    대문자로만 된 이름(`BACKENDS`)은 파이썬 관례상 상수지만 **이 저장소의 관례는 아니다** —
    `AGENTS.md` 는 `kPascalCase` 를 쓴다. 소문자로 시작하는 모듈 변수는 상수가 아니라 상태이므로
    여기서 보지 않는다(그건 다른 규칙의 몫이다).
    """
    listTarget = node.targets if isinstance(node, ast.Assign) else [node.target]
    listViolation: list[str] = []

    for target in listTarget:
        if not isinstance(target, ast.Name):
            continue

        name = target.id
        if _kDunderRe.match(name) or name in _kAllowedModuleVariable:
            continue

        if isTypeDeclarationInternal(node):
            continue

        bare = name.lstrip("_")
        # 상수로 **보이는** 것만 본다: 대문자로 시작하거나 전부 대문자인 이름.
        if not bare or not bare[0].isupper():
            continue

        if _kConstantNameRe.match(name):
            continue

        suggestion = "k" + "".join(part.capitalize() for part in bare.split("_"))
        mark = "_" if isPrivateNameInternal(name) else ""
        listViolation.append(
            f"{relPath}:{node.lineno} 모듈 상수 '{name}' 는 kPascalCase 여야 합니다 "
            f"('{mark}{suggestion}' 권장) — AGENTS.md '### Python'"
        )

    return listViolation


def checkPythonFileInternal(path: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나를 파싱해 규칙을 적용합니다."""
    relPath = path.relative_to(repositoryRoot).as_posix()
    listViolation: list[str] = []

    stem = path.stem
    if stem not in _kAllowedModuleName and not _kModuleNameRe.match(stem):
        listViolation.append(
            f"{relPath}:1 모듈 이름 '{path.name}' 는 PascalCase.py 여야 합니다 — AGENTS.md '### Python'"
        )

    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    except (OSError, SyntaxError) as exception:
        # 파싱이 안 되는 파이썬은 **위반이 아니라 오류다** — 조용히 넘기면 린트가 눈을 감는다.
        listViolation.append(f"{relPath}:1 파싱할 수 없습니다: {exception}")
        return listViolation

    for node in tree.body:
        if isinstance(node, (ast.Assign, ast.AnnAssign)):
            listViolation.extend(checkModuleConstantInternal(node, relPath))

    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            listViolation.extend(checkFunctionNameInternal(node, relPath))

    return listViolation


class CheckPythonConventionsGate(LintGate):
    """`AGENTS.md` 의 Python 규칙 — 지금까지 아무 게이트도 보지 않던 자리다."""

    description = "Scripts/ 및 Tools/ 파이썬 명명 규칙 검사 (AGENTS.md '### Python')"
    buildComment = "Checking Python naming conventions (AGENTS.md)..."
    timeoutSeconds = 30
    preCommitPattern = ("*.py",)
    preCommitFileArgument = "--files"
    violationHeader = "Python 명명 규칙 위반"
    hint = "  AGENTS.md '### Python': 함수는 camelCase(내부는 camelCaseInternal), 모듈 상수는 kPascalCase, 파일은 PascalCase.py."
    selfTestCases = [
        {
            "name": "snake_case 함수",
            "files": {"Scripts/Probe/BadFunction.py": "def ppm_stats(path):\n    return path\n"},
        },
        {
            "name": "대문자 스네이크 모듈 상수",
            "files": {"Scripts/Probe/BadConstant.py": "BACKENDS = [1, 2]\n"},
        },
        {
            "name": "PascalCase 가 아닌 파일 이름",
            "files": {"Scripts/Probe/bad_module.py": "kProbeValue = 1\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 파일 (생략 시 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        if args.files:
            listPath = [Path(item).resolve() for item in args.files]
            listPath = [path for path in listPath if path.suffix == ".py" and path.is_file()]
        else:
            # 저장소 전체를 본다. `Scripts/` 와 `Tools/` 만 훑으면 다른 데 놓인 파이썬이 조용히
            # 규칙 밖에 있게 된다 — 이 저장소가 린트에서 반복해 배운 것이 "목록이 아니라 자리" 다.
            listPath = sorted(
                path
                for path in repositoryRoot.rglob("*.py")
                if not any(part in _kExcludedPart for part in path.relative_to(repositoryRoot).parts)
            )

        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(checkPythonFileInternal(path, repositoryRoot))

        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} python files scanned")


main = CheckPythonConventionsGate.run


if __name__ == "__main__":
    sys.exit(main())
