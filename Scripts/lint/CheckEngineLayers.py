#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 GameFramework/Base/GameService.h 의 game:: 만 사용).
  3) Engine 내부 티어: 아래 티어가 위 티어를 include 하지 못한다 (_kEngineTier).

티어는 **include 그래프에서 계산한 것**이다. 예전에는 손으로 고른 네 쌍(Utility->Graphics 등)만
경고로 찍고 실패시키지 않았다 — 근거 없는 목록이라 늘릴 기준도 없고, 실패하지 않으니 쌓여도
아무도 몰랐다. 지금은 전체 그래프를 Tarjan SCC 로 줄이고 위상 순서를 티어로 쓴다.

  python Scripts/lint/CheckEngineLayers.py [--root <repo>] [--strict]

(--strict 는 남겨 두었지만 이제 기본 동작과 같다. 티어 위반은 항상 실패다.)
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
    normalizePath,
    startsWithPathComponent,
    useUtf8Stdout,
)

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def includeHitsBanInternal(includePath: str, bannedPattern: str) -> bool:
    normalizedParts = normalizePath(includePath).split("/")
    bannedParts = [part for part in normalizePath(bannedPattern).split("/") if part]
    if not bannedParts:
        return False
    for partIndex in range(len(normalizedParts) - len(bannedParts) + 1):
        if normalizedParts[partIndex : partIndex + len(bannedParts)] == bannedParts:
            return True
    return False


_kForbiddenRules: list[tuple[str, tuple[str, ...]]] = [
    (
        kDirSourceEngine,
        (
            "Editor/",
            "Source/Editor/",
            "GameFramework/",
            "Games/",
            kDirSourceGames,
        ),
    ),
    (
        kDirSourceGames,
        (kFileEngineServices, "EngineServices.h"),
    ),
    (
        kDirSourceGameFramework,
        (kFileEngineServices, "EngineServices.h"),
    ),
]

# Engine 최상위 폴더를 담지 않는 파일(EngineLoop.cpp 등)의 가상 티어 이름.
_kRootLayerName = "<root>"

# ------------------------------------------------------------------------------
# Engine 내부 티어 — 숫자가 큰 쪽이 위다. 같은 티어끼리는 서로 참조해도 된다.
#
# 이 표는 include 그래프를 Tarjan SCC 로 줄여 위상 정렬해서 얻었다. 손으로 고른 순서가 아니므로,
# 코드가 바뀌면 표도 다시 계산해야 한다.
#
# **티어 4 의 일곱 폴더는 하나의 강결합 묶음이다.** Graphics·Module·Object·Resource·Scene·
# Sequencer·Window 이 서로 도달 가능하다(씬이 에셋을 읽고, 컴포넌트가 머티리얼을 들고, 핫리로드가
# 씬의 TypeInfo 를 다시 묶고, 그래픽스가 스왑체인 때문에 창을 안다). 그 안에는 지킬 수 있는
# 순서가 없으므로 순서를 주장하지 않는다 — 남은 엣지와 푸는 순서는 docs/06_Backlog.md 에 있다.
#
# 예전에는 이 묶음이 **열 개**였다. Reflection·Serialization·Config 가 끌려 들어가 있었고, 원인은
# 세 줄이었다: 직렬화기가 TagID·ComponentHandle 때문에 Object 를 include 했고(두 타입 모두
# Core 기능만 쓰는 값 타입인데 Object/Component/ 에 있었다), Reflection 이 ReflectAny·Rpc 의
# 인코딩 때문에 Serialization 을 include 했고, EngineConfig 가 RHIBackend 이름 하나 때문에
# RHITypes.h(732줄) 전체를 끌어왔다.
# ------------------------------------------------------------------------------
_kEngineTier: dict[str, int] = {
    # 0: 토대 — Engine 의 어느 것도 참조하지 않는다.
    "Common": 0,
    "Physics": 0,
    "Utility": 0,
    # 1: 리플렉션과, 코어가 쓰는 잎 서브시스템.
    "Animation": 1,
    "Audio": 1,
    "Localization": 1,
    "Reflection": 1,
    "Spatial": 1,
    # 2: 리플렉션 위에 올라가는 직렬화.
    "Dialogue": 2,
    "Serialization": 2,
    # 3: 설정 — 리플렉션·직렬화로 읽히고, 코어가 읽는다.
    "Config": 3,
    # 4: 코어 묶음 (강결합). 내부 순서는 없다.
    "Graphics": 4,
    "Module": 4,
    "Object": 4,
    "Resource": 4,
    "Scene": 4,
    "Sequencer": 4,
    "Window": 4,
    # 5: 코어 위에 올라가는 것.
    "Input": 5,
    # 6: 전부를 엮는 자리.
    _kRootLayerName: 6,
}

# 티어가 아니라 **prelude·경로 헬퍼**인 헤더. 어느 티어에서 include 해도 된다.
#   - EngineMinimal.h / Common.h : 타입 별칭과 전방 선언만 모은 우산 헤더.
#   - ResourceUtil.h             : 리소스 경로 해석 static 헬퍼. loadFromResource 진입점이 쓴다.
_kUbiquitousHeaders: frozenset[str] = frozenset(
    {
        "Engine/EngineMinimal.h",
        "Engine/Common/Common.h",
        "Engine/Resource/ResourceUtil.h",
    }
)

# 티어가 아니라 **배선**인 파일. 모든 서브시스템을 알아야 하므로 티어 검사에서 뺀다.
#   - EngineServices.cpp   : 서비스 로케이터 구현. 노출하는 모든 매니저를 include 해야 한다.
#   - ReflectGenerated.h   : .gen.cpp 전용 preamble.
#   - ResourceManager.cpp  : 리소스 파사드 구현.
_kWiringFiles: frozenset[str] = frozenset(
    {
        "Source/Engine/Common/EngineServices.cpp",
        "Source/Engine/Reflection/ReflectGenerated.h",
        "Source/Engine/Resource/ResourceManager.cpp",
    }
)


def engineTierOfInternal(folderName: str) -> int | None:
    """Engine 최상위 폴더 이름의 티어. 표에 없으면 None (새 폴더 = 검사 실패)."""
    return _kEngineTier.get(folderName)


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

    for rulePrefix, bannedList in _kForbiddenRules:
        if not startsWithPathComponent(relativeFilePath, rulePrefix):
            continue
        for includePath in _kIncludeRe.findall(text):
            normalizedInclude = normalizePath(includePath)
            for bannedPattern in bannedList:
                if includeHitsBanInternal(normalizedInclude, bannedPattern):
                    fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (금지: {bannedPattern})')

    del strict  # 티어 위반은 항상 실패다 — 옛 --strict 는 기본 동작이 되었다.

    if startsWithPathComponent(relativeFilePath, kDirSourceEngine) is False:
        return fileViolations, fileStrictWarns, None
    if relativeFilePath in _kWiringFiles:
        return fileViolations, fileStrictWarns, None

    enginePrefixLen = len(kDirSourceEngine) + 1
    engineRelativePath = relativeFilePath[enginePrefixLen:]
    sourceLayer = engineRelativePath.split("/", 1)[0] if "/" in engineRelativePath else _kRootLayerName
    sourceTier = engineTierOfInternal(sourceLayer)
    if sourceTier is None:
        fileViolations.append(f"{relativeFilePath}: Engine 최상위 폴더 '{sourceLayer}' 가 티어 표(_kEngineTier)에 없습니다.")
        return fileViolations, fileStrictWarns, None

    for includePath in _kIncludeRe.findall(text):
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        if normalizedInclude in _kUbiquitousHeaders:
            continue
        destRelative = normalizedInclude[len("Engine/") :]
        destLayer = destRelative.split("/", 1)[0] if "/" in destRelative else _kRootLayerName
        if destLayer == sourceLayer:
            continue
        destTier = engineTierOfInternal(destLayer)
        if destTier is None:
            fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (티어 표에 없는 폴더 \'{destLayer}\')')
            continue
        if destTier > sourceTier:
            fileViolations.append(
                f'{relativeFilePath}: #include "{includePath}"  (티어 {sourceLayer}(T{sourceTier}) -> {destLayer}(T{destTier}))'
            )

    return fileViolations, fileStrictWarns, None


def main() -> int:
    useUtf8Stdout()

    parser = argparse.ArgumentParser(description="Engine 레이어 금지 include 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    parser.add_argument("--strict", action="store_true", help="(옛 옵션) 티어 위반은 이제 항상 실패한다")
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
