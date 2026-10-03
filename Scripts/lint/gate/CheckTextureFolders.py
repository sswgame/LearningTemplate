#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckTextureFolders.py

런타임 텍스처 폴더와 원본 텍스처 폴더가 섞이지 않았는지 검사합니다.

런타임은 DDS(BC 압축 · 밉이 이미 된 것)만 읽습니다. 원본 이미지(PNG · JPG …)는 `textures_raw/` 에 두고
`App --bake-textures`(또는 에디터의 핫 리로드)가 같은 상대 경로의 `textures/*.dds` 로 굽습니다. 쿠킹은
`textures_raw/` 를 팩에서 뺍니다.

검사 규칙:
- `textures/` 아래에는 `.dds` 와 데이터(`.json` — 스프라이트 클립 · `.meta`)만 둡니다.
- 원본 이미지 확장자는 `textures_raw/` 아래에만 둡니다(Resource 어디든).
- `textures_raw/` 아래에 `.dds` 를 두지 않습니다(구운 결과는 `textures/` 로 갑니다).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: 런타임 텍스처 폴더 이름.
_kBakedFolderName = "textures"
#: 원본 텍스처 폴더 이름.
_kRawFolderName = "textures_raw"
#: 런타임 텍스처 폴더에 둘 수 있는 확장자(구운 텍스처와 그 옆 데이터).
_kBakedFolderSuffix = frozenset({".dds", ".json", ".meta"})
#: 원본 이미지 확장자 — `textures_raw/` 밖에 있으면 런타임이 읽지 못하는 파일이다.
_kSourceImageSuffix = frozenset({".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".psd", ".exr", ".gif"})


def findTextureFolderViolations(resourceRelativePaths: Iterable[str]) -> list[str]:
    """`Resource/` 기준 상대 경로(`/` 구분) 목록에서 위반 줄을 모읍니다."""
    violations: list[str] = []
    for relPath in sorted(resourceRelativePaths):
        parts = relPath.split("/")
        folders = parts[:-1]
        suffix = os.path.splitext(parts[-1])[1].lower()
        inRaw = _kRawFolderName in folders
        inBaked = _kBakedFolderName in folders

        if inBaked and suffix not in _kBakedFolderSuffix:
            violations.append(f"[Texture Folder] `textures/` 에는 .dds 와 데이터만 둡니다: Resource/{relPath}")
        elif suffix in _kSourceImageSuffix and not inRaw:
            violations.append(f"[Texture Folder] 원본 이미지는 `textures_raw/` 에 둡니다: Resource/{relPath}")
        elif inRaw and suffix == ".dds":
            violations.append(f"[Texture Folder] 구운 DDS 는 `textures/` 에 둡니다: Resource/{relPath}")
    return violations


def collectResourcePathsInternal(projectRoot: Path, targetFiles: Sequence[str] | None) -> list[str]:
    """검사할 파일을 `Resource/` 기준 상대 경로로 모읍니다. 파일 목록이 오면(커밋 훅) 그것만 봅니다."""
    resourceRoot = (projectRoot / "Resource").resolve()
    if not resourceRoot.is_dir():
        return []

    if targetFiles is not None:
        result: list[str] = []
        for filePath in targetFiles:
            path = Path(filePath)
            if not path.is_absolute():
                path = projectRoot / path
            try:
                result.append(path.resolve().relative_to(resourceRoot).as_posix())
            except ValueError:
                continue
        return result

    result = []
    for root, _dirs, files in os.walk(resourceRoot):
        relRoot = Path(root).relative_to(resourceRoot).as_posix()
        for fileName in files:
            result.append(fileName if relRoot == "." else f"{relRoot}/{fileName}")
    return result


class CheckTextureFoldersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "런타임 텍스처 폴더(textures/)에는 DDS 만, 원본 이미지는 textures_raw/ 에만"
    buildComment = "Checking runtime texture folders hold only baked DDS..."
    timeoutSeconds = 15
    preCommitPattern = ("Resource/*",)
    preCommitFileArgument = "positional"
    violationHeader = "텍스처 폴더 규칙 위반"
    hint = ("  원본 이미지는 같은 상대 경로의 `textures_raw/` 로 옮기고 `App --bake-textures` 로 구워 DDS 와 "
            "`textures_raw/bake.stamp` 를 함께 커밋합니다. 참조가 없는 원본은 지웁니다.")
    selfTestCases = [
        {
            "name": "런타임 폴더의 PNG",
            "files": {
                "Resource/engine/textures/white.png": "probe",
            },
        },
        {
            "name": "textures_raw 밖의 원본 이미지",
            "files": {
                "Resource/game/probe/art/hero.jpg": "probe",
            },
        },
        {
            "name": "textures_raw 안의 DDS",
            "files": {
                "Resource/editor/textures_raw/splash.dds": "probe",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("files", nargs="*", help="검사할 특정 파일 경로 목록 (생략 시 전체 Resource/ 검사)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listPath = collectResourcePathsInternal(repositoryRoot, args.files or None)
        violations = findTextureFolderViolations(listPath)
        return GateResult(listViolation=violations, summary=f"Resource 파일 {len(listPath)}개")


main = CheckTextureFoldersGate.run


if __name__ == "__main__":
    sys.exit(main())
