#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckWorkflowPresets.py

CI 워크플로(`.github/workflows/*.yml`)가 글로 적은 프리셋 이름이 `CMakePresets.json` 에 실제로 있는지 검사합니다.

워크플로는 YAML 이라 파이썬 도구처럼 프리셋 표를 코드로 읽을 수 없습니다. 프리셋 이름을 바꾸거나 지우면 CI 가 push 뒤에야 `cmake --preset` 에서 멈춥니다.
형식으로 막지 못하는 틈이라 게이트로 둡니다.

검사 규칙: `preset: <이름>`(매트릭스 항목) · `--preset <이름>`(`cmake` · 도구 인자) · `'<이름>'` 과 견주는 `matrix.preset == '<이름>'` 의 이름이 configure 프리셋 이름 중 하나여야 합니다.
`${{ … }}` 식은 값이 아니므로 보지 않습니다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Search import collectRepositoryFiles  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402

_kWorkflowFolder = ".github/workflows"
#: 워크플로가 프리셋 이름을 적는 세 모양 — 매트릭스 항목, 명령줄 인자, `matrix.preset == '이름'` 비교.
_kPresetRe = re.compile(r"(?:^\s*(?:-\s+)?preset:\s*|--preset\s+|matrix\.preset\s*==\s*')([A-Za-z][\w.+-]*)")


def findWorkflowPresetViolations(repositoryRoot: Path) -> tuple[list[str], int]:
    """(위반 줄, 검사한 워크플로 수)를 돌려줍니다."""
    presetPath = repositoryRoot / "CMakePresets.json"
    workflowRoot = repositoryRoot / _kWorkflowFolder
    if workflowRoot.is_dir() is False:
        return [], 0
    if presetPath.is_file() is False:
        return ["[Workflow Preset] CMakePresets.json 이 없습니다 — 워크플로의 프리셋 이름을 견줄 수 없습니다"], 0
    try:
        setName = {preset["name"] for preset in json.loads(presetPath.read_text(encoding="utf-8")).get("configurePresets", [])}
    except ValueError as error:
        return [f"[Workflow Preset] CMakePresets.json 을 읽지 못했습니다 ({error})"], 0
    violations: list[str] = []
    listWorkflow = collectRepositoryFiles(repositoryRoot, (_kWorkflowFolder,), suffixes=(".yml",))
    for workflow in listWorkflow:
        for lineNumber, line in enumerate(workflow.read_text(encoding="utf-8").splitlines(), start=1):
            for name in _kPresetRe.findall(line):
                if name not in setName:
                    violations.append(f"[Workflow Preset] {_kWorkflowFolder}/{workflow.name}:{lineNumber} 의 프리셋 `{name}` 이 CMakePresets.json 에 없습니다")
    return violations, len(listWorkflow)


class CheckWorkflowPresetsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "CI 워크플로가 적은 프리셋 이름이 CMakePresets.json 에 있다"
    buildComment = "Checking workflow preset names against CMakePresets.json..."
    timeoutSeconds = 15
    preCommitPattern = (".github/workflows/*.yml", "CMakePresets.json")
    preCommitFileArgument = ""
    violationHeader = "워크플로 프리셋 위반"
    hint = "  `CMakePresets.json` 에 있는 configure 프리셋 이름으로 고칩니다. 프리셋 이름을 바꿨으면 워크플로의 `preset:` · `--preset` 도 함께 바꿉니다."
    selfTestCases = [
        {
            "name": "없는 프리셋을 매트릭스에 적은 워크플로",
            "expectedViolation": "ci.yml:3 의 프리셋 `Gone-Debug` 이 CMakePresets.json 에 없습니다",
            "files": {
                "CMakePresets.json": '{ "configurePresets": [ { "name": "CI-Debug" } ] }',
                ".github/workflows/ci.yml": "matrix:\n  include:\n    - preset: Gone-Debug\n",
            },
        },
        {
            "name": "없는 프리셋을 명령줄에 적은 워크플로",
            "expectedViolation": "ci.yml:1 의 프리셋 `CI-Gone` 이 CMakePresets.json 에 없습니다",
            "files": {
                "CMakePresets.json": '{ "configurePresets": [ { "name": "CI-Debug" } ] }',
                ".github/workflows/ci.yml": "run: cmake --preset CI-Gone\n",
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations, workflowCount = findWorkflowPresetViolations(repositoryRoot)
        return GateResult(listViolation=violations, checkedCount=workflowCount, summary=f"워크플로 {workflowCount}개")


main = CheckWorkflowPresetsGate.run


if __name__ == "__main__":
    sys.exit(main())
