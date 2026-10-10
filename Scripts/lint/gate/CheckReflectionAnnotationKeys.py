#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckReflectionAnnotationKeys.py

커밋된 편집기용 어노테이션 키 헤더(`Source/Engine/Reflection/ReflectionAnnotationKeys.h`)가 원본(`AnnotationMeta.txt` ·
`ReflectUnits.h` · `PredefinedContainerKind.xxx`)과 같은지 검사합니다.

이 헤더는 편집기만 읽어 빌드와 테스트가 낡은 것을 잡지 못합니다. 낡으면 새 키가 편집기에서 오류로 보이거나 지운 키가 완성 목록에 남습니다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common.ReflectionAnnotationKeys import buildAnnotationKeysFiles  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402


class CheckReflectionAnnotationKeysGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 곳에 있다."""

    description = "편집기용 어노테이션 키 헤더가 AnnotationMeta.txt · 단위 표와 같은지 검사"
    buildComment = "Checking that the editor annotation key header matches AnnotationMeta.txt..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/Core/Predefined/AnnotationMeta.txt", "Source/Core/Predefined/PredefinedContainerKind.xxx",
                        "Source/Engine/Reflection/ReflectUnits.h", "Source/Engine/Reflection/ReflectionAnnotationKeys.h",
                        "Scripts/common/ReflectionAnnotationKeys.py")
    preCommitFileArgument = ""
    violationHeader = "편집기용 어노테이션 키 헤더가 원본과 다름"
    hint = "  다시 만든다:  py -3 Scripts/generate/GenerateReflectionAnnotationKeys.py"
    selfTestCases = [
        {
            "name": "손으로 고친 키 헤더",
            "files": {
                "Source/Core/Predefined/AnnotationMeta.txt": "[REFLECT]\nflag.Abstract = Abstract\n[ENUM]\nflag.Flags = Flags\n"
                                                             "[PROPERTY]\nflag.ReadOnly = ReadOnly\n[FUNCTION]\nflag.Reliable = Reliable\n",
                "Source/Engine/Reflection/ReflectUnits.h": "kArrReflectUnit[] = {\n{ \"m\", ReflectUnitDimension::Length, 1.0 },\n};\n",
                "Source/Core/Predefined/PredefinedContainerKind.xxx": "REGISTER_CONTAINER_KIND( Sequence )\n",
                "Source/Engine/Reflection/ReflectionAnnotationKeys.h": "// hand edited\n",
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        try:
            mapFile = buildAnnotationKeysFiles(repositoryRoot)
        except (OSError, ValueError) as error:
            raise GateError(f"[AnnotationKeys] 원본을 읽지 못했습니다: {error}") from error
        violations = []
        for relative, data in mapFile.items():
            path = repositoryRoot / relative
            if path.is_file() is False:
                violations.append(f"[AnnotationKeys] 파일이 없습니다: {relative}")
            elif path.read_bytes().replace(b"\r\n", b"\n") != data:
                violations.append(f"[AnnotationKeys] 원본과 다릅니다: {relative}")
        return GateResult(listViolation=violations, summary=f"생성 파일 {len(mapFile)}개")


main = CheckReflectionAnnotationKeysGate.run


if __name__ == "__main__":
    sys.exit(main())
