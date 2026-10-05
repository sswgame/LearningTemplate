#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
엔진 루트(`Source/Engine/` 바로 아래)에 둘 수 있는 파일을 허용 목록으로 막는다.

루트는 기동 · 종료를 엮는 자리(`EngineLoop` · `EngineBootstrap` · `EngineInitSequence` · `EngineServiceCollection`)와
prelude · PCH · 빌드 파일뿐이다. 기능은 자기 폴더에 둔다 — 루트 파일은 층 검사(`CheckEngineLayers`)에서 맨 위 티어라
무엇이든 include 할 수 있어서, 기능 파일이 루트에 앉으면 그 기능이 어느 층을 올려다보는지가 감춰진다.
새 기능 파일의 자리는 `Source/Engine/README.md` 의 티어 표에서 그 파일이 include 하는 폴더보다 높거나 같은 폴더다.

  python Scripts/lint/gate/CheckEngineRootFiles.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kDirSourceEngine  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

# 엔진 루트에 둘 수 있는 파일. 늘리기 전에 그 파일이 기동 · 종료를 엮는 자리인지 묻는다 — 아니면 기능 폴더로 간다.
_kAllowedRootFileName: frozenset[str] = frozenset(
    {
        "CMakeLists.txt",
        "README.md",
        "pch.h",
        "pch.cpp",
        "EngineMinimal.h",
        "EngineLoop.h",
        "EngineLoop.cpp",
        "EngineBootstrap.h",
        "EngineBootstrap.cpp",
        "EngineInitSequence.h",
        "EngineInitSequence.cpp",
        "EngineInitStepList.xxx",
        "EngineServiceCollection.h",
        "EngineServiceCollection.cpp",
    }
)


def collectUnexpectedRootFiles(repositoryRoot: Path) -> list[str]:
    """엔진 루트에서 허용 목록에 없는 파일의 저장소 상대 경로입니다. 엔진 폴더가 없으면 `GateError` 입니다."""
    engineDir = repositoryRoot / kDirSourceEngine
    if not engineDir.is_dir():
        raise GateError(f"{kDirSourceEngine} 가 없습니다 — --root 를 확인하세요")
    return sorted(
        filePath.relative_to(repositoryRoot).as_posix()
        for filePath in engineDir.iterdir()
        if filePath.is_file() and filePath.name not in _kAllowedRootFileName
    )


class CheckEngineRootFilesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "엔진 루트 허용 목록 검사"
    buildComment = "Checking that the Engine root holds only the startup wiring files..."
    timeoutSeconds = 15
    preCommitPattern = ("Source/Engine/*",)
    preCommitFileArgument = ""
    violationHeader = "엔진 루트에 둘 수 없는 파일"
    hint = ("  엔진 루트는 기동 · 종료를 엮는 파일만 둡니다. 기능 파일은 자기 폴더로 옮기세요 — 자리는 그 파일이 include 하는\n"
            "  폴더보다 티어가 높거나 같은 폴더입니다(Source/Engine/README.md 의 티어 표, CheckEngineLayers).")
    selfTestCases = [
        {
            "name": "엔진 루트의 기능 파일",
            "files": {
                "Source/Engine/EngineLoop.cpp": "int engineLoopProbe() { return 0; }\n",
                "Source/Engine/FeatureTools.cpp": "int featureProbe() { return 0; }\n",
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation = [f"{relativePath}: 엔진 루트의 허용 목록에 없습니다" for relativePath in collectUnexpectedRootFiles(repositoryRoot)]
        return GateResult(listViolation=listViolation, summary=f"{kDirSourceEngine} 루트 {len(_kAllowedRootFileName)}개 허용")


main = CheckEngineRootFilesGate.run


if __name__ == "__main__":
    sys.exit(main())
