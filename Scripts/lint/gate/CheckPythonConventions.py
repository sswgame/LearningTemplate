#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckPythonConventions.py

`AGENTS.md` 의 **Python** 규칙(이름 · 모양)을 검사합니다.

`AGENTS.md` 는 세 언어(C++ · CMake · Python)의 규칙을 적어 두었는데 C++ 게이트는 `kCppAllExtensions` 만
훑는다 — 이 게이트가 없으면 **린트를 만드는 코드가 린트를 받지 않는다**.

검사 규칙 (`AGENTS.md` → "### Python"):

- 이름: 공개 함수는 `camelCase`, 내부 헬퍼는 `camelCaseInternal` · 모듈 상수는 `kPascalCase` 또는 `_kPascalCase` · 파일은 `PascalCase.py`
- 모양(`_kStyleRule`): 모듈 docstring · `from __future__ import annotations` · 매개변수와 반환 타입 표기 · `X | None` 과 내장 제네릭 ·
  글 파일 입출력의 `encoding=` · 경로는 `pathlib`(os.path 는 글자 연산만) · 종료는 `sys.exit` · `__main__` 가드는 `sys.exit(main())` 한 줄 ·
  `ArgumentParser` 는 `description=`

**AST 로 본다, 정규식이 아니라.** 함수 이름·모듈 수준 대입은 구문 트리가 정확히 답해 주는
질문이라 굳이 틀릴 이유가 없다. 문자열 안의 예시 코드를 위반으로 읽는 사고도 이걸로 사라진다.

규칙 데이터(예외 표 · 목록)는 `Scripts/lint/rules/CheckPythonConventions.toml` 에 있다.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common.RuleData import kKindTextList  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kRuleSchema = {"lexical_os_path_function": kKindTextList, "legacy_typing_name": kKindTextList}
_kRuleData = LintGate.readRules("CheckPythonConventions", _kRuleSchema, requiredKeys=tuple(_kRuleSchema))
#: os.path 가운데 pathlib 에 같은 것이 없는 글자 연산 — 이것 말고는 `Path` 로 쓴다.
_kSetLexicalOsPathFunction: frozenset[str] = frozenset(_kRuleData["lexical_os_path_function"])
#: typing 의 옛 철자 — `X | None` · `list[str]` 로 쓴다.
_kSetLegacyTypingName: frozenset[str] = frozenset(_kRuleData["legacy_typing_name"])

#: 모양 규칙의 이름 — 위반 줄의 머리이자 예외 키(`<규칙>:<경로>`)의 앞부분이다.
_kStyleRule = ("docstring", "future", "annotation", "typing", "encoding", "osPath", "exit", "mainGuard", "argparse")
#: 예외 표에서 모든 모양 규칙을 빼는 키의 앞말.
_kAllStyleRulePrefix = "*:"
#: 글 모드로 여는 `open` 의 모드 글자 — `b` 가 없다.
_kTextModeRe = re.compile(r"^[rwxat+]+$")

#: 파이썬이 이름을 정해 둔 자리 — 우리 규칙을 들이댈 수 없다.
_kDunderRe = re.compile(r'^__[a-z0-9_]+__$')

#: 검사에서 빼는 경로 조각 — 남의 코드이거나 생성물이다.

#: `camelCase` — 소문자로 시작하고 밑줄이 없다. 뒤에 `Internal` 이 붙는 것도 같은 모양이다.
_kCamelCaseRe = re.compile(r'^[a-z][a-zA-Z0-9]*$')

#: `kPascalCase` / `_kPascalCase` — `k` 다음이 대문자여야 한다 (`kdirRoot` 는 안 된다).
_kConstantNameRe = re.compile(r'^_?k[A-Z][a-zA-Z0-9]*$')

#: `PascalCase.py`
_kModuleNameRe = re.compile(r'^[A-Z][a-zA-Z0-9]*$')

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
        if _kDunderRe.match(name):
            continue
        if f"variable:{name}" in CheckPythonConventionsGate.mapExemption:
            CheckPythonConventionsGate.useExemption(f"variable:{name}")
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


class StyleScanInternal:
    """파일 하나에 모양 규칙을 적용합니다. 위반은 `(규칙, 줄, 말)` 로 모으고, 예외 표에 든 규칙은 넘깁니다."""

    def __init__(self, tree: ast.Module, relPath: str) -> None:
        self.tree = tree
        self.relPath = relPath
        self.listViolation: list[str] = []
        self.mapExemptionKey: dict[str, str] = {}
        for rule in _kStyleRule:
            for key in (f"{rule}:{relPath}", _kAllStyleRulePrefix + relPath):
                if key in CheckPythonConventionsGate.mapExemption:
                    CheckPythonConventionsGate.seeExemption(key)
                    self.mapExemptionKey[rule] = key
                    break

    def addViolation(self, rule: str, lineNumber: int, message: str) -> None:
        key = self.mapExemptionKey.get(rule)
        if key is not None:
            CheckPythonConventionsGate.useExemption(key)
            return
        self.listViolation.append(f"{self.relPath}:{lineNumber} [{rule}] {message} — AGENTS.md '### Python'")

    def run(self) -> list[str]:
        self.checkModuleHead()
        for node in self.tree.body:
            if isMainGuardInternal(node):
                self.checkMainGuard(node)
        for node in ast.walk(self.tree):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                self.checkAnnotation(node)
            elif isinstance(node, ast.Call):
                self.checkCall(node)
            elif isinstance(node, ast.ImportFrom):
                self.checkImportFrom(node)
            elif isinstance(node, ast.Attribute):
                self.checkAttribute(node)
            elif isinstance(node, ast.Raise):
                self.checkRaise(node)
        return self.listViolation

    def checkModuleHead(self) -> None:
        """모듈 docstring 과 `from __future__ import annotations` — 빈 파일은 보지 않는다."""
        body = self.tree.body
        if not body:
            return
        if ast.get_docstring(self.tree) is None:
            self.addViolation("docstring", 1, "모듈 docstring 이 없습니다 — 파일 맨 위에 무엇을 하는 모듈인지 적습니다")
        listCode = body[1:] if ast.get_docstring(self.tree) is not None else body
        if listCode and not any(isFutureAnnotationsInternal(node) for node in listCode):
            self.addViolation("future", 1, "`from __future__ import annotations` 가 없습니다")

    def checkMainGuard(self, node: ast.If) -> None:
        """`if __name__ == "__main__":` 의 몸은 `sys.exit(main(...))`(시험은 `unittest.main(...)`) 한 줄이다."""
        if len(node.body) == 1 and isinstance(node.body[0], ast.Expr) and isinstance(node.body[0].value, ast.Call):
            call = node.body[0].value
            if isDottedNameInternal(call.func, "unittest.main"):
                return
            if (isDottedNameInternal(call.func, "sys.exit") and len(call.args) == 1 and isinstance(call.args[0], ast.Call)
                    and isDottedNameInternal(call.args[0].func, "main")):
                return
        self.addViolation("mainGuard", node.lineno, "`__main__` 가드의 몸은 `sys.exit(main())` 한 줄입니다(시험은 `unittest.main()`)")

    def checkAnnotation(self, node: ast.FunctionDef | ast.AsyncFunctionDef) -> None:
        """매개변수(`self` · `cls` 빼고)와 반환 타입을 모두 적는다."""
        arguments = node.args
        listParameter = [*arguments.posonlyargs, *arguments.args]
        if listParameter and listParameter[0].arg in ("self", "cls") and listParameter[0].annotation is None:
            listParameter = listParameter[1:]
        listParameter += [*arguments.kwonlyargs, *(argument for argument in (arguments.vararg, arguments.kwarg) if argument is not None)]
        listMissing = [argument.arg for argument in listParameter if argument.annotation is None]
        if node.returns is None:
            listMissing.append("반환")
        if listMissing:
            self.addViolation("annotation", node.lineno, f"함수 '{node.name}' 의 타입 표기가 없습니다: {', '.join(listMissing)}")

    def checkCall(self, node: ast.Call) -> None:
        func = node.func
        setKeyword = {keyword.arg for keyword in node.keywords}
        name = func.id if isinstance(func, ast.Name) else (func.attr if isinstance(func, ast.Attribute) else "")
        if name == "ArgumentParser" and "description" not in setKeyword:
            self.addViolation("argparse", node.lineno, "`ArgumentParser` 에 `description=` 이 없습니다")
        if "encoding" in setKeyword:
            return
        if isinstance(func, ast.Name) and func.id == "open":
            mode = node.args[1] if len(node.args) > 1 else next((keyword.value for keyword in node.keywords if keyword.arg == "mode"), None)
            if not (isinstance(mode, ast.Constant) and isinstance(mode.value, str) and "b" in mode.value):
                self.addViolation("encoding", node.lineno, "글 모드 `open()` 에 `encoding=` 이 없습니다")
        elif isinstance(func, ast.Attribute):
            # `read_text(encoding)` · `write_text(data, encoding)` 처럼 자리로 넘긴 것도 인정한다.
            if (func.attr == "read_text" and not node.args) or (func.attr == "write_text" and len(node.args) < 2):
                self.addViolation("encoding", node.lineno, f"`{func.attr}()` 에 `encoding=` 이 없습니다")
            elif (func.attr == "open" and node.args and isinstance(node.args[0], ast.Constant) and isinstance(node.args[0].value, str)
                  and _kTextModeRe.match(node.args[0].value)):
                self.addViolation("encoding", node.lineno, "글 모드 `.open()` 에 `encoding=` 이 없습니다")

    def checkImportFrom(self, node: ast.ImportFrom) -> None:
        listName = [alias.name for alias in node.names]
        if node.module == "typing":
            listLegacy = [name for name in listName if name in _kSetLegacyTypingName]
            if listLegacy:
                self.addViolation("typing", node.lineno, f"typing 의 옛 철자({', '.join(listLegacy)}) — `X | None` · `list[str]` 로 씁니다")
        elif node.module == "os.path":
            listBanned = [name for name in listName if name not in _kSetLexicalOsPathFunction]
            if listBanned:
                self.addViolation("osPath", node.lineno, f"os.path 의 {', '.join(listBanned)} — `pathlib.Path` 로 씁니다")
        elif node.module == "os" and "path" in listName:
            self.addViolation("osPath", node.lineno, "`from os import path` — `pathlib.Path` 로 씁니다")

    def checkAttribute(self, node: ast.Attribute) -> None:
        if isDottedNameInternal(node.value, "typing") and node.attr in _kSetLegacyTypingName:
            self.addViolation("typing", node.lineno, f"`typing.{node.attr}` — `X | None` · `list[str]` 로 씁니다")
        elif isDottedNameInternal(node.value, "os.path") and node.attr not in _kSetLexicalOsPathFunction:
            self.addViolation("osPath", node.lineno, f"`os.path.{node.attr}` — `pathlib.Path` 로 씁니다(os.path 는 글자 연산만)")

    def checkRaise(self, node: ast.Raise) -> None:
        exception = node.exc.func if isinstance(node.exc, ast.Call) else node.exc
        if isinstance(exception, ast.Name) and exception.id == "SystemExit":
            self.addViolation("exit", node.lineno, "`raise SystemExit` — `sys.exit(...)` 로 끝냅니다")


def isDottedNameInternal(node: ast.expr, dottedName: str) -> bool:
    """`node` 가 `a.b.c` 모양의 이름 `dottedName` 인가."""
    listPart = dottedName.split(".")
    for part in reversed(listPart[1:]):
        if not (isinstance(node, ast.Attribute) and node.attr == part):
            return False
        node = node.value
    return isinstance(node, ast.Name) and node.id == listPart[0]


def isFutureAnnotationsInternal(node: ast.stmt) -> bool:
    return isinstance(node, ast.ImportFrom) and node.module == "__future__" and any(alias.name == "annotations" for alias in node.names)


def isMainGuardInternal(node: ast.stmt) -> bool:
    """`if __name__ == "__main__":` 인가."""
    if not isinstance(node, ast.If) or not isinstance(node.test, ast.Compare):
        return False
    test = node.test
    return (isinstance(test.left, ast.Name) and test.left.id == "__name__" and len(test.comparators) == 1
            and isinstance(test.comparators[0], ast.Constant) and test.comparators[0].value == "__main__")


def checkPythonFileInternal(path: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나를 파싱해 규칙을 적용합니다."""
    relPath = path.relative_to(repositoryRoot).as_posix()
    listViolation: list[str] = []

    stem = path.stem
    if f"module:{stem}" in CheckPythonConventionsGate.mapExemption:
        CheckPythonConventionsGate.useExemption(f"module:{stem}")
    elif not _kModuleNameRe.match(stem):
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

    listViolation.extend(StyleScanInternal(tree, relPath).run())
    return listViolation


#: 셀프테스트 조각의 머리 — 이것만으로는 어떤 규칙도 어기지 않아, 조각마다 겨눈 규칙 하나만 진다.
_kProbeHead = '"""조각."""\nfrom __future__ import annotations\n\n'


class CheckPythonConventionsGate(LintGate):
    """`AGENTS.md` 의 Python 규칙 — 지금까지 아무 게이트도 보지 않던 자리다."""

    description = "저장소 파이썬의 이름 · 모양 규칙 검사 (AGENTS.md '### Python')"
    buildComment = "Checking Python conventions (AGENTS.md)..."
    timeoutSeconds = 30
    preCommitPattern = ("*.py",)
    preCommitFileArgument = "--files"
    ruleSchema = _kRuleSchema
    violationHeader = "Python 규칙 위반"
    hint = ("  AGENTS.md '### Python': 함수는 camelCase(내부는 camelCaseInternal), 모듈 상수는 kPascalCase, 파일은 PascalCase.py. "
            "[규칙] 머리가 붙은 줄은 같은 절의 모양 규칙이고, 정말 예외면 rules/CheckPythonConventions.toml 에 '<규칙>:<경로>' 와 이유를 적습니다.")
    selfTestCases = [
        {
            "name": "snake_case 함수",
            "files": {"Scripts/Probe/BadFunction.py": _kProbeHead + "def ppm_stats(path: str) -> str:\n    return path\n"},
        },
        {
            "name": "대문자 스네이크 모듈 상수",
            "files": {"Scripts/Probe/BadConstant.py": _kProbeHead + "BACKENDS = [1, 2]\n"},
        },
        {
            "name": "PascalCase 가 아닌 파일 이름",
            "files": {"Scripts/Probe/bad_module.py": _kProbeHead + "kProbeValue = 1\n"},
        },
        {"name": "모듈 docstring 없음", "files": {"Scripts/Probe/NoDoc.py": "from __future__ import annotations\n\nkProbeValue = 1\n"}},
        {"name": "future annotations 없음", "files": {"Scripts/Probe/NoFuture.py": '"""조각."""\nkProbeValue = 1\n'}},
        {"name": "반환 타입 표기 없음", "files": {"Scripts/Probe/NoReturn.py": _kProbeHead + "def probe(value: int):\n    return value\n"}},
        {"name": "매개변수 타입 표기 없음", "files": {"Scripts/Probe/NoParam.py": _kProbeHead + "def probe(value) -> int:\n    return value\n"}},
        {"name": "typing.Optional", "files": {"Scripts/Probe/OldTyping.py": _kProbeHead + "from typing import Optional\n"}},
        {"name": "encoding 없는 open", "files": {"Scripts/Probe/NoEncoding.py": _kProbeHead + "kText = open('a.txt').read()\n"}},
        {"name": "encoding 없는 read_text", "files": {"Scripts/Probe/NoEncodingPath.py": _kProbeHead + "from pathlib import Path\n\nkText = Path('a').read_text()\n"}},
        {"name": "os.path.join", "files": {"Scripts/Probe/OsPath.py": _kProbeHead + "import os\n\nkPath = os.path.join('a', 'b')\n"}},
        {"name": "raise SystemExit", "files": {"Scripts/Probe/RaiseExit.py": _kProbeHead + "def probe() -> None:\n    raise SystemExit(1)\n"}},
        {
            "name": "__main__ 가드 모양",
            "files": {"Scripts/Probe/Guard.py": _kProbeHead + "def main() -> int:\n    return 0\n\n\nif __name__ == '__main__':\n    main()\n"},
        },
        {
            "name": "description 없는 ArgumentParser",
            "files": {"Scripts/Probe/Parser.py": _kProbeHead + "import argparse\n\nkParser = argparse.ArgumentParser()\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 저장소 전체를 본다. `Scripts/` 와 `Tools/` 만 훑으면 다른 데 놓인 파이썬이 조용히
        # 규칙 밖에 있게 된다 — 이 저장소가 린트에서 반복해 배운 것이 "목록이 아니라 자리" 다.
        listPath = self.selectTargetFiles(repositoryRoot, args.files, suffixes=(".py",))

        listViolation: list[str] = []
        for path in listPath:
            listViolation.extend(checkPythonFileInternal(path, repositoryRoot))

        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} python files scanned")


main = CheckPythonConventionsGate.run


if __name__ == "__main__":
    sys.exit(main())
