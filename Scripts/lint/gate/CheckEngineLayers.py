#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 GameFramework/Base/Foundation/Framework/GameService.h 의 game:: 만 사용).
  3) Engine 내부 티어: 아래 티어가 위 티어를 include 하지 못한다(`Scripts/lint/rules/CheckEngineLayers.toml` 의 `[tier]`). 레이어는 Engine 최상위 폴더 하나다.
     하위 티어(`[subtier]`)가 있는 최상위 폴더는 그 안의 `<폴더>/<하위 폴더>` 사이도 같은 규칙으로 본다(지금은 Graphics — RHI ↔ Shader 같은 고리).
  4) Source/RuntimeAPI/** 에서 Engine / App / ModuleHost / Games 경로 include 금지 — 호스트 ↔ 모듈 계약이 구현을 알면 안 된다.
     Editor / GameFramework 도 금지이고, Export/ 의 모듈 매크로가 로케이터를 끌어오는 계약 예외만 `[exemption]` 의 `contract:<파일>|<include>` 줄로 넘긴다.
  5) 최상위 소스 폴더 사이의 방향(include 경로의 **앞부분**으로 본다): Core 는 아무것도 모르고, App 은 Engine · RuntimeAPI · ModuleHost 만(게임 · 에디터는
     C-ABI 로만), Server 는 Engine · RuntimeAPI · ModuleHost 만, ModuleHost 는 Engine · RuntimeAPI 만 알고, 에디터는 게임 · 키트 · 호스트를 모르며,
     Engine · GameFramework · 게임은 호스트(App · ModuleHost · Server)를 모른다.
  6) 게임은 다른 게임을 include 하지 않는다 — 나눠 쓸 것은 키트 · 기반으로 내린다.
  3-1) 티어는 같은 티어끼리도 서로 include 하지 못한다(`>=`) — 같은 티어 역방향이 낡은 표에서 통과하지 않게 한다.
     게다가 전체를 훑을 때(`--files` 없이) include 그래프에서 티어를 직접 계산해 표와 맞춘다(`lint/LayerGraph.py`): 순환(SCC)은 실패,
     표 < 계산값은 실패(표가 낡았다), 표 > 계산값은 `[tierReason]` 에 이유가 있는 폴더만 허용, 계산값과 같아진 이유 줄은 낡은 줄이라 실패.
  7) 컴포넌트 모델 코어(`[coreTree]` — Object/GameObject 아래 전부, `[coreFolder]` — Prefab · Component **바로 아래** 파일)는 기능 폴더(`[coreBannedLayer]` — Graphics ·
     Physics · Navigation · Audio · Animation · TileMap, 그리고 기능 컴포넌트 폴더 Object/Component/2D · 3D · Audio · Navigation · Physics 와
     Object/SceneAnimation)를 include 하지 않는다. 티어로는 Object 가 그 폴더들 위이거나 같은 Object 안이라 티어 검사가 못 잡는 방향이다.
     금지 항목은 폴더 접두로 본다(그 폴더와 하위 전부). 예외 키는 `core:<파일>|<include 경로>` 다.
  8) 접두 없는 include 가 include 경로 `Source/Engine` 으로 풀리면 실패다(`Scene/SceneManager.h` 는 `Engine/Scene/SceneManager.h` 로 쓴다) — 위 검사들이
     `Engine/` 접두만 보므로 이 모양은 모두를 비껴간다. 공유 PCH 진입 헤더(`pch.h`)처럼 이름으로 쓰는 계약은 `[exemption]` 의 `unprefixed:<include 경로>` 줄로만 넘긴다.
     구조로 막는 길(Engine 의 PUBLIC include 경로에서 `Source/Engine` 을 빼고 `pch.h` 만 따로 내보내기)은 CMake 를 바꿔야 해서 아직 이 검사가 막는다.

티어는 **include 그래프에서 계산한 것**이다 — 손으로 고른 금지 쌍은 늘릴 기준이 없다. 전체 그래프를
Tarjan SCC 로 줄이고 위상 순서를 티어로 쓰며, 위반은 경고가 아니라 실패다.

  python Scripts/lint/gate/CheckEngineLayers.py [--root <repo>] [--files a.h b.cpp]
"""

from __future__ import annotations

import argparse
import functools
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import iterIncludes  # noqa: E402
from common.Constants import (  # noqa: E402
    kCppAllExtensions,
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
)
from common.Paths import normalizePath, startsWithPathComponent  # noqa: E402
from lint.LayerGraph import findTierTableProblems, newEdgeMap  # noqa: E402
from lint.LayerRules import engineFileEdges, engineLayerOf, kEngineLayerSchema, kEnginePreludePrefix, kEngineRootLayerName, kEngineWiringPrefix, readEngineLayerRules  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings  # noqa: E402

#: 층 규칙이 보는 뿌리 — 걷는 뿌리이자 커밋 훅이 이 게이트를 부르는 자리다.
_kListScanRoot = (kDirSourceEngine, kDirSourceGames, kDirSourceGameFramework, kDirSourceRuntimeAPI, kDirSourceCore, kDirSourceApp,
                  kDirSourceModuleHost, kDirSourceEditor, kDirSourceServer)


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


# 엔진 루프(호스트가 엔진을 세우는 자리)와 부트스트랩 · 헤드리스 단계는 호스트(App · Server · 테스트 하네스)만 안다. 모듈(게임 · 키트 · 에디터)은
# 호스트가 넘긴 서비스 범위에서 타입으로 찾는다(`game::findSubsystem<T>()` · `editor::findSubsystem<T>()`) — 엔진을 세우는 쪽의 헤더를 보면 그 경계가 무너진다.
_kEngineHostOnly: tuple[str, ...] = ("Engine/Engine.h", "Engine/HeadlessSubsystem.h", "Engine/Bootstrap/")

#: RuntimeAPI 가 include 하면 안 되는 모듈 쪽 경로 앞부분(계약 예외 키 `contract:<파일>|<include 경로>`로만 넘긴다).
_kRuntimeApiContractBanned: tuple[str, ...] = ("Editor/", "GameFramework/")
_kContractPrefix = "contract:"

#: 접두 없는 include 예외 키 접두 — `unprefixed:<include 경로>`. `Source/Engine` 이 PUBLIC include 경로라 접두 없이 풀리는 헤더를 줄마다 이유를 적어 넘긴다.
_kUnprefixedPrefix = "unprefixed:"
#: 접두가 이미 있는 include 의 첫 조각 — `Source/` 로 풀리므로 파일 시스템을 묻지 않고 넘긴다.
_kSetSourceRootName = frozenset(root.split("/")[1] for root in _kListScanRoot)

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
        (kFileEngineServices, "EngineServices.h", *_kEngineHostOnly),
    ),
    (
        kDirSourceGameFramework,
        (kFileEngineServices, "EngineServices.h", *_kEngineHostOnly),
    ),
    (
        kDirSourceEditor,
        _kEngineHostOnly,
    ),
    (
        # 계약이 구현을 알면 안 된다. Export/ 의 모듈 매크로가 모듈 쪽 서비스 창구(Editor · GameFramework)를 끌어오는 것은
        # 계약이지만(그 본문은 모듈 .cpp 에서 펼쳐진다) 위 `_kRuntimeApiContractBanned` 와 `contract:` 예외 줄로만 허용한다.
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

# Engine 내부 티어 — 숫자가 큰 쪽이 위다(`rules/CheckEngineLayers.toml` 의 `[tier]`, 계산 · 고쳐 쓰기는 RunEngineLayerGraph --apply).
_kRuleData = readEngineLayerRules()
_kEngineTier: dict[str, int] = _kRuleData["tier"]
#: 표가 그래프 계산값보다 높은 폴더 → 이유(`[tierReason]`). 이유 없는 상향은 `finishSourceScan` 의 "표 = 계산값" 검사가 실패시킨다.
_kEngineTierReason: dict[str, str] = _kRuleData["tierReason"]
#: 하위 티어 — `<최상위 폴더>/<하위 폴더>` → 숫자(`[subtier]`). 키가 있는 최상위 폴더만 하위 폴더 사이를 본다.
_kEngineSubTier: dict[str, int] = _kRuleData["subtier"]
_kSubTieredLayer: frozenset[str] = frozenset(key.split("/")[0] for key in _kEngineSubTier)
#: 컴포넌트 모델 코어 폴더(Engine/ 기준) — 그 **바로 아래** 파일만 코어다(하위 폴더는 기능 컴포넌트).
_kCoreFolder: frozenset[str] = frozenset(_kRuleData["coreFolder"])
#: 하위 폴더까지 모두 코어인 폴더(Engine/ 기준) — 오브젝트 모델 본체를 하위 폴더로 나눠도 코어 규칙이 따라간다.
_kCoreTree: tuple[str, ...] = tuple(_kRuleData["coreTree"])
#: 코어가 include 하지 않는 폴더(Engine/ 기준). 최상위 폴더(`Graphics`)도, 그 아래 폴더(`Object/Component/3D`)도 된다 — 그 폴더와 하위 전부를 막는다.
_kCoreBannedLayer: tuple[str, ...] = tuple(_kRuleData["coreBannedLayer"])
#: 코어 예외 키 접두 — `core:<저장소 기준 파일>|<include 경로>`.
_kCorePrefix = "core:"

#: `subprelude:<include 경로>` 는 같은 최상위 폴더 안의 어느 하위 티어에서 include 해도 되는 헤더다(최상위 티어 검사에는 쓰이지 않는다).
_kSubPreludePrefix = "subprelude:"


def engineTierOfInternal(folderName: str) -> int | None:
    """Engine 최상위 폴더 이름의 티어. 표에 없으면 None (새 폴더 = 검사 실패)."""
    return _kEngineTier.get(folderName)


def engineSubLayerOfInternal(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → `<최상위 폴더>/<하위 폴더>`(최상위 폴더 바로 아래 파일은 `<최상위 폴더>/<root>`)."""
    parts = engineRelativePath.split("/")
    if len(parts) <= 2:
        return f"{parts[0]}/{kEngineRootLayerName}"
    return f"{parts[0]}/{parts[1]}"


def findSubTierViolationsInternal(relativeFilePath: str, engineRelativePath: str, sourceLayer: str, listRawInclude: list[str]) -> list[str]:
    """하위 티어가 있는 최상위 폴더 안에서 아래 하위 폴더가 위 하위 폴더를 include 하는 줄."""
    if sourceLayer not in _kSubTieredLayer:
        return []
    gate = CheckEngineLayersGate
    sourceSubLayer = engineSubLayerOfInternal(engineRelativePath)
    sourceSubTier = _kEngineSubTier.get(sourceSubLayer)
    if sourceSubTier is None:
        return [f"{relativeFilePath}: 하위 폴더 '{sourceSubLayer}' 가 하위 티어 표(rules/CheckEngineLayers.toml 의 [subtier])에 없습니다."]
    listViolation: list[str] = []
    for includePath in listRawInclude:
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        destRelative = normalizedInclude[len("Engine/") :]
        if engineLayerOf(destRelative) != sourceLayer:
            continue
        subPreludeKey = _kSubPreludePrefix + normalizedInclude
        if subPreludeKey in gate.mapExemption:
            gate.seeExemption(subPreludeKey)
        destSubLayer = engineSubLayerOfInternal(destRelative)
        if destSubLayer == sourceSubLayer:
            continue
        destSubTier = _kEngineSubTier.get(destSubLayer)
        if destSubTier is None:
            listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (하위 티어 표에 없는 폴더 \'{destSubLayer}\')')
            continue
        if destSubTier >= sourceSubTier:
            if subPreludeKey in gate.mapExemption:
                gate.useExemption(subPreludeKey)
                continue
            listViolation.append(
                f'{relativeFilePath}: #include "{includePath}"  (하위 티어 {sourceSubLayer}(T{sourceSubTier}) -> {destSubLayer}(T{destSubTier}))'
            )
    return listViolation


def findCoreViolationsInternal(relativeFilePath: str, engineRelativePath: str, listRawInclude: list[str]) -> list[str]:
    """컴포넌트 모델 코어 파일이 기능 폴더를 include 하는 줄(예외 표에 든 줄은 넘긴다)."""
    folderPath, _, _ = engineRelativePath.rpartition("/")
    bCoreTree = any(folderPath == tree or folderPath.startswith(tree + "/") for tree in _kCoreTree)
    if folderPath not in _kCoreFolder and bCoreTree is False:
        return []
    gate = CheckEngineLayersGate
    listViolation: list[str] = []
    for includePath in listRawInclude:
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        destPath = normalizedInclude[len("Engine/") :]
        bannedFolder = next((banned for banned in _kCoreBannedLayer if destPath.startswith(banned + "/")), None)
        if bannedFolder is None:
            continue
        coreKey = f"{_kCorePrefix}{relativeFilePath}|{normalizedInclude}"
        if coreKey in gate.mapExemption:
            gate.useExemption(coreKey)
            continue
        listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (컴포넌트 모델 코어 {folderPath} -> 기능 폴더 {bannedFolder})')
    return listViolation


def findUnprefixedEngineIncludesInternal(source: SourceFile) -> list[str]:
    """
    접두 없는 include 가 include 경로 `Source/Engine` 으로 풀리는 줄.

    `Source/Engine` 은 Engine 의 PUBLIC include 경로라(`Source/Engine/CMakeLists.txt`) `#include "Scene/SceneManager.h"` 가 어느 타깃에서나 풀린다.
    위 검사들은 `Engine/` 접두만 보므로 이 모양은 층 · 티어 검사를 모두 비껴간다. 컴파일러처럼 그 파일의 폴더 → `Source/` → `Source/Engine/` 순으로 풀어
    마지막에서만 풀리면 위반이다. 확장자 없는 이름(표준 헤더)과 접두가 있는 이름은 파일 시스템을 묻지 않는다.
    """
    repositoryRoot = source.path
    for _ in source.relative.split("/"):
        repositoryRoot = repositoryRoot.parent
    listViolation: list[str] = []
    for _, includePath in iterIncludes(source.text):
        normalizedInclude = normalizePath(includePath)
        bSkipped = "." not in normalizedInclude or normalizedInclude.split("/")[0] in _kSetSourceRootName
        if bSkipped:
            continue
        bResolvedElsewhere = (source.path.parent / includePath).is_file() or (repositoryRoot / "Source" / normalizedInclude).is_file()
        if bResolvedElsewhere or (repositoryRoot / kDirSourceEngine / normalizedInclude).is_file() is False:
            continue
        exemptionKey = _kUnprefixedPrefix + normalizedInclude
        if exemptionKey in CheckEngineLayersGate.mapExemption:
            CheckEngineLayersGate.useExemption(exemptionKey)
            continue
        listViolation.append(
            f'{source.relative}: #include "{includePath}"  (접두 없는 include 가 include 경로 {kDirSourceEngine} 로 풀린다 — "Engine/{normalizedInclude}" 로 쓴다)'
        )
    return listViolation


def processFile(relativeFilePath: str, text: str) -> list[str]:
    """파일 하나의 금지 include 와 티어 위반을 돌려줍니다."""
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

    if startsWithPathComponent(relativeFilePath, kDirSourceRuntimeAPI):
        # 계약이 모듈 쪽 서비스 창구(Editor · GameFramework)를 끌어오는 것은 줄마다 이유를 적은 예외로만 허용한다 — 주석 한 줄이 아니라 표가 근거다.
        for include in listInclude:
            if include.startswith(_kRuntimeApiContractBanned) is False:
                continue
            contractKey = f"{_kContractPrefix}{relativeFilePath}|{include}"
            if contractKey in CheckEngineLayersGate.mapExemption:
                CheckEngineLayersGate.useExemption(contractKey)
                continue
            fileViolations.append(f'{relativeFilePath}: #include "{include}"  (금지: RuntimeAPI 는 {include.split("/")[0]}/ 를 모른다 — 계약 예외는 [exemption] 의 {_kContractPrefix}<파일>|<include> 줄)')

    if startsWithPathComponent(relativeFilePath, kDirSourceEngine) is False:
        return fileViolations
    gate = CheckEngineLayersGate
    wiringKey = kEngineWiringPrefix + relativeFilePath
    bWiring = wiringKey in gate.mapExemption
    if bWiring:
        gate.seeExemption(wiringKey)
    tierViolations: list[str] = []

    enginePrefixLen = len(kDirSourceEngine) + 1
    engineRelativePath = relativeFilePath[enginePrefixLen:]
    # 코어 규칙은 배선 예외로 넘기지 않는다 — 티어 위반과 따로 센다.
    fileViolations += findCoreViolationsInternal(relativeFilePath, engineRelativePath, listRawInclude)
    sourceLayer = engineLayerOf(engineRelativePath)
    sourceTier = engineTierOfInternal(sourceLayer)
    if sourceTier is None:
        fileViolations.append(f"{relativeFilePath}: Engine 최상위 폴더 '{sourceLayer}' 가 티어 표(rules/CheckEngineLayers.toml 의 [tier])에 없습니다.")
        return fileViolations

    for includePath in listRawInclude:
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        if kEnginePreludePrefix + normalizedInclude in gate.mapExemption:
            gate.seeExemption(kEnginePreludePrefix + normalizedInclude)
        destRelative = normalizedInclude[len("Engine/") :]
        destLayer = engineLayerOf(destRelative)
        if destLayer == sourceLayer:
            continue
        destTier = engineTierOfInternal(destLayer)
        if destTier is None:
            fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (티어 표에 없는 폴더 \'{destLayer}\')')
            continue
        if destTier >= sourceTier:
            preludeKey = kEnginePreludePrefix + normalizedInclude
            if preludeKey in gate.mapExemption:
                gate.useExemption(preludeKey)
                continue
            tierViolations.append(
                f'{relativeFilePath}: #include "{includePath}"  (티어 {sourceLayer}(T{sourceTier}) -> {destLayer}(T{destTier}))'
            )

    tierViolations += findSubTierViolationsInternal(relativeFilePath, engineRelativePath, sourceLayer, listRawInclude)

    if bWiring and tierViolations:
        gate.useExemption(wiringKey)
        return fileViolations
    return fileViolations + tierViolations


class CheckEngineLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    ruleSchema = kEngineLayerSchema

    description = "Engine 레이어 금지 include 검사"
    buildComment = "Checking Engine layer include rules..."
    timeoutSeconds = 15
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = tuple(sorted(kCppAllExtensions | {".xxx"}))   # `.xxx` 목록 파일도 include 를 든다 — 보고서 · 티어 계산과 같은 파일을 본다
    # 옛 걷기(`collectSourceFiles`)처럼 폴더를 빼지 않는다 — 층 규칙은 뿌리 아래 모든 C++ 를 본다.
    sourceExcludedDirNames = frozenset()
    violationHeader = "레이어 위반"
    selfTestCases = [
        {
            "name": "Engine 이 Editor 를 include",
            "expectedViolation": "Source/Engine/Scene/Probe.cpp: #include \"Editor/Common/Workspace/EditorContext.h\"  (금지: Editor/)",
            "files": {
                "Source/Engine/Scene/Probe.cpp": '#include "pch.h"\n\n#include "Editor/Common/Workspace/EditorContext.h"\n',
            },
        },
        {
            # 액터 층이 월드 관리자를 아는 방향 — 가장 되돌아오기 쉬운 엣지다.
            "name": "Object 가 Scene 을 include (아래층이 위층을)",
            "expectedViolation": "Source/Engine/Object/Probe.cpp: #include \"Engine/Scene/SceneManager.h\"",
            "files": {
                "Source/Engine/Object/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # 디바이스 · 셰이더가 그리는 쪽을 알면 안 된다.
            "name": "Graphics(RHI)가 Renderer 를 include",
            "expectedViolation": "Source/Engine/Graphics/RHI/Probe.cpp: #include \"Engine/Renderer/Frame/FrameRenderer.h\"",
            "files": {
                "Source/Engine/Graphics/RHI/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Renderer/Frame/FrameRenderer.h"\n',
            },
        },
        {
            # 같은 Graphics 안의 고리 — RHI 백엔드가 셰이더 캐시를 쓰므로 셰이더가 RHI 를 알면 순환이다(최상위 티어로는 안 보인다).
            "name": "Graphics/Shader 가 Graphics/RHI 를 include (하위 티어)",
            "expectedViolation": "Source/Engine/Graphics/Shader/Compile/Probe.cpp: #include \"Engine/Graphics/RHI/RHI.h\"  (하위 티어 Graphics/Shader(T0) -> Graphics/RHI(T1))",
            "files": {
                "Source/Engine/Graphics/Shader/Compile/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Graphics/RHI/RHI.h"\n',
            },
        },
        {
            # 하위 폴더를 새로 만들면 표에 자리를 정해야 한다.
            "name": "하위 티어 표에 없는 Graphics 하위 폴더",
            "expectedViolation": "Source/Engine/Graphics/ProbeFolder/Probe.cpp: 하위 폴더 'Graphics/ProbeFolder' 가 하위 티어 표(rules/CheckEngineLayers.toml 의 [subtier])에 없습니다.",
            "files": {
                "Source/Engine/Graphics/ProbeFolder/Probe.cpp": '#include "pch.h"\n',
            },
        },
        {
            # 컴포넌트 모델 코어가 기능을 아는 방향 — Object 는 Graphics 보다 티어가 위라 티어 검사로는 안 잡힌다.
            "name": "Object/GameObject 가 Graphics 를 include (코어 -> 기능)",
            "expectedViolation": "Source/Engine/Object/GameObject/Probe.cpp: #include \"Engine/Graphics/Mesh/Mesh.h\"  (컴포넌트 모델 코어 Object/GameObject -> 기능 폴더 Graphics)",
            "files": {
                "Source/Engine/Object/GameObject/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Graphics/Mesh/Mesh.h"\n',
            },
        },
        {
            "name": "Object/GameObject 하위 폴더가 Physics 를 include (코어 트리 -> 기능)",
            "expectedViolation": "Source/Engine/Object/GameObject/Tick/Probe.cpp: #include \"Engine/Physics/PhysicsTypes.h\"  (컴포넌트 모델 코어 Object/GameObject/Tick -> 기능 폴더 Physic",
            "files": {
                "Source/Engine/Object/GameObject/Tick/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Physics/PhysicsTypes.h"\n',
            },
        },
        {
            "name": "Object/Component 바로 아래가 Physics 를 include (코어 -> 기능)",
            "expectedViolation": "Source/Engine/Object/Component/Probe.h: #include \"Engine/Physics/PhysicsTypes.h\"  (컴포넌트 모델 코어 Object/Component -> 기능 폴더 Physics)",
            "files": {
                "Source/Engine/Object/Component/Probe.h": '#pragma once\n\n#include "Engine/Physics/PhysicsTypes.h"\n',
            },
        },
        {
            # 계약이 구현을 아는 방향 — 서비스 표를 Engine 에 두면 이 모양이 된다.
            "name": "RuntimeAPI 가 Engine 을 include",
            "expectedViolation": "Source/RuntimeAPI/Service/Probe.h: #include \"Engine/Common/EngineServices.h\"  (금지: Engine/)",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/RuntimeAPI/Service/Probe.h": '#pragma once\n\n#include "Engine/Common/EngineServices.h"\n',
            },
        },
        {
            "name": "Core 가 Engine 을 include",
            "expectedViolation": "Source/Core/Probe/Probe.cpp: #include \"Engine/Scene/SceneManager.h\"  (금지: Source/Core 는 Engine/ 를 모른다)",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Core/Probe/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # App 은 게임 · 에디터를 C-ABI 로만 안다.
            "name": "App 이 게임을 include",
            "expectedViolation": "Source/App/Probe.cpp: #include \"Games/Alpha/AlphaGame.h\"  (금지: Source/App 는 Games/ 를 모른다)",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/App/Probe.cpp": '#include "pch.h"\n\n#include "Games/Alpha/AlphaGame.h"\n',
            },
        },
        {
            # 같은 티어 역방향 — 표가 낡으면 `>` 검사가 통과시키던 모양이다.
            "name": "같은 티어의 다른 폴더를 include (Object ↔ Window 가 둘 다 9)",
            "expectedViolation": "Source/Engine/Object/Probe.cpp: #include \"Engine/Window/Window.h\"  (티어 Object(T9) -> Window(T9))",
            "files": {
                "Source/Engine/Object/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Window/Window.h"\n',
            },
        },
        {
            "name": "RuntimeAPI 가 계약 예외 표에 없는 Editor 헤더를 include",
            "expectedViolation": "Source/RuntimeAPI/Export/Probe.h: #include \"Editor/Common/Probe.h\"  (금지: RuntimeAPI 는 Editor/ 를 모른다",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/RuntimeAPI/Export/Probe.h": '#pragma once\n\n#include "Editor/Common/Probe.h"\n',
            },
        },
        {
            # 표 = 계산값 — 아래 셋은 작은 트리라 계산값이 작지만, 표와 계산이 어긋나는 모양마다 따로 실패해야 한다.
            "name": "표 < 계산값 (Reflection 이 Serialization 을 거쳐 Common 위로 올라가는데 표는 1)",
            "expectedViolation": "티어 표: Reflection: rules/CheckEngineLayers.toml [tier] 의 티어 1 가 그래프 계산값 2 보다 낮다",
            "files": {
                "Source/Engine/Reflection/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Serialization/Probe.h"\n',
                "Source/Engine/Serialization/Probe.h": '#pragma once\n\n#include "Engine/Common/Probe.h"\n',
            },
        },
        {
            "name": "표 > 계산값인데 [tierReason] 에 이유가 없다 (사유 없는 상향)",
            "expectedViolation": "티어 표: Scene: rules/CheckEngineLayers.toml [tier] 의 티어 11 가 계산값 1 보다 높은데 [tierReason] 에 이유가 없다",
            "files": {
                "Source/Engine/Scene/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Common/Probe.h"\n',
            },
        },
        {
            "name": "폴더 순환(SCC)",
            "expectedViolation": "티어 표: 강결합 묶음(순환): Object · Window",
            "files": {
                "Source/Engine/Object/Probe.h": '#pragma once\n\n#include "Engine/Window/Probe.h"\n',
                "Source/Engine/Window/Probe.h": '#pragma once\n\n#include "Engine/Object/Probe.h"\n',
            },
        },
        {
            # `Source/Engine` 이 PUBLIC include 경로라 접두 없이도 풀린다 — 층 · 티어 검사가 `Engine/` 접두만 봐서 못 잡던 모양이다.
            "name": "접두 없는 include 가 Source/Engine 으로 풀린다",
            "expectedViolation": "Source/Games/Alpha/AlphaProbe.cpp: #include \"Common/Probe.h\"  (접두 없는 include 가 include 경로 Source/Engine 로 풀린다",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Games/Alpha/AlphaProbe.cpp": '#include "pch.h"\n\n#include "Common/Probe.h"\n',
            },
        },
        {
            "name": "게임이 다른 게임을 include",
            "expectedViolation": "Source/Games/Alpha/AlphaProbe.cpp: #include \"Games/Beta/BetaProbe.h\"  (게임 Alpha -> 다른 게임 Beta)",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Games/Alpha/AlphaProbe.cpp": '#include "pch.h"\n\n#include "Games/Beta/BetaProbe.h"\n',
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def prepareSourceScan(self, repositoryRoot: Path, args: argparse.Namespace) -> None:
        engineDir = repositoryRoot / kDirSourceEngine
        if not engineDir.is_dir():
            raise GateError(f"Engine 경로 없음: {engineDir}")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        if source.decodeError:
            return FileFindings(error=source.decodeError)
        listEdge: list[tuple[str, str]] = []
        if startsWithPathComponent(source.relative, kDirSourceEngine):
            listEdge = engineFileEdges(source.relative, [rawInclude for _, rawInclude in iterIncludes(source.text)], self.mapExemption)
        listViolation = processFile(source.relative, source.text) + findUnprefixedEngineIncludesInternal(source)
        return FileFindings(listViolation=listViolation, value=listEdge)

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        violations, _ = joinFindings(result.listFindings)
        summary = f"{result.fileCount} files scanned in parallel"
        if args.files is not None:
            # 일부 파일만 보면 그래프가 일부라 티어를 계산할 수 없다 — 표 = 계산값 검사는 전체 훑기(린트 묶음 · CI)가 한다.
            return GateResult(listViolation=violations, summary=summary + " (--files: 표 = 계산값 검사는 전체 훑기에서)")
        mapEdge = newEdgeMap()
        for findings in result.listFindings:
            for sourceLayer, destLayer in findings.value or ():
                mapEdge[sourceLayer][destLayer].append(None)
        violations += [f"티어 표: {problem}" for problem in findTierTableProblems(
            mapEdge, _kEngineTier, _kEngineTierReason, tableLabel="rules/CheckEngineLayers.toml [tier]", reasonLabel="[tierReason]")]
        return GateResult(listViolation=violations, summary=f"{summary}, {len(mapEdge)} 폴더의 티어를 그래프 계산값과 맞춰 봄")


main = CheckEngineLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
