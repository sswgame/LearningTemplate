#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckScriptCommonHelpers.py

`Scripts/common` 에 한 자리가 있는 일을 스크립트가 스스로 하는지 봅니다. 공통부를 만들어도 새 스크립트가 옛 모양을 복사해 오면 다시 갈라진다
(빌드 폴더 고르기가 열 군데로 불어난 경위). 규칙 하나 = 표 한 줄: 무엇을 보면 · 무엇을 대신 쓰나 · 어디는 예외인가(이유와 함께).

  python Scripts/lint/gate/CheckScriptCommonHelpers.py [--files <path> ...]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from LintGate import GateResult, LintGate  # noqa: E402


@dataclass(frozen=True)
class HelperRule:
    """
    공통부를 비켜 가는 모양 하나.

    - `name`        : 위반 줄에 찍는 이름
    - `pattern`     : 그 모양(줄 단위 정규식, `#` 로 시작하는 주석 줄은 보지 않는다)
    - `instead`     : 대신 쓸 것
    - `allowedPaths`: 저장소 기준 경로 — 예외는 이유를 주석으로
    """

    name: str
    pattern: re.Pattern[str]
    instead: str
    allowedPaths: tuple[str, ...] = ()


_kRule: tuple[HelperRule, ...] = (
    HelperRule("subprocess", re.compile(r"\bsubprocess\.(run|check_output|check_call|call)\("), "common.runProcess",
               # 컴파일 DB 의 셸 명령 문자열을 그대로 넘기는 훑기 — Process.py 머리말의 "쓰지 않는 곳"
               ("Scripts/lint/report/RunBuildWarnings.py", "Scripts/lint/report/RunHeaderSelfContained.py",
                "Scripts/lint/report/RunForwardDeclarationCandidates.py")),
    HelperRule("compile DB", re.compile(r"[\"']compile_commands\.json[\"']"), "BuildTree.readCompileDatabase"),
    HelperRule("빌드 폴더 조립", re.compile(r"/\s*[\"']build[\"']\s*/|os\.path\.join\([^)\n]*[\"']build[\"']"), "BuildTree.fromArguments · fromPreset"),
    HelperRule("콘솔 인코딩", re.compile(r"\.reconfigure\(\s*encoding"), "모듈 수준 `import common`(common/__init__.py)"),
    HelperRule("바뀌었을 때만 쓰기", re.compile(r"previous\s*!=\s*content|read_text\([^)\n]*\)\s*==\s*content"), "common.writeGeneratedFile"),
    HelperRule("CMakeCache 읽기", re.compile(r"[\"']CMakeCache\.txt[\"']"), "BuildTree.readCacheValue"),
)

#: 규칙의 정규식 · 조각을 글로 드는 파일 — 자기 자신과, 같은 정규식으로 숫자를 세는 보고서.
_kSelfPaths = ("Scripts/lint/gate/CheckScriptCommonHelpers.py", "Scripts/lint/report/RunBuildScriptInventory.py")


class CheckScriptCommonHelpersGate(LintGate):
    description = "Scripts/ 가 common 의 한 자리(프로세스 · 빌드 폴더 · 콘솔 · 생성 파일)를 비켜 가지 않는지"
    buildComment = "Checking that scripts use the shared helpers in Scripts/common..."
    timeoutSeconds = 30
    preCommitPattern = ("Scripts/*.py",)
    preCommitFileArgument = "--files"
    hint = "  common 의 한 자리를 쓰십시오. 정말 예외면 그 규칙 줄의 allowedPaths 에 경로와 이유를 적습니다."
    selfTestCases = [
        {"name": "subprocess 직접", "files": {"Scripts/dev/Probe.py": "import subprocess\nsubprocess.run(['x'])\n"}},
        {"name": "build/<preset> 조립", "files": {"Scripts/dev/Probe.py": "path = root / \"build\" / preset\n"}},
        {"name": "쓰기 비교", "files": {"Scripts/generate/Probe.py": "if previous != content:\n    pass\n"}},
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Scripts",), suffixes=(".py",))
        listViolation: list[str] = []
        for path, text in self.readFiles(listPath):
            relPath = path.relative_to(repositoryRoot).as_posix()
            if relPath.startswith("Scripts/common/") or relPath in _kSelfPaths:
                continue
            for rule in _kRule:
                if relPath in rule.allowedPaths:
                    continue
                for lineNumber, line in enumerate(text.splitlines(), 1):
                    if rule.pattern.search(line) and not line.lstrip().startswith("#"):
                        listViolation.append(f"{relPath}:{lineNumber}: {rule.name} — {rule.instead} 를 쓴다")
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} scripts · {len(_kRule)} rules")


main = CheckScriptCommonHelpersGate.run

if __name__ == "__main__":
    sys.exit(main())
