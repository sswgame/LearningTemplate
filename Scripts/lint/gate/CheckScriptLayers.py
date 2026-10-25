#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts 층 검사 — `Scripts/` 의 파이썬 모듈은 층을 거꾸로 · 옆으로 import 하지 않는다(C++ 의 `CheckCoreLayers` 와 같은 구조).

  0 common(공용 라이브러리) → 1 lint(린트 틀: LintGate · LintFixer · LintReport · LintCatalog · SourceScan · LayerRules · StagedScope · conventions)
  → 2 명령(lint.gate · fixer · report · selftest, 린트 루트 명령, setup · generate · dev · qa · asset) → 3 __main__

강제 규칙(파일의 모든 `import` · `from … import`, 함수 안 포함, 상대 import 는 절대 이름으로 풀어서):
  1) 아래 층은 위 층을 import 하지 않는다.
  2) 층 2 명령끼리는 import 하지 않는다 — 다른 명령이 쓸 코드는 층 0 · 1 로 옮긴다. 조립 명령 · 게이트와 짝 픽서처럼 이유가 있는 간선은
     예외 표에 `<파일>|<모듈>` 로 적는다.
  3) 맨 이름 import(`import LintGate`, `from MakeWorktree import …`)는 쓰지 않는다 — import 루트는 `Scripts/` 하나다.
  4) `Scripts.` 로 시작하는 이름은 쓰지 않는다.

왜 게이트인가: 파이썬에는 CMake 의 링크 가시성(`target_link_libraries`)에 해당하는 형식이 없어 import 방향을 형식으로 막을 수 없고,
파일을 경로로 돌리면 파이썬이 그 파일의 폴더를 sys.path 맨 앞에 넣으므로 맨 이름 import 도 형식으로 막지 못한다. 숨은 라이브러리를
층 0 · 1 로 옮겨 import 할 이유를 없앴고(`docs/plans/ScriptsStructure.md`), 이 게이트는 남은 틈만 본다. 같은 파일이 실제로 두 이름으로
로드되는지는 `Test/PythonTest/TestScriptsImportIdentity.py` 가 본다.

동적 import(`importlib.import_module`)는 보지 않는다. 린트 틀(`LintCatalog`)이 층 2 의 린트를 폴더로 찾아 싣는 것은 언리얼 UAT 가
명령 클래스를 리플렉션으로 찾는 것과 같은 의도된 구조다.

층 표(`[tier]`)와 예외 표(`[exemption]`)는 `Scripts/lint/rules/CheckScriptLayers.toml` 에 있다.

  python Scripts/lint/gate/CheckScriptLayers.py [--root <repo>] [--files a.py b.py]
"""

from __future__ import annotations

import argparse
import ast
import sys
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.RuleData import kKindInteger  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, SourceScanSet, selectedRelatives  # noqa: E402

_kScriptRoot = "Scripts"
#: 명령 층 — 이 층의 모듈끼리는 import 하지 않는다.
_kCommandTier = 2
_kRuleSchema = {"tier": kKindInteger}
#: 모듈 이름 접두 → 층(긴 접두가 먼저 맞는다).
_kTier: dict[str, int] = LintGate.readRules("CheckScriptLayers", _kRuleSchema, requiredKeys=("tier",))["tier"]


def tierOfModule(dottedName: str) -> int | None:
    """모듈 점 이름의 층 — 표에서 가장 긴 접두(점 단위)로 고른다. 표에 없으면 None(`Scripts/` 의 모듈이 아니다)."""
    listPart = dottedName.split(".")
    for count in range(len(listPart), 0, -1):
        tier = _kTier.get(".".join(listPart[:count]))
        if tier is not None:
            return tier
    return None


def moduleNameOf(relative: str) -> str:
    """파일(저장소 기준 경로) → `Scripts/` 기준 점 이름(`Scripts/lint/gate/CheckX.py` → `lint.gate.CheckX`, `Scripts/dev/__init__.py` → `dev`)."""
    listPart = list(PurePosixPath(relative).with_suffix("").parts[1:])
    if listPart[-1] == "__init__" and len(listPart) > 1:
        listPart.pop()
    return ".".join(listPart)


class ScriptTree:
    """`Scripts/` 의 모듈 이름 표 — 점 이름이 파일인지, 맨 이름이 어느 파일 이름과 같은지 답한다."""

    def __init__(self, listRelative: list[str]) -> None:
        self.uniqueModule: set[str] = set()
        self.uniqueBareName: set[str] = set()
        for relative in listRelative:
            path = PurePosixPath(relative)
            self.uniqueModule.add(moduleNameOf(relative))
            self.uniqueBareName.add(path.stem if path.stem != "__init__" else path.parent.name)
        self.uniqueTopLevel = {name.split(".")[0] for name in self.uniqueModule}

    def resolveImported(self, base: str, listAlias: list[str]) -> list[str]:
        """`from base import a, b` 가 실제로 기대는 모듈 — `a` 가 하위 모듈이면 `base.a`, 아니면 `base`."""
        listModule = [f"{base}.{alias}" for alias in listAlias if f"{base}.{alias}" in self.uniqueModule]
        return listModule or [base]


def collectImportsInternal(tree: ast.Module, moduleName: str, bPackage: bool) -> list[tuple[int, str, list[str]]]:
    """(줄, 쓴 이름, 기대는 모듈 후보) — 상대 import 는 이 모듈의 패키지 기준 절대 이름으로 푼다."""
    listImport: list[tuple[int, str, list[str]]] = []
    listPackagePart = moduleName.split(".") if bPackage else moduleName.split(".")[:-1]
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            listImport.extend((node.lineno, alias.name, [alias.name]) for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            if node.level:
                listBase = listPackagePart[:len(listPackagePart) - (node.level - 1)]
                base = ".".join(listBase + ([node.module] if node.module else []))
            else:
                base = node.module or ""
            listImport.append((node.lineno, base if not node.level else "." * node.level + (node.module or ""),
                               [base, *[alias.name for alias in node.names if alias.name != "*"]]))
    return listImport


@dataclass
class ScriptImports:
    """파일 하나가 import 하는 것(자식 프로세스에서 부모로 피클해 온다). 파싱하지 못했으면 `parseError`(위반 줄) 만 있다."""

    parseError: str = ""
    moduleName: str = ""
    listImport: list[tuple[int, str, list[str]]] = field(default_factory=list)


def readImportsInternal(source: SourceFile) -> ScriptImports:
    """파일 하나의 import 를 읽는다 — 층 판정은 모든 모듈의 이름 표(`ScriptTree`)가 필요해 끝맺음에서 한다."""
    try:
        syntax = ast.parse(source.text, filename=source.relative)
    except SyntaxError as error:
        return ScriptImports(parseError=f"{source.relative}:{error.lineno}: 파싱할 수 없습니다: {error.msg}")
    moduleName = moduleNameOf(source.relative)
    return ScriptImports(moduleName=moduleName, listImport=collectImportsInternal(syntax, moduleName, source.path.stem == "__init__"))


def findViolationsInFile(tree: ScriptTree, relPath: str, imports: ScriptImports) -> list[str]:
    """파일 하나의 층 위반."""
    if imports.parseError:
        return [imports.parseError]
    moduleName = imports.moduleName
    selfTier = tierOfModule(moduleName)
    if selfTier is None:
        return [f"{relPath}: 모듈 '{moduleName}' 의 층이 표(rules/CheckScriptLayers.toml 의 [tier])에 없습니다 — 새 폴더는 층을 정해 넣습니다"]
    listViolation: list[str] = []
    for lineNumber, writtenName, listCandidate in imports.listImport:
        base = listCandidate[0]
        first = base.split(".")[0]
        if first == _kScriptRoot:
            listViolation.append(f"{relPath}:{lineNumber}: '{writtenName}' — Scripts. 로 시작하는 이름을 쓰지 않습니다(import 루트는 Scripts/ 안)")
            continue
        if first not in tree.uniqueTopLevel:
            if first in tree.uniqueBareName:
                listViolation.append(f"{relPath}:{lineNumber}: '{writtenName}' — 맨 이름 import 입니다. Scripts 기준 점 이름으로 씁니다")
            continue
        for imported in tree.resolveImported(base, listCandidate[1:]):
            importedTier = tierOfModule(imported)
            if importedTier is None or imported == moduleName:
                continue
            key = f"{relPath}|{imported}"
            bUpward = importedTier > selfTier
            bSideways = selfTier == _kCommandTier and importedTier == _kCommandTier
            if not bUpward and not bSideways:
                continue
            if key in CheckScriptLayersGate.mapExemption:
                CheckScriptLayersGate.useExemption(key)
                continue
            if bUpward:
                listViolation.append(f"{relPath}:{lineNumber}: '{imported}' -> 층 {selfTier}('{moduleName}')가 위 층 {importedTier} 를 import 합니다")
            else:
                listViolation.append(f"{relPath}:{lineNumber}: '{imported}' -> 층 2 명령끼리 import 합니다('{moduleName}')")
    return listViolation


class CheckScriptLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    ruleSchema = _kRuleSchema
    description = "Scripts 파이썬 모듈이 층(common → lint 틀 → 명령 → __main__)을 거꾸로 · 옆으로 import 하지 않는지 검사"
    buildComment = "Checking the Scripts module layers..."
    timeoutSeconds = 30
    preCommitPattern = ("Scripts/*.py",)
    preCommitFileArgument = "--files"
    violationHeader = "Scripts 층 위반"
    hint = (
        "  다른 명령이 쓸 코드는 common/(공용) 이나 lint/ 의 틀 모듈로 옮긴다(docs/plans/ScriptsStructure.md 2절).\n"
        "  조립 명령처럼 이유가 있는 간선만 Scripts/lint/rules/CheckScriptLayers.toml 의 [exemption] 에 '<파일>|<모듈>' 로 이유와 함께 둔다."
    )
    selfTestCases = [
        {
            "name": "common 이 린트 틀을 import 한다",
            "expectedViolation": "Scripts/common/Probe.py:1: 'lint.LintGate' -> 층 0('common.Probe')가 위 층 1 를 import 합니다",
            "files": {"Scripts/common/Probe.py": "from lint.LintGate import LintGate\n", "Scripts/lint/LintGate.py": "kProbe = 1\n"},
        },
        {
            "name": "명령끼리 import 한다",
            "expectedViolation": "Scripts/dev/RunProbe.py:1: 'dev.MakeProbe' -> 층 2 명령끼리 import 합니다('dev.RunProbe')",
            "files": {"Scripts/dev/RunProbe.py": "from dev.MakeProbe import kValue\n", "Scripts/dev/MakeProbe.py": "kValue = 1\n"},
        },
        {
            "name": "맨 이름 import",
            "expectedViolation": "Scripts/lint/gate/CheckProbe.py:2: 'LintGate' — 맨 이름 import 입니다",
            "files": {"Scripts/lint/gate/CheckProbe.py": "import sys\nfrom LintGate import LintGate\n", "Scripts/lint/LintGate.py": "kProbe = 1\n"},
        },
        {
            "name": "Scripts. 로 시작하는 이름",
            "expectedViolation": "Scripts/dev/RunProbe.py:1: 'Scripts.common' — Scripts. 로 시작하는 이름을 쓰지 않습니다",
            "files": {"Scripts/dev/RunProbe.py": "import Scripts.common\n"},
        },
        {
            "name": "함수 안의 상대 import 도 본다",
            "expectedViolation": "Scripts/lint/Probe.py:2: 'lint.gate.CheckProbe' -> 층 1('lint.Probe')가 위 층 2 를 import 합니다",
            "files": {"Scripts/lint/Probe.py": "def probe() -> None:\n    from .gate import CheckProbe\n", "Scripts/lint/gate/CheckProbe.py": "kProbe = 1\n"},
        },
        {
            # `--files` 는 판정할 파일만 줄인다 — 맨 이름이 어느 파일 이름인지는 `--files` 에 없는 `Scripts/lint/LintGate.py` 까지 읽어야 안다.
            "name": "--files 로 골라도 모듈 이름 표는 전체에서 만든다",
            "expectedViolation": "Scripts/lint/gate/CheckProbe.py:2: 'LintGate' — 맨 이름 import 입니다",
            "unexpectedViolation": "Scripts/common/Probe.py",
            "args": ["--files", "Scripts/lint/gate/CheckProbe.py"],
            "files": {
                "Scripts/lint/gate/CheckProbe.py": "import sys\nfrom LintGate import LintGate\n",
                "Scripts/lint/LintGate.py": "kProbe = 1\n",
                "Scripts/common/Probe.py": "from lint.LintGate import LintGate\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    # 모듈 이름 표는 `Scripts/` 전체로 만든다 — `--files` 가 있어도 전체를 읽고(`bAlwaysWhole`) 판정만 고른 파일로 줄인다.
    sourceScanSets = (SourceScanSet("script", (_kScriptRoot,), (".py",), bAlwaysWhole=True),)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(value=readImportsInternal(source))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        tree = ScriptTree([findings.relative for findings in result.listFindings])
        setTarget = selectedRelatives(repositoryRoot, self, args)
        listViolation: list[str] = []
        targetCount = 0
        for findings in result.listFindings:
            relPath = findings.relative
            if setTarget is not None and relPath not in setTarget:
                continue
            targetCount += 1
            for key in self.mapExemption:
                if key.startswith(relPath + "|"):
                    self.seeExemption(key)
            listViolation.extend(findViolationsInFile(tree, relPath, findings.value))
        return GateResult(listViolation=listViolation, summary=f"Scripts 모듈 {targetCount} 개가 층을 지킨다")


main = CheckScriptLayersGate.run

if __name__ == "__main__":
    sys.exit(main())
