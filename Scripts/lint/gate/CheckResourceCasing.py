#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckResourceCasing.py

Resource/ 디렉터리 하위의 모든 파일 및 폴더명이 완전한 소문자(Lowercase)인지 검사합니다.
Linux ext4 등 대소문자 구분 파일시스템 호환성 및 엔진 에셋 명명 표준을 강제합니다.

검사 규칙:
- Resource/ 하위의 모든 디렉터리 이름은 소문자여야 합니다 (대문자 금지).
- Resource/ 하위의 모든 파일 이름은 소문자여야 합니다 (대문자 금지, README.md 제외).
- 위반 사항 발견 시 0이 아닌 종료 코드를 반환하여 Git 커밋 및 CI를 중단시킵니다.

규칙 데이터(예외 표 · 목록)는 `Scripts/lint/rules/CheckResourceCasing.toml` 에 있다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

def findCasingViolations(repositoryRoot: Path, listPath: Sequence[Path]) -> list[str]:
    """Resource/ 아래 파일의 경로 조각(폴더 · 파일 이름)에 대문자가 있으면 위반입니다(README.md 만 예외). 빈 폴더는 git 이 들지 않으므로 파일 경로로 본다."""
    violations: list[str] = []
    for path in listPath:
        relative = path.resolve().relative_to(repositoryRoot.resolve())
        for part in relative.parts[1:]:
            if part in CheckResourceCasingGate.mapExemption:
                CheckResourceCasingGate.useExemption(part)
                continue
            if any(character.isupper() for character in part):
                violations.append(f"[Resource Casing] 대문자가 포함된 리소스 경로: {relative.as_posix()}")
                break
    return violations


class CheckResourceCasingGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "Resource 하위 소문자 명명 규칙 검사"
    buildComment = "Checking Resource lowercase casing rules..."
    timeoutSeconds = 15
    preCommitPattern = ()
    preCommitFileArgument = "--files"
    violationHeader = "Resource 소문자 규칙 위반"
    hint = "  Resource/ 하위의 모든 파일/폴더는 반드시 소문자여야 합니다 (README.md 만 예외)."
    selfTestCases = [
        {
            "name": "대문자 리소스 경로",
            "files": {
                "Resource/engine/Textures/Splash.png": "probe",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 파일 (생략 시 전체 Resource/)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Resource",), bAnySuffix=True)
        violations = findCasingViolations(repositoryRoot, listPath)
        return GateResult(listViolation=violations, summary="Resource 하위 모든 파일/폴더 소문자")


main = CheckResourceCasingGate.run


if __name__ == "__main__":
    sys.exit(main())
