#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommit.py

린트 입구 하나 — 규칙은 `Rules.py` 의 표 한 곳에 있다.

  py -3 -m Scripts lint                 # 커밋 훅: 스테이지된 파일 종류에 걸리는 규칙만 돈다 + 에셋 참조 + clang-format
  py -3 -m Scripts lint --list          # 규칙 표(갈래 · 막는 것 · 왜 린트인가 · 예외 파일)
  py -3 -m Scripts lint <id> [인자…]    # 규칙 하나의 단독 실행(`--all` · `--self-check` 등은 그 모듈의 인자)

**통과한 것은 찍지 않는다.** 통과는 한 줄, 실패만 상세를 찍는다. 고치는 것은 `py -3 -m Scripts format` · `format-includes` 다.
"""

from __future__ import annotations

import importlib
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ProjectConstants import kShaderSourceExtensions  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Git import getAllStagedFiles  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.Format import getStagedCppFiles, runClangFormatBatch  # noqa: E402
from lint.AssetReferences import checkStagedAssetReferences  # noqa: E402
from lint.Rules import Rule, formatTable, isTriggered, kRuleById, kRules  # noqa: E402


def runRule(rule: Rule, projectRoot: Path, listStagedCpp: list[str]) -> list[str]:
    moduleName, _, functionName = rule.check.partition(":")
    check = getattr(importlib.import_module(moduleName), functionName)
    return check(projectRoot, listStagedCpp) if rule.perFile else check(projectRoot)


def runStaged(projectRoot: Path) -> int:
    # 에셋 참조는 스테이지 목록(더하기 · 고치기)보다 먼저 돈다 — 에셋을 지우거나 옮기기만 한 커밋이 남긴 역참조도 잡아야 한다(AssetReferences.py 머리말).
    hasErrors = False
    listAssetProblem = checkStagedAssetReferences(projectRoot)
    if listAssetProblem:
        hasErrors = True
        print(f"[asset-refs] FAIL — 끊어진 에셋 참조 {len(listAssetProblem)}건(옮기기는 `py -3 -m Scripts move-asset <옛> <새>` 가 참조를 함께 고친다)")
        for line in listAssetProblem:
            print(f"  - {line}")

    listStagedName = [path.replace("\\", "/") for path in map(str, getAllStagedFiles(projectRoot))]
    if not listStagedName:
        return ExitCode.Violation if hasErrors else ExitCode.Passed

    listStagedCpp = [str(path) for path in getStagedCppFiles(projectRoot)]
    listRan: list[str] = []
    for rule in kRules:
        if isTriggered(rule, listStagedName) is False:
            continue
        if rule.perFile and not listStagedCpp:
            continue
        listRan.append(rule.id)
        listProblem = runRule(rule, projectRoot, listStagedCpp)
        if listProblem:
            hasErrors = True
            where = f" — 예외 · 기준선: {rule.exceptions}" if rule.exceptions else ""
            print(f"[{rule.id}] FAIL — {rule.blocks} {len(listProblem)}건{where}")
            for line in listProblem:
                print(f"  - {line}")

    stagedFormat = getStagedCppFiles(projectRoot) + getStagedCppFiles(projectRoot, extensions=kShaderSourceExtensions)
    if stagedFormat and runClangFormatBatch(stagedFormat, checkOnly=True, cwd=projectRoot) != 0:
        hasErrors = True
        print("[format] FAIL — 'py -3 -m Scripts format' 으로 고친 뒤 다시 git add 한다.")

    if hasErrors:
        print("\n[lint] FAIL — 고친 뒤 다시 git add 하고 커밋한다.")
        return ExitCode.Violation
    print(f"[lint] OK — 스테이지 {len(listStagedName)}개, 규칙 {len(listRan)}개({', '.join(listRan) or '없음'}) · 포맷")
    return ExitCode.Passed


def main(argv: Sequence[str] | None = None) -> int:
    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        return runStaged(getProjectRoot())
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
