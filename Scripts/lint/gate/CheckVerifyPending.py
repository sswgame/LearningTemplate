#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckVerifyPending.py

통합에서 돌릴 확인의 정본 `Test/QA/VerifyBatch.json` 이 읽히는지, 가리키는 파일 · 프리셋 · 테스트 케이스가 있는지, 계획 문서 · 후보 문서의
대기 줄(`확인 대기` · `실행 대기` · `통합에서 확인` · `**남은 확인`)이 글 대신 `매니페스트 id` 로 항목을 가리키는지 검사합니다.
판정은 `common/VerifyManifest.checkVerifyPending` 한 곳이고 `py -3 -m Scripts plans --check` 와 `verify-batch --check` 가 같은 함수를 부릅니다.

게이트를 두는 이유: 문서 안의 글은 형식으로 막을 수 없다. 대기를 글로만 적으면 통합이 문서마다 찾아 항목마다 따로 돌리게 되어(측정 후보가 쌓이는 원인)
대기는 항목 id 로만 적게 하고, 없는 id · 지운 항목을 가리키는 줄 · 가리키는 줄이 없는 일회성 항목(`pending`)을 여기서 막는다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.VerifyManifest import checkVerifyPending, kManifestRelPath  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402

_kPlanText = "# 계획\n\n> 상태: 구현중 · 선행: 없음 · 영역: engine · 옛번호: 없음 · 진행: 해당없음\n\n"
_kManifestText = (
    '{"configs": [{"name": "Debug", "preset": "Ninja-Debug"}], "items": [{"id": "debug-full-build", "title": "빌드", "kind": "build", "config": "Debug", '
    '"platform": "windows", "gpu": "none", "pending": false, "timeout_minutes": 10, "source": "docs/plans/A.md 절", "result_doc": "docs/plans/A.md 절"}]}'
)


class CheckVerifyPendingGate(LintGate):
    """검증 묶음 매니페스트가 맞고, 문서의 대기 줄이 글 대신 매니페스트 id 를 가리키는지."""

    description = "Test/QA/VerifyBatch.json 이 맞고 계획 문서의 확인 대기 줄이 `매니페스트 id` 로 항목을 가리키는지 검사"
    buildComment = "Checking verify-batch manifest and pending lines in plan docs..."
    timeoutSeconds = 60
    preCommitPattern = ("docs/plans/*", kManifestRelPath)
    preCommitFileArgument = ""
    violationHeader = "검증 묶음 위반"
    hint = "  대기는 글로 적지 않고 `Test/QA/VerifyBatch.json` 에 항목을 더한 뒤 문서에는 `매니페스트 id` 와 항목 id 만 적습니다(docs/11_Workflow.md 3절)."
    selfTestCases = [
        {
            "name": "대기 줄에 id 가 없다",
            "expectedViolation": "대기 줄에는 글 대신",
            "files": {"Test/QA/VerifyBatch.json": _kManifestText, "docs/plans/A.md": _kPlanText + "실행 대기: 통합에서 돌린다.\n"},
        },
        {
            "name": "없는 id 를 가리킨다",
            "expectedViolation": "매니페스트 id `missing-item` 가",
            "files": {"Test/QA/VerifyBatch.json": _kManifestText, "docs/plans/A.md": _kPlanText + "확인 대기: 매니페스트 id `missing-item`.\n"},
        },
        {
            "name": "측정대기 문서가 돌릴 항목을 가리키지 않는다",
            "expectedViolation": "돌릴 명령이 있는 항목을 가리키는",
            "files": {"Test/QA/VerifyBatch.json": _kManifestText, "docs/plans/A.md": _kPlanText.replace("구현중", "측정대기") + "본문\n"},
        },
        {
            "name": "대기 줄이 있는데 매니페스트가 없다",
            "expectedViolation": "VerifyBatch.json 가 없습니다",
            "files": {"docs/plans/A.md": _kPlanText + "실행 대기: 통합에서 돌린다.\n"},
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation = checkVerifyPending(repositoryRoot)
        if listViolation:
            return GateResult(listViolation=listViolation)
        return GateResult(summary="검증 묶음 매니페스트와 문서의 대기 줄이 맞음")


main = CheckVerifyPendingGate.run


if __name__ == "__main__":
    sys.exit(main())
