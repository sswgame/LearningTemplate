#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckScriptLayout.py

`Scripts/` 의 **자리 규칙** — 폴더가 성격이고, 파일 이름의 앞머리가 그 폴더를 말한다(`Scripts/docs/Layout.md` 의 Layout 표).
이름만 보고 그 스크립트가 실패하는지 · 고쳐 쓰는지 · 찍기만 하는지 안다. 린트 폴더는 기반 클래스까지 본다 — `report/` 에 놓았는데
`LintReport` 가 아니면 `--out` · `--preset` 같은 껍데기가 없고, `CheckReportsRun` · `py -3 -m Scripts report` 가 그것을 부를 수 없다.

기반 클래스는 **import 없이** AST 로 본다(`class X(LintGate)`, 기반 모듈이 정의한 중간 기반 `class X(BaselineSourceGate)` 도) — 게이트가 다른 스크립트를 import 하면 훅이 느려지고, 고장 난 파일이 이 게이트를 죽인다.

  python Scripts/lint/gate/CheckScriptLayout.py [--files <path> ...]
"""

from __future__ import annotations

import argparse
import ast
import functools
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from LintGate import GateResult, LintGate  # noqa: E402


@dataclass(frozen=True)
class FolderRule:
    """
    폴더 하나의 규칙.

    - `folder`      : `Scripts/` 기준 폴더
    - `namePattern` : 파일 이름(확장자 없이) 규칙 — 비우면 보지 않는다
    - `baseClass`   : 이 이름의 클래스를 상속한 클래스가 있어야 한다 — 비우면 보지 않는다
    - `exemptName`  : 이 모듈 수준 이름이 있으면 기반 클래스 규칙에서 뺀다(이유를 그 이름이 든다 — `kFixerSkipReason`)
    """

    folder: str
    namePattern: str = ""
    baseClass: str = ""
    exemptName: str = ""


#: `Scripts/docs/Layout.md` 의 Layout 표 그대로.
_kRule: tuple[FolderRule, ...] = (
    FolderRule("lint/gate", r"^Check[A-Z]\w+$", "LintGate"),
    FolderRule("lint/selftest", r"^Check[A-Z]\w+$", "LintGate"),
    FolderRule("lint/fixer", r"^Format[A-Z]\w+$", "LintFixer", exemptName="kFixerSkipReason"),
    FolderRule("lint/report", r"^Run[A-Z]\w+$", "LintReport"),
    FolderRule("generate", r"^(Generate|Cook)[A-Z]\w+$"),
    FolderRule("setup", r"^(Setup|Install|Add)[A-Z]\w+$"),
    FolderRule("dev", r"^(Run|Compare|Make|Move|Remove|Configure|Sample|Store|List|Ci)[A-Z]\w*$"),
)


def baseNameInternal(base: ast.expr) -> str:
    """기반 식의 이름(맨 이름 또는 `x.이름`의 끝). 그 밖의 식은 빈 글."""
    if isinstance(base, ast.Name):
        return base.id
    return base.attr if isinstance(base, ast.Attribute) else ""


@functools.cache
def collectAcceptedBasesInternal(baseClass: str) -> frozenset[str]:
    """
    `baseClass` 와, 그 클래스를 정의한 모듈(`Scripts/lint/<baseClass>.py`)이 그것을 상속해 정의한 중간 기반들(`BaselineSourceGate`).
    중간 기반의 이름을 여기 따로 적지 않는다 — 기반 모듈이 정의하는 대로 따라간다. 모듈은 이 스크립트 옆에서 읽는다(자가 테스트의 임시 트리에는 없다).
    """
    tree = ast.parse((Path(__file__).resolve().parents[1] / f"{baseClass}.py").read_text(encoding="utf-8"))
    setAccepted = {baseClass}
    bGrew = True
    while bGrew:
        bGrew = False
        for node in tree.body:
            if isinstance(node, ast.ClassDef) and node.name not in setAccepted and any(baseNameInternal(base) in setAccepted for base in node.bases):
                setAccepted.add(node.name)
                bGrew = True
    return frozenset(setAccepted)


def inheritsInternal(tree: ast.Module, baseClass: str) -> bool:
    """모듈 수준 클래스 하나가 `baseClass` 나 기반 모듈이 정의한 그 하위 기반을 바로 상속하는가."""
    setAccepted = collectAcceptedBasesInternal(baseClass)
    return any(isinstance(node, ast.ClassDef) and any(baseNameInternal(base) in setAccepted for base in node.bases) for node in tree.body)


def definesNameInternal(tree: ast.Module, name: str) -> bool:
    return any(isinstance(node, (ast.Assign, ast.AnnAssign)) and any(isinstance(target, ast.Name) and target.id == name
                                                                      for target in (node.targets if isinstance(node, ast.Assign) else [node.target]))
               for node in tree.body)


def checkScriptInternal(relPath: str, text: str) -> list[str]:
    """`Scripts/<폴더>/<이름>.py` 하나의 위반."""
    folder, _, fileName = relPath.removeprefix("Scripts/").rpartition("/")
    stem = fileName.removesuffix(".py")
    rule = next((rule for rule in _kRule if rule.folder == folder), None)
    if rule is None or stem == "__init__":
        return []
    listViolation: list[str] = []
    if rule.namePattern and not re.match(rule.namePattern, stem):
        listViolation.append(f"{relPath}: {folder}/ 의 파일 이름은 {rule.namePattern} — Scripts/docs/Layout.md 의 Layout 표")
    if rule.baseClass:
        try:
            tree = ast.parse(text, filename=relPath)
        except SyntaxError as error:
            return listViolation + [f"{relPath}:{error.lineno}: 파싱할 수 없습니다: {error.msg}"]
        if not inheritsInternal(tree, rule.baseClass) and not (rule.exemptName and definesNameInternal(tree, rule.exemptName)):
            exempt = f"(아니면 {rule.exemptName} 에 이유)" if rule.exemptName else ""
            listViolation.append(f"{relPath}: {folder}/ 의 스크립트는 {rule.baseClass} 하위 클래스다{exempt}")
    return listViolation


class CheckScriptLayoutGate(LintGate):
    description = "Scripts/ 의 폴더마다 파일 이름 앞머리 · 린트 기반 클래스(Scripts/docs/Layout.md 의 Layout 표)"
    buildComment = "Checking that scripts sit in the folder their name prefix says..."
    timeoutSeconds = 30
    preCommitPattern = ("Scripts/*.py",)
    preCommitFileArgument = "--files"
    hint = "  폴더에 맞는 이름으로 옮기거나(git mv) 기반 클래스를 상속합니다 — Scripts/docs/Layout.md 의 Layout 표."
    selfTestCases = [
        {"name": "report/ 의 접두 어긋남", "expectedViolation": "Scripts/lint/report/ProbeReport.py: lint/report/ 의 파일 이름은 ^Run[A-Z]\\w+$",
         "files": {"Scripts/lint/report/ProbeReport.py": "from LintReport import LintReport\n\n\nclass ProbeReport(LintReport):\n    pass\n"}},
        {"name": "report/ 인데 LintReport 가 아님", "expectedViolation": "Scripts/lint/report/RunProbe.py: lint/report/ 의 스크립트는 LintReport 하위 클래스다",
         "files": {"Scripts/lint/report/RunProbe.py": "def main(argv=None):\n    return 0\n"}},
        {"name": "dev/ 의 동사 없는 이름", "expectedViolation": "Scripts/dev/BackendProbe.py: dev/ 의 파일 이름은 ^(Run|Compare|Make|Move|Remove|Configure|Sample|Store|List|Ci)[A-Z]\\w*$",
         "files": {"Scripts/dev/BackendProbe.py": "import sys\n\nif __name__ == \"__main__\":\n    sys.exit(0)\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Scripts",), suffixes=(".py",))
        listViolation: list[str] = []
        for path, text in self.readFiles(listPath):
            listViolation.extend(checkScriptInternal(path.relative_to(repositoryRoot).as_posix(), text))
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} scripts · {len(_kRule)} folders")


main = CheckScriptLayoutGate.run

if __name__ == "__main__":
    sys.exit(main())
