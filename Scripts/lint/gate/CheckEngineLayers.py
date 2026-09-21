#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 GameFramework/Base/GameService.h 의 game:: 만 사용).
  3) Engine 내부 티어: 아래 티어가 위 티어를 include 하지 못한다 (_kEngineTier).
     `Graphics/Renderer` 만 최상위 폴더보다 잘게 본다 — 그리는 쪽은 씬 위, 나머지 Graphics 는 컴포넌트 아래.

티어는 **include 그래프에서 계산한 것**이다. 예전에는 손으로 고른 네 쌍(Utility->Graphics 등)만
경고로 찍고 실패시키지 않았다 — 근거 없는 목록이라 늘릴 기준도 없고, 실패하지 않으니 쌓여도
아무도 몰랐다. 지금은 전체 그래프를 Tarjan SCC 로 줄이고 위상 순서를 티어로 쓴다.

  python Scripts/lint/gate/CheckEngineLayers.py [--root <repo>] [--strict]

(--strict 는 남겨 두었지만 이제 기본 동작과 같다. 티어 위반은 항상 실패다.)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import (  # noqa: E402
    collectSourceFiles,
    kDirSourceEngine,
    kDirSourceGameFramework,
    kDirSourceGames,
    kFileEngineServices,
    mapConcurrent,
    normalizePath,
    startsWithPathComponent,
)
from LintGate import GateError, GateResult, LintGate  # noqa: E402
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

# 최상위 폴더보다 잘게 보는 유일한 자리 — Graphics 안의 "그리는 쪽". 아래 표의 설명 참고.
_kGraphicsRendererLayerName = "Graphics/Renderer"

# ------------------------------------------------------------------------------
# Engine 내부 티어 — 숫자가 큰 쪽이 위다. 같은 티어끼리는 서로 참조해도 된다.
#
# 이 표는 include 그래프를 위상 정렬해서 얻었다(`Scripts/lint/report/RunEngineLayerGraph.py` 가 같은
# 규칙으로 다시 계산한다). 손으로 고른 순서가 아니므로, 코드가 바뀌면 표도 다시 계산해야 한다.
#
# **강결합 묶음은 없다 (2026-09-21).** 그래프가 DAG 라 모든 폴더에 참인 순서가 있다. 예전에는 티어 4
# 가 일곱 폴더(Graphics·Module·Object·Resource·Scene·Sequencer·Window)의 묶음이었고, 그 안에서는
# 순서를 주장하지 않았다. 묶음을 푼 엣지 다섯은 전부 "위층 것을 아래층이 들고 있던" 모양이었다 —
# Object 가 SceneManager 에게 활성 씬을 묻고, RHI 디바이스가 렌더 패스 에셋 캐시를 소유하고, RHI 가
# IWindow 전역을 읽고, SceneManager 가 FrameRenderer 를 들고, 셰이더 컴파일 폴더가 렌더러의 패스
# 지식을 include 했다. 자세한 것은 Source/Engine/README.md 와 docs/07_EngineStructureVsCommercial.md.
#
# `Graphics` 만 최상위 폴더보다 잘게 본다: `Graphics/Renderer`(FrameRenderer · RenderGraph · GpuScene ·
# RenderThread · Bake)는 씬과 컴포넌트를 **읽어서 그리는 쪽**이라 그 위(8)이고, 나머지 `Graphics`(RHI ·
# Shader · Material · Mesh · Texture · Upload)는 컴포넌트가 드는 **디바이스와 GPU 에셋**이라 그 아래(5)
# 다 — 언리얼의 RHI/RenderCore 와 Renderer 사이의 선이다. 그래서 RHI·Shader 가 Renderer 를 include 하면
# 실패한다(`engineLayerOfInternal` 참고).
# ------------------------------------------------------------------------------
_kEngineTier: dict[str, int] = {
    # 0: 토대 — Engine 의 어느 것도 참조하지 않는다.
    "Common": 0,
    # 외부 압축 라이브러리(lz4·zstd) 코덱. Core 의 ICompressionCodec 만 구현하고 Engine 것은 안 본다
    # — Core 를 압축 라이브러리에 종속시키지 않으려고 여기 둔다(Source/Engine/CMakeLists.txt 주석 참고).
    "Compression": 0,
    "Physics": 0,
    # 1: 리플렉션과, 토대 위의 잎 서브시스템·헬퍼.
    "Audio": 1,
    "Reflection": 1,
    "Spatial": 1,
    "Utility": 1,
    # 2: 리플렉션 위에 올라가는 직렬화와 에셋형 잎.
    "Animation": 2,
    "Localization": 2,
    "Serialization": 2,
    # 3: 설정 — 리플렉션·직렬화로 읽힌다.
    "Config": 3,
    "Dialogue": 3,
    # 4: 에셋 데이터베이스·팩·캐시 등록부. 위의 모두가 읽는다.
    "Resource": 4,
    # 5: 디바이스와 GPU 에셋(RHI·Shader·Material·Mesh·Texture·Upload) · 창. 창은 IRenderSurface 로만 RHI 에 보인다.
    "Graphics": 5,
    "Window": 5,
    # 6: 컴포넌트 모델 · 입력. 컴포넌트가 머티리얼·메시(5)를 든다.
    "Input": 6,
    "Object": 6,
    # 7: 월드와, 오브젝트 위에서 도는 기능 모듈. 월드는 액터를 알고 액터는 월드를 모른다.
    "Scene": 7,
    "Sequencer": 7,
    # 8: 그리는 쪽 · 핫리로드. 씬과 컴포넌트를 읽는다.
    _kGraphicsRendererLayerName: 8,
    "Module": 8,
    # 9: 전부를 엮는 자리.
    _kRootLayerName: 9,
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


def engineLayerOfInternal(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → 레이어 이름. `Graphics/Renderer/**` 만 최상위 폴더보다 잘게 본다."""
    parts = engineRelativePath.split("/")
    if len(parts) == 1:
        return _kRootLayerName
    if parts[0] == "Graphics" and len(parts) > 2 and parts[1] == "Renderer":
        return _kGraphicsRendererLayerName
    return parts[0]


def processFile(filePath: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나의 금지 include 와 티어 위반을 돌려줍니다. 읽지 못하면 `GateError` 입니다."""
    relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="strict")
    except UnicodeDecodeError as exception:
        raise GateError(f"UTF-8 인코딩 오류: {relativeFilePath}: {exception}") from exception
    except OSError as exception:
        raise GateError(f"읽기 실패: {relativeFilePath}: {exception}") from exception

    fileViolations: list[str] = []

    for rulePrefix, bannedList in _kForbiddenRules:
        if not startsWithPathComponent(relativeFilePath, rulePrefix):
            continue
        for includePath in _kIncludeRe.findall(text):
            normalizedInclude = normalizePath(includePath)
            for bannedPattern in bannedList:
                if includeHitsBanInternal(normalizedInclude, bannedPattern):
                    fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (금지: {bannedPattern})')

    if startsWithPathComponent(relativeFilePath, kDirSourceEngine) is False:
        return fileViolations
    if relativeFilePath in _kWiringFiles:
        return fileViolations

    enginePrefixLen = len(kDirSourceEngine) + 1
    engineRelativePath = relativeFilePath[enginePrefixLen:]
    sourceLayer = engineLayerOfInternal(engineRelativePath)
    sourceTier = engineTierOfInternal(sourceLayer)
    if sourceTier is None:
        fileViolations.append(f"{relativeFilePath}: Engine 최상위 폴더 '{sourceLayer}' 가 티어 표(_kEngineTier)에 없습니다.")
        return fileViolations

    for includePath in _kIncludeRe.findall(text):
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        if normalizedInclude in _kUbiquitousHeaders:
            continue
        destRelative = normalizedInclude[len("Engine/") :]
        destLayer = engineLayerOfInternal(destRelative)
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

    return fileViolations


class CheckEngineLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "Engine 레이어 금지 include 검사"
    buildComment = "Checking Engine layer include rules..."
    timeoutSeconds = 15
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    violationHeader = "레이어 위반"
    selfTestCases = [
        {
            "name": "Engine 이 Editor 를 include",
            "files": {
                "Source/Engine/Scene/Probe.cpp": '#include "pch.h"\n\n#include "Editor/Common/Workspace/EditorContext.h"\n',
            },
        },
        {
            # 액터 층이 월드 관리자를 아는 방향 — 2026-09-21 에 뗀 엣지가 되돌아오면 잡아야 한다.
            "name": "Object 가 Scene 을 include (아래층이 위층을)",
            "files": {
                "Source/Engine/Object/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # Graphics 안의 선 — 디바이스·셰이더가 그리는 쪽을 알면 안 된다.
            "name": "Graphics 의 아래(RHI)가 Graphics/Renderer 를 include",
            "files": {
                "Source/Engine/Graphics/RHI/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"\n',
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--strict", action="store_true", help="(옛 옵션) 티어 위반은 이제 항상 실패한다")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        engineDir = repositoryRoot / kDirSourceEngine
        if not engineDir.is_dir():
            raise GateError(f"Engine 경로 없음: {engineDir}")

        scanRoots = [engineDir, repositoryRoot / kDirSourceGames, repositoryRoot / kDirSourceGameFramework]
        allFiles = collectSourceFiles(scanRoots)

        violations: list[str] = []
        for fileViolations in mapConcurrent(lambda path: processFile(path, repositoryRoot), allFiles):
            violations.extend(fileViolations)
        return GateResult(listViolation=violations, summary=f"{len(allFiles)} files scanned in parallel")


main = CheckEngineLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
