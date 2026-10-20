#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckScriptEntryPoints.py

`Scripts/` 아래 진입점(`if __name__ == "__main__":` 이 있는 파일 · `__main__.py`)은 **모듈 수준에서** `common` 을 import 해야 합니다.

이 저장소의 스크립트 출력은 한국어이고, Windows 콘솔의 기본 코덱(cp949 · cp1252)은 em-dash(`—`) 같은 글자 하나에서 `UnicodeEncodeError`
를 낸다 — 결과를 알리려던 print 가 스크립트를 죽인다. `common` 패키지는 import 될 때 stdout · stderr 를 UTF-8 로 맞추므로
(`Scripts/common/__init__.py` 머리), 진입점이 `common` 을 부르면 이 결함이 원리상 생기지 않는다.

- 직접(`import common` · `from common import ...`)이든, `common` 을 부르는 `Scripts/` 안 모듈(`lint.LintGate` · `lint.LintFixer` …)을 거쳐서든 된다.
  모듈은 `Scripts/` 기준 점 이름으로 찾는다(import 루트가 하나다 — `docs/plans/ScriptsStructure.md`).
- **함수 안의 import 는 치지 않는다** — 그 함수가 불리기 전의 출력은 보호받지 못한다(`RunForwardDeclarationCandidates.py` 가
  `--verify-unused` 갈래에서만 `common` 을 끌어와 `--apply` 끝에서 죽었다).

진입점은 또 **인자를 받는다**: 모듈 수준 `def main()` 은 첫 인자(`argv`)가 있어야 하고, `parse_args()` 를 인자 없이 부르지 않는다 —
그러면 프로그램에서 인자를 넘길 수 없어 `py -3 -m Scripts` · 테스트가 부를 수 없다(`main = XxxGate.run` 은 기반이 받는다).

**sys.path 는 진입점의 한 줄만 바꾼다**: `sys.path.insert(0, str(Path(__file__).resolve().parents[N]))` 가 `Scripts/` 를 가리킨다(N 은 `Scripts/`
아래 폴더 깊이, `__main__.py` 는 `.parent`). 라이브러리(진입점이 아닌 모듈)는 sys.path 를 건드리지 않는다 — 루트가 늘면 같은 파일이 두 이름으로
로드된다(`docs/plans/ScriptsStructure.md`). 층과 맨 이름 import 는 `CheckScriptLayers` 가 본다.

  python Scripts/lint/gate/CheckScriptEntryPoints.py [--root <repo>] [--files a.py b.py]
"""

from __future__ import annotations

import argparse
import ast
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import GateResult, LintGate  # noqa: E402

#: 콘솔을 UTF-8 로 맞추는 패키지 — import 하는 것만으로 맞춰진다.
_kInitializingPackage = "common"
_kScriptRoot = "Scripts"


def isMainGuardInternal(node: ast.stmt) -> bool:
    """`if __name__ == "__main__":` 인가."""
    if not isinstance(node, ast.If) or not isinstance(node.test, ast.Compare):
        return False
    test = node.test
    if len(test.ops) != 1 or not isinstance(test.ops[0], ast.Eq):
        return False
    listOperand = [test.left, *test.comparators]
    bName = any(isinstance(operand, ast.Name) and operand.id == "__name__" for operand in listOperand)
    bMain = any(isinstance(operand, ast.Constant) and operand.value == "__main__" for operand in listOperand)
    return bName and bMain


def findArgumentProblemsInternal(tree: ast.Module) -> list[str]:
    """진입점이 인자를 받지 못하는 자리 — 인자 없는 모듈 수준 `def main()`, 인자 없는 `parse_args()`."""
    listProblem: list[str] = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "main" and not node.args.args and node.args.vararg is None:
            listProblem.append(f"{node.lineno}: def main() 이 인자를 받지 않습니다 — main(argv: list[str] | None = None)")
    for node in ast.walk(tree):
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "parse_args"
                and not node.args and not node.keywords):
            listProblem.append(f"{node.lineno}: parse_args() 를 인자 없이 부릅니다 — parse_args(argv)")
    return listProblem


def isSysPathEditInternal(node: ast.AST) -> bool:
    """`sys.path.insert(...)` · `sys.path.append(...)` 호출인가."""
    if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute) or node.func.attr not in ("insert", "append"):
        return False
    owner = node.func.value
    return isinstance(owner, ast.Attribute) and owner.attr == "path" and isinstance(owner.value, ast.Name) and owner.value.id == "sys"


def findSysPathProblemsInternal(tree: ast.Module, text: str, depth: int, bEntryPoint: bool) -> list[str]:
    """sys.path 를 바꾸는 자리의 문제 — 라이브러리는 0 줄, 진입점은 `Scripts/` 를 가리키는 1 줄."""
    listEdit = [node for node in ast.walk(tree) if isSysPathEditInternal(node)]
    if not bEntryPoint:
        return [f"{node.lineno}: 라이브러리가 sys.path 를 바꿉니다 — 진입점만 Scripts 한 줄을 넣는다" for node in listEdit]
    if len(listEdit) > 1:
        return [f"{node.lineno}: sys.path 를 두 번 이상 바꿉니다 — Scripts 를 가리키는 한 줄만 둔다" for node in listEdit[1:]]
    listExpected = [f"str(Path(__file__).resolve().parents[{depth}])"] + (["str(Path(__file__).resolve().parent)"] if depth == 0 else [])
    for node in listEdit:
        listArgument = node.args[1:] if node.func.attr == "insert" else node.args
        segment = ast.get_source_segment(text, listArgument[0]) if listArgument else None
        if node.func.attr != "insert" or segment is None or segment.replace(" ", "").replace("pathlib.Path(", "Path(") not in listExpected:
            return [f"{node.lineno}: sys.path 줄이 Scripts 를 가리키지 않습니다 — sys.path.insert(0, {listExpected[0]})"]
    return []


def collectModuleLevelImportsInternal(listStatement: list[ast.stmt], outListName: list[str]) -> None:
    """
    모듈이 **읽힐 때** 실행되는 import 의 점 이름을 모읍니다.

    `if` · `try` · `with` 블록 안은 들어가고(모듈 수준에서 실행된다), 함수 · 클래스 본문은 들어가지 않는다.
    `from a.b import c` 는 `a.b` 와 `a.b.c` 를 둘 다 넣는다 — `c` 가 하위 모듈일 수 있다. 상대 import 는 뺀다.
    """
    for node in listStatement:
        if isinstance(node, ast.Import):
            outListName.extend(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            if node.level == 0 and node.module:
                outListName.append(node.module)
                outListName.extend(f"{node.module}.{alias.name}" for alias in node.names if alias.name != "*")
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            continue
        else:
            for fieldName in ("body", "orelse", "finalbody", "handlers"):
                listChild = getattr(node, fieldName, None)
                if not listChild:
                    continue
                for child in listChild:
                    if isinstance(child, ast.ExceptHandler):
                        collectModuleLevelImportsInternal(child.body, outListName)
                    elif isinstance(child, ast.stmt):
                        collectModuleLevelImportsInternal([child], outListName)


class ScriptModuleGraph:
    """`Scripts/` 의 파이썬 모듈과 그 모듈 수준 import — 어느 모듈이 `common` 에 닿는지 답한다."""

    def __init__(self, repositoryRoot: Path, listPath: list[Path]) -> None:
        self._scriptRoot = (repositoryRoot / _kScriptRoot).resolve()
        self._mapPathToImport: dict[Path, list[str]] = {}
        self._mapPathToEntryPoint: dict[Path, bool] = {}
        self._mapPathToReach: dict[Path, bool] = {}
        self._mapPathToArgumentProblem: dict[Path, list[str]] = {}
        self._mapPathToSysPathProblem: dict[Path, list[str]] = {}
        self.listParseError: list[str] = []
        for path, text in LintGate.readFiles(listPath):
            resolved = path.resolve()
            try:
                tree = ast.parse(text, filename=str(path))
            except SyntaxError as exception:
                self.listParseError.append(f"{path.relative_to(repositoryRoot).as_posix()}:1 파싱할 수 없습니다: {exception}")
                continue
            listName: list[str] = []
            collectModuleLevelImportsInternal(tree.body, listName)
            self._mapPathToImport[resolved] = listName
            self._mapPathToEntryPoint[resolved] = path.name == "__main__.py" or any(isMainGuardInternal(node) for node in tree.body)
            self._mapPathToArgumentProblem[resolved] = findArgumentProblemsInternal(tree)
            depth = len(resolved.relative_to(self._scriptRoot).parts) - 1
            self._mapPathToSysPathProblem[resolved] = findSysPathProblemsInternal(tree, text, depth, self._mapPathToEntryPoint[resolved])

    def isEntryPoint(self, path: Path) -> bool:
        return self._mapPathToEntryPoint.get(path.resolve(), False)

    def findArgumentProblems(self, path: Path) -> list[str]:
        return self._mapPathToArgumentProblem.get(path.resolve(), [])

    def findSysPathProblems(self, path: Path) -> list[str]:
        return self._mapPathToSysPathProblem.get(path.resolve(), [])

    def reachesCommon(self, path: Path) -> bool:
        """이 모듈을 읽으면 `common` 이 import 되는가(모듈 수준 import 를 따라간다)."""
        return self.reachesCommonInternal(path.resolve(), set())

    def reachesCommonInternal(self, path: Path, uniqueVisiting: set[Path]) -> bool:
        if path in self._mapPathToReach:
            return self._mapPathToReach[path]
        if path in uniqueVisiting:
            return False
        uniqueVisiting.add(path)
        bReach = False
        for name in self._mapPathToImport.get(path, []):
            if name.split(".")[0] == _kInitializingPackage:
                bReach = True
                break
            target = self.findLocalModuleInternal(name)
            if target is not None and target != path and self.reachesCommonInternal(target, uniqueVisiting):
                bReach = True
                break
        uniqueVisiting.discard(path)
        self._mapPathToReach[path] = bReach
        return bReach

    def findLocalModuleInternal(self, dottedName: str) -> Path | None:
        """점 이름에 맞는 `Scripts/` 안 파일. import 루트는 `Scripts/` 하나라 점 이름이 곧 경로다(맨 이름 import 는 `CheckScriptLayers` 가 막는다)."""
        listPart = dottedName.split(".")
        for candidate in (self._scriptRoot.joinpath(*listPart).with_suffix(".py"), self._scriptRoot.joinpath(*listPart, "__init__.py")):
            if candidate in self._mapPathToImport:
                return candidate
        return None


def findViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> tuple[list[str], int]:
    """위반 줄과 본 진입점 수. import 를 따라가야 하므로 모듈 표는 늘 `Scripts/` 전체로 만든다."""
    listAll = LintGate.selectTargetFiles(repositoryRoot, None, listScanRoot=(_kScriptRoot,), suffixes=(".py",))
    graph = ScriptModuleGraph(repositoryRoot, listAll)
    listTarget = listAll if not listFileArgument else LintGate.selectTargetFiles(
        repositoryRoot, listFileArgument, listScanRoot=(_kScriptRoot,), suffixes=(".py",))
    listViolation = list(graph.listParseError)
    entryPointCount = 0
    for path in listTarget:
        listViolation.extend(f"{path.relative_to(repositoryRoot).as_posix()}:{problem}" for problem in graph.findSysPathProblems(path))
        if not graph.isEntryPoint(path):
            continue
        entryPointCount += 1
        if not graph.reachesCommon(path):
            listViolation.append(f"{path.relative_to(repositoryRoot).as_posix()}: 진입점이 모듈 수준에서 common 을 import 하지 않습니다"
                                 " — Windows 콘솔(cp949)에서 한국어 출력이 UnicodeEncodeError 로 죽습니다")
        listViolation.extend(f"{path.relative_to(repositoryRoot).as_posix()}:{problem} — 프로그램에서 인자를 넘길 수 없다"
                             for problem in graph.findArgumentProblems(path))
    return listViolation, entryPointCount


class CheckScriptEntryPointsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "Scripts/ 진입점이 모듈 수준에서 common 을 import 해 콘솔을 UTF-8 로 맞추는지, sys.path 를 진입점의 Scripts 한 줄로만 바꾸는지 검사"
    buildComment = "Checking that every script entry point sets the console to UTF-8 through common..."
    timeoutSeconds = 30
    preCommitPattern = ("Scripts/*.py",)
    preCommitFileArgument = "--files"
    violationHeader = "진입점 콘솔 초기화 · 인자 받기 누락"
    hint = (
        "  진입점 머리에서 Scripts 를 sys.path 에 넣고 common 을 import 합니다:\n"
        "    sys.path.insert(0, str(Path(__file__).resolve().parents[N]))   # Scripts\n"
        "    import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다\n"
        "  함수 안의 import 는 그 함수 전의 출력을 지키지 못합니다 — 모듈 수준에 둡니다.\n"
        "  진입점은 main(argv: list[str] | None = None) 이고 parser.parse_args(argv) 로 읽습니다."
    )
    selfTestCases = [
        {
            "name": "common 을 부르지 않는 진입점",
            "expectedViolation": "ProbeReport.py: 진입점이 모듈 수준에서 common 을 import 하지 않습니다",
            "files": {"Scripts/report/ProbeReport.py": "import sys\n\n\ndef main():\n    print('보고 — 끝')\n    return 0\n\n\n"
                                                       "if __name__ == \"__main__\":\n    sys.exit(main())\n"},
        },
        {
            "name": "함수 안에서만 common 을 import 한다",
            "expectedViolation": "ProbeLate.py: 진입점이 모듈 수준에서 common 을 import 하지 않습니다",
            "files": {"Scripts/report/ProbeLate.py": "import sys\n\n\ndef verify():\n    import common\n    return common\n\n\n"
                                                     "def main():\n    print('보고 — 끝')\n    return 0\n\n\n"
                                                     "if __name__ == \"__main__\":\n    sys.exit(main())\n"},
        },
        {
            "name": "common 을 부르지 않는 이웃 모듈만 거친다",
            "expectedViolation": "Scripts/report/ProbeEntry.py: 진입점이 모듈 수준에서 common 을 import 하지 않습니다",
            "files": {
                "Scripts/report/ProbeHelper.py": "import os\n\nkProbeValue = os.sep\n",
                "Scripts/report/ProbeEntry.py": "import sys\nfrom report.ProbeHelper import kProbeValue\n\n"
                                                "if __name__ == \"__main__\":\n    print(kProbeValue)\n",
            },
        },
        {
            "name": "인자를 받지 않는 main()",
            "expectedViolation": "Scripts/report/ProbeArgless.py:5: def main() 이 인자를 받지 않습니다",
            "files": {"Scripts/report/ProbeArgless.py": "import sys\nimport common\n\n\ndef main():\n    return 0\n\n\n"
                                                         "if __name__ == \"__main__\":\n    sys.exit(main())\n"},
        },
        {
            "name": "parse_args() 를 인자 없이",
            "expectedViolation": "Scripts/report/ProbeParse.py:7: parse_args() 를 인자 없이 부릅니다",
            "files": {"Scripts/report/ProbeParse.py": "import argparse\nimport sys\nimport common\n\n\ndef main(argv=None):\n"
                                                       "    argparse.ArgumentParser().parse_args()\n    return 0\n\n\n"
                                                       "if __name__ == \"__main__\":\n    sys.exit(main())\n"},
        },
        {
            "name": "라이브러리가 sys.path 를 바꾼다",
            "expectedViolation": "Scripts/report/ProbeLibrary.py:4: 라이브러리가 sys.path 를 바꿉니다",
            "files": {"Scripts/report/ProbeLibrary.py": "import sys\nfrom pathlib import Path\n\nsys.path.insert(0, str(Path(__file__).resolve().parent))\n"},
        },
        {
            "name": "진입점이 Scripts 밖 루트를 더한다",
            "expectedViolation": "Scripts/report/ProbeRoot.py:5: sys.path 를 두 번 이상 바꿉니다",
            "files": {"Scripts/report/ProbeRoot.py": "import sys\nfrom pathlib import Path\n\nsys.path.insert(0, str(Path(__file__).resolve().parents[1]))\n"
                                                    "sys.path.insert(0, str(Path(__file__).resolve().parent))\nimport common\n\n\n"
                                                    "def main(argv=None):\n    return 0\n\n\nif __name__ == \"__main__\":\n    sys.exit(main())\n"},
        },
        {
            "name": "진입점의 sys.path 줄이 Scripts 를 가리키지 않는다",
            "expectedViolation": "Scripts/report/ProbeDepth.py:4: sys.path 줄이 Scripts 를 가리키지 않습니다",
            "files": {"Scripts/report/ProbeDepth.py": "import sys\nfrom pathlib import Path\n\nsys.path.insert(0, str(Path(__file__).resolve().parents[2]))\n"
                                                     "import common\n\n\ndef main(argv=None):\n    return 0\n\n\n"
                                                     "if __name__ == \"__main__\":\n    sys.exit(main())\n"},
        },
        {
            "name": "python -m 진입점(__main__.py)",
            "expectedViolation": "Scripts/__main__.py: 진입점이 모듈 수준에서 common 을 import 하지 않습니다",
            "files": {"Scripts/__main__.py": "import argparse\n\nargparse.ArgumentParser(description='도구 — 목록').print_help()\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation, entryPointCount = findViolations(repositoryRoot, args.files)
        return GateResult(listViolation=listViolation, summary=f"{entryPointCount} entry points reach common")


main = CheckScriptEntryPointsGate.run

if __name__ == "__main__":
    sys.exit(main())
