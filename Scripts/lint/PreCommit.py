#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommit.py

린트 입구 하나 — 규칙은 `Rules.py` 의 표 한 곳에 있다.

  py -3 -m Scripts lint                 # 커밋 훅: 모든 규칙이 스테이지된 파일만 본다(LintScope) + clang-format
  py -3 -m Scripts lint --all           # 묶음 검증: 모든 규칙이 저장소 전체를 본다 + clang-format(VerifyBundle.sh 첫 단계)
  py -3 -m Scripts lint --list          # 규칙 표(갈래 · 막는 것 · 왜 린트인가 · 예외 파일)
  py -3 -m Scripts lint <id> [인자…]    # 규칙 하나의 단독 실행(`--all` · `--self-check` 등은 그 모듈의 인자)

커밋 훅은 저장소 크기에 비례하지 않는다: 규칙은 스테이지된 파일만 보고, 여러 파일을 보는 규칙은 내용 해시 색인(`FactCache`)을 바뀐 파일만
다시 계산해 쓴다. 규칙의 표 · 예외 · 모듈이 스테이지에 있으면 그 규칙만 저장소 전체를 본다(`Rule.listInputs`).
**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다. 고치는 것은 `py -3 -m Scripts format` · `format-includes` 다.
"""

from __future__ import annotations

import importlib
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ProjectConstants import kCppAllExtensions, kLintTargetRelDirs, kNotOurDirNames, kShaderSourceExtensions  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.Format import runClangFormatBatch  # noqa: E402
from lint.LintScope import LintScope, makeStagedScope, makeWholeScope  # noqa: E402
from lint.Rules import Rule, formatTable, kRuleById, kRules  # noqa: E402

kCppSuffixes = tuple(sorted(kCppAllExtensions))
kShaderSuffixes = tuple(sorted(kShaderSourceExtensions))


def selectFormatFiles(scope: LintScope) -> list[str]:
    """clang-format 이 볼 파일 — `format --all` 과 같은 대상(C++ 은 `kLintTargetRelDirs`, 셰이더는 남의 코드 · 산출물 폴더 밖). 받아 온 남의 헤더는 고치지 않는다."""
    listCpp = scope.select(kLintTargetRelDirs, kCppSuffixes)
    listShader = [path for path in scope.select(suffixes=kShaderSuffixes) if not any(part in kNotOurDirNames for part in path.split("/")[:-1])]
    return listCpp + listShader


def runRule(rule: Rule, projectRoot: Path, scope: LintScope) -> list[str]:
    moduleName, _, functionName = rule.check.partition(":")
    return getattr(importlib.import_module(moduleName), functionName)(projectRoot, scope)


def selectRuleScope(rule: Rule, projectRoot: Path, scope: LintScope) -> LintScope:
    """규칙의 표 · 예외 · 모듈이 바뀌었으면 그 규칙만 저장소 전체 — 바뀌지 않은 파일의 판정도 달라진다."""
    if scope.bWholeRepo or not scope.touches(files=(), roots=rule.listInputs()):
        return scope
    return makeWholeScope(projectRoot)


def runLint(projectRoot: Path, scope: LintScope) -> int:
    if not scope.listFile and not scope.listRemoved:
        return ExitCode.Passed
    hasErrors = False
    listWhole: list[str] = []
    for rule in kRules:
        ruleScope = selectRuleScope(rule, projectRoot, scope)
        if ruleScope.bWholeRepo and not scope.bWholeRepo:
            listWhole.append(rule.id)
        listProblem = runRule(rule, projectRoot, ruleScope)
        if listProblem:
            hasErrors = True
            where = f" — 예외 · 기준선: {rule.exceptions}" if rule.exceptions else ""
            print(f"[{rule.id}] FAIL — {rule.blocks} {len(listProblem)}건{where}")
            for line in listProblem:
                print(f"  - {line}")

    listFormat = [projectRoot / path for path in selectFormatFiles(scope)]
    if listFormat and runClangFormatBatch(listFormat, checkOnly=True, cwd=projectRoot) != 0:
        hasErrors = True
        print("[format] FAIL — 'py -3 -m Scripts format' 으로 고친 뒤 다시 git add 한다.")

    if hasErrors:
        print("\n[lint] FAIL — 고친 뒤 다시 git add 하고 커밋한다.")
        return ExitCode.Violation
    what = "저장소 전체" if scope.bWholeRepo else f"스테이지 {len(scope.listFile) + len(scope.listRemoved)}개"
    whole = f"(표가 바뀌어 전체: {', '.join(listWhole)})" if listWhole else ""
    print(f"[lint] OK — {what}, 규칙 {len(kRules)}개{whole} · 포맷")
    return ExitCode.Passed


def main(argv: Sequence[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        return runLint(getProjectRoot(), makeStagedScope(getProjectRoot()))
    if listArgument[0] == "--all":
        return runLint(getProjectRoot(), makeWholeScope(getProjectRoot()))
    if listArgument[0] == "--list":
        print(formatTable())
        return ExitCode.Passed
    rule = kRuleById.get(listArgument[0])
    if rule is None:
        print(f"[lint] 모르는 규칙 '{listArgument[0]}' — `py -3 -m Scripts lint --list`", file=sys.stderr)
        return ExitCode.CheckInvalid
    module = importlib.import_module(rule.getModuleName())
    if hasattr(module, "main") is False:
        print(f"[lint] {rule.id} 는 커밋 훅으로만 돈다(단독 실행 없음)", file=sys.stderr)
        return ExitCode.CheckInvalid
    return module.main(listArgument[1:])


if __name__ == "__main__":
    sys.exit(main())
