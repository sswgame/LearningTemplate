#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 GameFramework/Base/Foundation/Framework/GameService.h 의 game:: 만 사용).
  3) Engine 내부 티어: 아래 티어가 위 티어를 include 하지 못한다(`Scripts/lint/rules/CheckEngineLayers.toml` 의 `[tier]`). 레이어는 Engine 최상위 폴더 하나다.
  4) Source/RuntimeAPI/** 에서 Engine / App / ModuleHost / Games 경로 include 금지 — 호스트 ↔ 모듈 계약이 구현을 알면 안 된다.
     (Export/ 의 모듈 매크로가 Editor · GameFramework 로케이터를 끌어오는 것은 계약이라 막지 않는다.)
  5) 최상위 소스 폴더 사이의 방향(include 경로의 **앞부분**으로 본다): Core 는 아무것도 모르고, App 은 Engine · RuntimeAPI · ModuleHost 만(게임 · 에디터는
     C-ABI 로만), Server 는 Engine · RuntimeAPI · ModuleHost 만, ModuleHost 는 Engine · RuntimeAPI 만 알고, 에디터는 게임 · 키트 · 호스트를 모르며,
     Engine · GameFramework · 게임은 호스트(App · ModuleHost · Server)를 모른다.
  6) 게임은 다른 게임을 include 하지 않는다 — 나눠 쓸 것은 키트 · 기반으로 내린다.

티어는 **include 그래프에서 계산한 것**이다 — 손으로 고른 금지 쌍은 늘릴 기준이 없다. 전체 그래프를
Tarjan SCC 로 줄이고 위상 순서를 티어로 쓰며, 위반은 경고가 아니라 실패다.

  python Scripts/lint/gate/CheckEngineLayers.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import functools
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import (  # noqa: E402
    collectSourceFiles,
    kDirSourceApp,
    kDirSourceModuleHost,
    kDirSourceCore,
    kDirSourceEditor,
    kDirSourceEngine,
    kDirSourceGameFramework,
    kDirSourceGames,
    kDirSourceRuntimeAPI,
    kDirSourceServer,
    kFileEngineServices,
    iterIncludes,
    mapConcurrent,
    normalizePath,
    startsWithPathComponent,
)
from common.RuleData import kKindInteger  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402


@functools.lru_cache(maxsize=None)
def splitNormalizedPathInternal(pathText: str) -> tuple[str, ...]:
    """`normalizePath` 한 경로를 `/` 로 나눈 조각. include 마다 금지 패턴 수만큼 불리므로 같은 글자는 한 번만 푼다."""
    return tuple(normalizePath(pathText).split("/"))


def includeHitsBanInternal(includePath: str, bannedPattern: str) -> bool:
    normalizedParts = list(splitNormalizedPathInternal(includePath))
    bannedParts = [part for part in splitNormalizedPathInternal(bannedPattern) if part]
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
    (
        # 계약이 구현을 알면 안 된다 — 서비스 표도 RuntimeAPI 에 있다. Export/ 의 모듈 매크로는 모듈 쪽 로케이터(Editor · GameFramework)를 끌어오는 것이
        # 계약이라(그 본문은 모듈 .cpp 에서 펼쳐진다) 막지 않는다.
        kDirSourceRuntimeAPI,
        ("Engine/", kDirSourceEngine, "App/", kDirSourceApp, "ModuleHost/", kDirSourceModuleHost, "Games/", kDirSourceGames),
    ),
]

# 최상위 소스 폴더 사이의 방향 — include 경로의 **앞부분**으로 본다(`GameFramework/Kits/Feature/Online/Chat/Server/` 같은 하위 폴더 이름과 헷갈리지 않게).
#   Core 는 아무것도 모른다(생성 설정 `sw/` 만). App 은 Engine · RuntimeAPI · ModuleHost 만 — 게임 · 에디터는 C-ABI 로만 안다(CLAUDE.md "Target graph").
#   Server 도 Engine · RuntimeAPI · ModuleHost 만이고, 두 실행 파일이 같이 쓰는 ModuleHost 는 Engine · RuntimeAPI 만 안다(실행 파일을 모른다).
#   에디터는 게임 · 키트 · 호스트를 모른다(RuntimeAPI · 위임 · 이벤트로). Engine · GameFramework · 게임은 호스트(App · ModuleHost · Server)를 모른다.
_kForbiddenPrefixRules: tuple[tuple[str, tuple[str, ...]], ...] = (
    (kDirSourceCore, ("Engine/", "Editor/", "GameFramework/", "Games/", "App/", "ModuleHost/", "Server/", "RuntimeAPI/")),
    (kDirSourceApp, ("Editor/", "GameFramework/", "Games/", "Server/")),
    (kDirSourceModuleHost, ("Editor/", "GameFramework/", "Games/", "App/", "Server/")),
    (kDirSourceEditor, ("GameFramework/", "Games/", "App/", "ModuleHost/", "Server/")),
    (kDirSourceServer, ("Editor/", "GameFramework/", "Games/", "App/")),
    (kDirSourceEngine, ("App/", "ModuleHost/", "Server/")),
    (kDirSourceGameFramework, ("App/", "ModuleHost/", "Server/")),
    (kDirSourceGames, ("App/", "ModuleHost/", "Server/", "Editor/")),
)

# Engine 최상위 폴더를 담지 않는 파일(EngineLoop.cpp 등)의 가상 티어 이름.
_kRootLayerName = "<root>"

# Engine 내부 티어 — 숫자가 큰 쪽이 위다(`rules/CheckEngineLayers.toml` 의 `[tier]`, 계산은 RunEngineLayerGraph · MoveEngineFolders --sync-tier).
_kRuleSchema = {"tier": kKindInteger}
_kEngineTier: dict[str, int] = LintGate.readRules("CheckEngineLayers", _kRuleSchema, requiredKeys=("tier",))["tier"]

#: 티어 예외 — `prelude:<include 경로>` 는 어느 티어에서 include 해도 되는 헤더, `wiring:<파일>` 은 모든 서브시스템을 알아야 해
#: 위 티어를 include 해도 되는 배선 파일이다(`rules/CheckEngineLayers.toml` 의 `[exemption]`).
_kPreludePrefix = "prelude:"
_kWiringPrefix = "wiring:"


def engineTierOfInternal(folderName: str) -> int | None:
    """Engine 최상위 폴더 이름의 티어. 표에 없으면 None (새 폴더 = 검사 실패)."""
    return _kEngineTier.get(folderName)


def engineLayerOfInternal(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → 레이어 이름(최상위 폴더, 루트 파일은 `_kRootLayerName`)."""
    parts = engineRelativePath.split("/")
    if len(parts) == 1:
        return _kRootLayerName
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
    listRawInclude = [rawInclude for _, rawInclude in iterIncludes(text)]

    for rulePrefix, bannedList in _kForbiddenRules:
        if not startsWithPathComponent(relativeFilePath, rulePrefix):
            continue
        for includePath in listRawInclude:
            normalizedInclude = normalizePath(includePath)
            for bannedPattern in bannedList:
                if includeHitsBanInternal(normalizedInclude, bannedPattern):
                    fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (금지: {bannedPattern})')

    listInclude = [normalizePath(includePath) for includePath in listRawInclude]
    for rulePrefix, listBannedPrefix in _kForbiddenPrefixRules:
        if not startsWithPathComponent(relativeFilePath, rulePrefix):
            continue
        for include in listInclude:
            if include.startswith(listBannedPrefix):
                fileViolations.append(f'{relativeFilePath}: #include "{include}"  (금지: {rulePrefix} 는 {include.split("/")[0]}/ 를 모른다)')
    # 게임은 다른 게임을 모른다 — 나눠 쓸 것은 키트 · 기반으로 내린다.
    if startsWithPathComponent(relativeFilePath, kDirSourceGames):
        listPart = relativeFilePath.split("/")
        gameName = listPart[2] if len(listPart) >= 4 else ""
        for include in listInclude:
            parts = include.split("/")
            if len(parts) > 2 and parts[0] == "Games" and gameName and parts[1] != gameName:
                fileViolations.append(f'{relativeFilePath}: #include "{include}"  (게임 {gameName} -> 다른 게임 {parts[1]})')

    if startsWithPathComponent(relativeFilePath, kDirSourceEngine) is False:
        return fileViolations
    gate = CheckEngineLayersGate
    wiringKey = _kWiringPrefix + relativeFilePath
    bWiring = wiringKey in gate.mapExemption
    if bWiring:
        gate.seeExemption(wiringKey)
    tierViolations: list[str] = []

    enginePrefixLen = len(kDirSourceEngine) + 1
    engineRelativePath = relativeFilePath[enginePrefixLen:]
    sourceLayer = engineLayerOfInternal(engineRelativePath)
    sourceTier = engineTierOfInternal(sourceLayer)
    if sourceTier is None:
        fileViolations.append(f"{relativeFilePath}: Engine 최상위 폴더 '{sourceLayer}' 가 티어 표(rules/CheckEngineLayers.toml 의 [tier])에 없습니다.")
        return fileViolations

    for includePath in listRawInclude:
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        if _kPreludePrefix + normalizedInclude in gate.mapExemption:
            gate.seeExemption(_kPreludePrefix + normalizedInclude)
        destRelative = normalizedInclude[len("Engine/") :]
        destLayer = engineLayerOfInternal(destRelative)
        if destLayer == sourceLayer:
            continue
        destTier = engineTierOfInternal(destLayer)
        if destTier is None:
            fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (티어 표에 없는 폴더 \'{destLayer}\')')
            continue
        if destTier > sourceTier:
            preludeKey = _kPreludePrefix + normalizedInclude
            if preludeKey in gate.mapExemption:
                gate.useExemption(preludeKey)
                continue
            tierViolations.append(
                f'{relativeFilePath}: #include "{includePath}"  (티어 {sourceLayer}(T{sourceTier}) -> {destLayer}(T{destTier}))'
            )

    if bWiring and tierViolations:
        gate.useExemption(wiringKey)
        return fileViolations
    return fileViolations + tierViolations


class CheckEngineLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    ruleSchema = _kRuleSchema

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
            # 액터 층이 월드 관리자를 아는 방향 — 가장 되돌아오기 쉬운 엣지다.
            "name": "Object 가 Scene 을 include (아래층이 위층을)",
            "files": {
                "Source/Engine/Object/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # 디바이스 · 셰이더가 그리는 쪽을 알면 안 된다.
            "name": "Graphics(RHI)가 Renderer 를 include",
            "files": {
                "Source/Engine/Graphics/RHI/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Renderer/Frame/FrameRenderer.h"\n',
            },
        },
        {
            # 계약이 구현을 아는 방향 — 서비스 표를 Engine 에 두면 이 모양이 된다.
            "name": "RuntimeAPI 가 Engine 을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/RuntimeAPI/Service/Probe.h": '#pragma once\n\n#include "Engine/Common/EngineServices.h"\n',
            },
        },
        {
            "name": "Core 가 Engine 을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Core/Probe/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # App 은 게임 · 에디터를 C-ABI 로만 안다.
            "name": "App 이 게임을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/App/Probe.cpp": '#include "pch.h"\n\n#include "Games/Alpha/AlphaGame.h"\n',
            },
        },
        {
            "name": "게임이 다른 게임을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Games/Alpha/AlphaProbe.cpp": '#include "pch.h"\n\n#include "Games/Beta/BetaProbe.h"\n',
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        engineDir = repositoryRoot / kDirSourceEngine
        if not engineDir.is_dir():
            raise GateError(f"Engine 경로 없음: {engineDir}")

        scanRoots = [
            engineDir,
            repositoryRoot / kDirSourceGames,
            repositoryRoot / kDirSourceGameFramework,
            repositoryRoot / kDirSourceRuntimeAPI,
            repositoryRoot / kDirSourceCore,
            repositoryRoot / kDirSourceApp,
            repositoryRoot / kDirSourceModuleHost,
            repositoryRoot / kDirSourceEditor,
            repositoryRoot / kDirSourceServer,
        ]
        scanRoots = [root for root in scanRoots if root.is_dir()]
        allFiles = collectSourceFiles(scanRoots)

        violations: list[str] = []
        for fileViolations in mapConcurrent(lambda path: processFile(path, repositoryRoot), allFiles):
            violations.extend(fileViolations)
        return GateResult(listViolation=violations, summary=f"{len(allFiles)} files scanned in parallel")


main = CheckEngineLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
