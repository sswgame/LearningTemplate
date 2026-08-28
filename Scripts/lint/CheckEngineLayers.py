#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 RuntimeAPI/Service/GameService.h 의 game:: 만 사용).
  3) 의도 레이어(문서):
       Common/Utility → Reflection/Serialization → Object/Scene → Graphics/Input/Audio/Window
       Physics / Animation 은 experimental stub.

  python Scripts/lint/CheckEngineLayers.py [--root <repo>] [--strict]
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import (
    collectSourceFiles,
    getProjectRoot,
    kDirSourceEngine,
    kDirSourceGameFramework,
    kDirSourceGames,
    kFileEngineServices,
)

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def includeHitsBanInternal(includePath: str, bannedPattern: str) -> bool:
    normalizedParts = includePath.replace("\\", "/").split("/")
    bannedParts = [part for part in bannedPattern.replace("\\", "/").split("/") if part]
    if not bannedParts:
        return False
    for partIndex in range(len(normalizedParts) - len(bannedParts) + 1):
        if normalizedParts[partIndex : partIndex + len(bannedParts)] == bannedParts:
            return True
    return False


_kForbiddenRules: list[tuple[str, tuple[str, ...]]] = [
    (
        f"{kDirSourceEngine}/",
        (
            "Editor/",
            "Source/Editor/",
            "GameFramework/",
            "Games/",
            f"{kDirSourceGames}/",
        ),
    ),
    (
        f"{kDirSourceGames}/",
        (kFileEngineServices, "EngineServices.h"),
    ),
    (
        f"{kDirSourceGameFramework}/",
        (kFileEngineServices, "EngineServices.h"),
    ),
]

# 엄격 모드(--strict): 상위 레이어를 역참조하는 인클루드 관계 목록
_kStrictReverse: list[tuple[str, str]] = [
    ("Utility", "Graphics"),
    ("Reflection", "Object"),
    ("Physics", "Graphics"),
    ("Animation", "Graphics"),
]


def processFile(filePath: Path, repositoryRoot: Path, strict: bool) -> tuple[list[str], list[str], str | None]:
    relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="strict")
    except UnicodeDecodeError as exception:
        return [], [], f"[CheckEngineLayers] UTF-8 인코딩 오류: {relativeFilePath}: {exception}"
    except OSError as exception:
        return [], [], f"[CheckEngineLayers] 읽기 실패: {relativeFilePath}: {exception}"

    fileViolations: list[str] = []
    fileStrictWarns: list[str] = []
    prefix = f"{kDirSourceEngine}/"

    for rulePrefix, bannedList in _kForbiddenRules:
        if not relativeFilePath.startswith(rulePrefix):
            continue
        for includePath in _kIncludeRe.findall(text):
            normalizedInclude = includePath.replace("\\", "/")
            for bannedPattern in bannedList:
                if includeHitsBanInternal(normalizedInclude, bannedPattern):
                    fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (금지: {bannedPattern})')

    sourceLayer = ""
    if relativeFilePath.startswith(prefix) and "/" in relativeFilePath[len(prefix) :]:
        sourceLayer = relativeFilePath[len(prefix) :].split("/", 1)[0]

    # ReflectGenerated.h는 .gen.cpp 리플렉션 생성 코드 전용 preamble이며, ResourceManager.cpp는 파사드 구현체입니다.
    if relativeFilePath.endswith("ReflectGenerated.h") or relativeFilePath.endswith("ResourceManager.cpp"):
        return fileViolations, [], None

    for includePath in _kIncludeRe.findall(text):
        normalizedInclude = includePath.replace("\\", "/")
        if "Engine/" not in normalizedInclude:
            continue
        destParts = normalizedInclude.split("Engine/", 1)[-1].split("/")
        if not destParts:
            continue
        destLayer = destParts[0]
        for sourceBannedLayer, destBannedLayer in _kStrictReverse:
            if sourceLayer == sourceBannedLayer and destLayer == destBannedLayer:
                violationMessage = f'{relativeFilePath}: #include "{includePath}"  (레이어 {sourceBannedLayer}->{destBannedLayer})'
                if strict:
                    fileViolations.append(violationMessage)
                else:
                    fileStrictWarns.append(violationMessage)

    return fileViolations, fileStrictWarns, None


def main() -> int:
    parser = argparse.ArgumentParser(description="Engine 레이어 금지 include 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    parser.add_argument("--strict", action="store_true", help="내부 reverse-edge도 실패로 처리")
    args = parser.parse_args()
    repo = (args.root or getProjectRoot()).resolve()
    engineDir = repo / kDirSourceEngine
    if not engineDir.is_dir():
        print(f"[CheckEngineLayers] Engine 경로 없음: {engineDir}", file=sys.stderr)
        return 2

    scanRoots = [engineDir, repo / kDirSourceGames, repo / kDirSourceGameFramework]
    allFiles = collectSourceFiles(scanRoots)

    violations: list[str] = []
    strictWarns: list[str] = []

    maxWorkers = min(32, (os.cpu_count() or 4) * 2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(processFile, path, repo, args.strict) for path in allFiles]
        for future in concurrent.futures.as_completed(futures):
            fileViolations, fileStrictWarns, errorMessage = future.result()
            if errorMessage:
                print(errorMessage, file=sys.stderr)
                return 2
            violations.extend(fileViolations)
            strictWarns.extend(fileStrictWarns)

    if strictWarns and not args.strict:
        print(f"[CheckEngineLayers] 내부 레이어 경고 {len(strictWarns)}건 (--strict 시 실패):")
        for line in strictWarns[:20]:
            print(f"  - {line}")
        if len(strictWarns) > 20:
            print(f"  ... +{len(strictWarns) - 20} more")

    if violations:
        print("[CheckEngineLayers] 레이어 위반:")
        for line in violations:
            print(f"  - {line}")
        return 1

    print(f"[CheckEngineLayers] OK ({len(allFiles)} files scanned in parallel)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
