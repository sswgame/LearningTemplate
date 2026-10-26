#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckNextSize.py

`docs/plans/NEXT.md` 가 일지로 자라지 않았는지 검사합니다. NEXT 는 지금 상태 한 장이라 크기가 20KB 를 넘거나 `## ` 절이 셋을 넘으면 위반입니다
(고정 세 절: 마지막 통합 한 줄 요약 / 남은 일 / 확인할 것).

켜는 시점: 파일에 `<!-- next-format: one-page -->` 줄이 있을 때만 검사합니다. 통합 담당이 새 형식으로 덮어쓰면서 그 줄을 넣으면 그 순간부터 막고,
넣지 않은 옛 형식 NEXT 는 건너뜁니다(옛 형식이 이미 한도를 넘어 그대로 켜면 모든 커밋이 막힌다).

게이트를 두는 이유: 문서 규칙만으로는 NEXT 에 병합마다 절이 쌓이는 것을 막지 못했다. 글자 수 · 절 수는 형식으로 셀 수 있어 게이트가 지킨다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import GateResult, LintGate  # noqa: E402

kNextRelPath = "docs/plans/NEXT.md"
kMarker = "<!-- next-format: one-page -->"
kMaxBytes = 20 * 1024
kMaxSections = 3


class CheckNextSizeGate(LintGate):
    """NEXT.md 가 20KB 와 `## ` 절 셋 안에 드는지(새 형식 표시가 있을 때)."""

    description = "docs/plans/NEXT.md 가 20KB · 절 셋 한도 안인지 검사(새 형식 표시가 있을 때만)"
    buildComment = "Checking NEXT.md size..."
    timeoutSeconds = 30
    preCommitPattern = (kNextRelPath,)
    preCommitFileArgument = "--files"
    violationHeader = "NEXT.md 크기 위반"
    hint = "  NEXT 는 일지가 아니라 지금 상태 한 장입니다. 고정 세 절(마지막 통합 한 줄 요약 / 남은 일 / 확인할 것)만 두고 통합이 끝날 때 덮어씁니다."
    selfTestCases = [
        {
            "name": "절이 넷",
            "expectedViolation": "`## ` 절이 4개",
            "files": {kNextRelPath: f"# NEXT\n{kMarker}\n## A\n## B\n## C\n## D\n"},
        },
        {
            "name": "20KB 초과",
            "expectedViolation": "바이트로 20KB 를 넘었습니다",
            "files": {kNextRelPath: f"# NEXT\n{kMarker}\n## A\n" + "x" * (kMaxBytes + 1) + "\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "커밋 훅이 넘기는 staged 파일 (생략 시 전부 검사)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        path = repositoryRoot / kNextRelPath
        if not path.is_file():
            return GateResult(summary="NEXT.md 가 없어 건너뜀")
        raw = path.read_bytes()
        text = raw.decode("utf-8")
        if kMarker not in text:
            return GateResult(summary="NEXT.md 가 옛 형식이라 건너뜀(새 형식 표시 없음)")
        listViolation: list[str] = []
        if len(raw) > kMaxBytes:
            listViolation.append(f"{kNextRelPath}: {len(raw)} 바이트로 20KB 를 넘었습니다")
        countSection = sum(1 for line in text.splitlines() if line.startswith("## "))
        if countSection > kMaxSections:
            listViolation.append(f"{kNextRelPath}: `## ` 절이 {countSection}개로 {kMaxSections}개를 넘었습니다")
        if listViolation:
            return GateResult(listViolation=listViolation)
        return GateResult(summary="NEXT.md 가 한도 안")


main = CheckNextSizeGate.run


if __name__ == "__main__":
    sys.exit(main())
