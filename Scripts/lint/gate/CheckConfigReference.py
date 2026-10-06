#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckConfigReference.py

설정 참조 문서(`docs/Config/`)가 **지금 코드에서 만든 것과 같은지**, 설정 파일이 모두 목록(`Scripts/common/ConfigCatalog.py`)에 있는지 봅니다.

손으로 쓴 설정 문서는 칸을 더하거나 이름을 바꾸는 날 낡는다. 그래서 문서는 코드(설정 구조체 · 전역 변수 매크로 · `ArgumentList.xxx` ·
CMake 캐시 옵션 · 사용자 설정 스키마)에서 만들고, 이 게이트는 그 결과를 커밋된 파일과 바이트로 비교한다(줄끝은 읽을 때 맞춘다).

검사 규칙:
- `docs/Config/` 의 파일이 생성 결과와 같다 — 없는 파일 · 다른 파일 · 생성하지 않는 남은 파일은 위반
- `Config/` 의 모든 파일 · `Resource/` 의 설정 XML · `Source/` 의 모듈 매니페스트가 목록의 어느 줄에 맞는다
- 목록의 줄이 가리키는 리플렉션 구조체 · `ConfigKeyDoc` 표 · 정본 문서가 있다(선택 줄이 아니면 맞는 파일도 있어야 한다)
- 설정 칸 · 명령줄 줄 · 전역 변수의 설명이 비어 있지 않다

  python Scripts/lint/gate/CheckConfigReference.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common.ConfigReference import buildConfigReference, kConfigReferenceDir  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402


class CheckConfigReferenceGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "설정 참조 문서(docs/Config)가 코드와 같은지 · 설정 파일이 모두 목록에 있는지 검사"
    buildComment = "Checking that docs/Config matches config structs, global variables, arguments and build options..."
    timeoutSeconds = 60
    preCommitPattern = ("Source/*.h", "Source/*.cpp", "Source/*.inl", "Source/*.xxx", "Source/*.module.json", "*CMakeLists.txt", "cmake/*",
                        "CMakePresets.json", "Config/*", "Resource/*.xml", "docs/Config/*", "Scripts/common/ConfigCatalog.py",
                        "Scripts/common/ConfigReference.py")
    preCommitFileArgument = ""
    violationHeader = "설정 참조 문서가 코드와 다르거나, 목록 밖 설정 파일 · 설명 없는 칸"
    hint = (
        "  문서를 다시 만든다:  py -3 Scripts/generate/GenerateConfigReference.py\n"
        "  새 설정 파일은 Scripts/common/ConfigCatalog.py 에 줄을 더한다(층 · 읽는 곳 · 언제 · 배포본 · 커밋).\n"
        "  설명은 코드에 단다 — 설정 칸은 `///< …`, 명령줄 줄은 바로 위 `// …`."
    )
    selfTestCases = [
        {
            "name": "목록에 없는 설정 파일",
            "files": {"Config/Engine/OrphanSettings.json": "{}\n"},
        },
        {
            "name": "손으로 고친 생성 문서",
            "files": {"docs/Config/README.md": "손으로 적은 색인\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        mapOutput, violations = buildConfigReference(repositoryRoot)
        for relative, text in mapOutput.items():
            path = repositoryRoot / relative
            if path.is_file() is False:
                violations.append(f"[Config Reference] 생성 문서가 없습니다: {relative}")
            elif path.read_text(encoding="utf-8") != text:
                violations.append(f"[Config Reference] 생성 문서가 코드와 다릅니다: {relative}")
        outputDir = repositoryRoot / kConfigReferenceDir
        if outputDir.is_dir():
            for path in sorted(outputDir.iterdir()):
                relative = path.relative_to(repositoryRoot).as_posix()
                if path.is_file() and relative not in mapOutput:
                    violations.append(f"[Config Reference] 만들지 않는 파일이 남았습니다: {relative}")
        return GateResult(listViolation=violations, summary=f"생성 문서 {len(mapOutput)}개")


main = CheckConfigReferenceGate.run


if __name__ == "__main__":
    sys.exit(main())
