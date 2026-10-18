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
     (Export/ 의 모듈 매크로가 Editor · GameFramework 로케이터를 끌어오는 것은 계약이라 막지 않는다.)
  5) 최상위 소스 폴더 사이의 방향(include 경로의 **앞부분**으로 본다): Core 는 아무것도 모르고, App 은 Engine · RuntimeAPI · ModuleHost 만(게임 · 에디터는
     C-ABI 로만), Server 는 Engine · RuntimeAPI · ModuleHost 만, ModuleHost 는 Engine · RuntimeAPI 만 알고, 에디터는 게임 · 키트 · 호스트를 모르며,
     Engine · GameFramework · 게임은 호스트(App · ModuleHost · Server)를 모른다.
  6) 게임은 다른 게임을 include 하지 않는다 — 나눠 쓸 것은 키트 · 기반으로 내린다.
  7) 컴포넌트 모델 코어(`[coreTree]` — Object/GameObject 아래 전부, `[coreFolder]` — Prefab · Component **바로 아래** 파일)는 기능 폴더(`[coreBannedLayer]` — Graphics ·
     Physics · Navigation · Audio · Animation · TileMap)를 include 하지 않는다. 티어로는 Object 가 그 폴더들 위라 티어 검사가 못 잡는 방향이다.
     기능 컴포넌트(Component/2D · 3D · Audio · Navigation · Physics)는 코어가 아니다. 예외 키는 `core:<파일>|<include 경로>` 다.

티어는 **include 그래프에서 계산한 것**이다 — 손으로 고른 금지 쌍은 늘릴 기준이 없다. 전체 그래프를
Tarjan SCC 로 줄이고 위상 순서를 티어로 쓰며, 위반은 경고가 아니라 실패다.

  python Scripts/lint/gate/CheckEngineLayers.py [--root <repo>] [--files a.h b.cpp]
"""

from __future__ import annotations

import argparse
import functools
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import (  # noqa: E402
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
    iterIncludes,
    normalizePath,
    startsWithPathComponent,
)
from common.RuleData import kKindInteger, kKindTextList  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402
from SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings  # noqa: E402

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
        # 계약이 구현을 알면 안 된다. Export/ 의 모듈 매크로는 모듈 쪽 서비스 창구(Editor · GameFramework)를 끌어오는 것이
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

# Engine 최상위 폴더를 담지 않는 파일(Engine.cpp 등)의 가상 티어 이름.
_kRootLayerName = "<root>"

# Engine 내부 티어 — 숫자가 큰 쪽이 위다(`rules/CheckEngineLayers.toml` 의 `[tier]`, 계산 · 고쳐 쓰기는 RunEngineLayerGraph --apply).
_kRuleSchema = {"tier": kKindInteger, "subtier": kKindInteger, "coreFolder": kKindTextList, "coreTree": kKindTextList, "coreBannedLayer": kKindTextList}
_kRuleData = LintGate.readRules("CheckEngineLayers", _kRuleSchema, requiredKeys=("tier",))
_kEngineTier: dict[str, int] = _kRuleData["tier"]
#: 하위 티어 — `<최상위 폴더>/<하위 폴더>` → 숫자(`[subtier]`). 키가 있는 최상위 폴더만 하위 폴더 사이를 본다.
_kEngineSubTier: dict[str, int] = _kRuleData["subtier"]
_kSubTieredLayer: frozenset[str] = frozenset(key.split("/")[0] for key in _kEngineSubTier)
#: 컴포넌트 모델 코어 폴더(Engine/ 기준) — 그 **바로 아래** 파일만 코어다(하위 폴더는 기능 컴포넌트).
_kCoreFolder: frozenset[str] = frozenset(_kRuleData["coreFolder"])
#: 하위 폴더까지 모두 코어인 폴더(Engine/ 기준) — 오브젝트 모델 본체를 하위 폴더로 나눠도 코어 규칙이 따라간다.
_kCoreTree: tuple[str, ...] = tuple(_kRuleData["coreTree"])
#: 코어가 include 하지 않는 Engine 최상위 폴더.
_kCoreBannedLayer: frozenset[str] = frozenset(_kRuleData["coreBannedLayer"])
#: 코어 예외 키 접두 — `core:<저장소 기준 파일>|<include 경로>`.
_kCorePrefix = "core:"

#: 티어 예외 — `prelude:<include 경로>` 는 어느 티어에서 include 해도 되는 헤더, `wiring:<파일>` 은 모든 서브시스템을 알아야 해
#: 위 티어를 include 해도 되는 배선 파일이다(`rules/CheckEngineLayers.toml` 의 `[exemption]`).
_kPreludePrefix = "prelude:"
_kWiringPrefix = "wiring:"
#: `subprelude:<include 경로>` 는 같은 최상위 폴더 안의 어느 하위 티어에서 include 해도 되는 헤더다(최상위 티어 검사에는 쓰이지 않는다).
_kSubPreludePrefix = "subprelude:"


def engineTierOfInternal(folderName: str) -> int | None:
    """Engine 최상위 폴더 이름의 티어. 표에 없으면 None (새 폴더 = 검사 실패)."""
    return _kEngineTier.get(folderName)


def engineLayerOfInternal(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → 레이어 이름(최상위 폴더, 루트 파일은 `_kRootLayerName`)."""
    parts = engineRelativePath.split("/")
    if len(parts) == 1:
        return _kRootLayerName
    return parts[0]


def engineSubLayerOfInternal(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → `<최상위 폴더>/<하위 폴더>`(최상위 폴더 바로 아래 파일은 `<최상위 폴더>/<root>`)."""
    parts = engineRelativePath.split("/")
    if len(parts) <= 2:
        return f"{parts[0]}/{_kRootLayerName}"
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
        if engineLayerOfInternal(destRelative) != sourceLayer:
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
        if destSubTier > sourceSubTier:
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
        destLayer = engineLayerOfInternal(normalizedInclude[len("Engine/") :])
        if destLayer not in _kCoreBannedLayer:
            continue
        coreKey = f"{_kCorePrefix}{relativeFilePath}|{normalizedInclude}"
        if coreKey in gate.mapExemption:
            gate.useExemption(coreKey)
            continue
        listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (컴포넌트 모델 코어 {folderPath} -> 기능 폴더 {destLayer})')
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
    # 코어 규칙은 배선 예외로 넘기지 않는다 — 티어 위반과 따로 센다.
    fileViolations += findCoreViolationsInternal(relativeFilePath, engineRelativePath, listRawInclude)
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

    tierViolations += findSubTierViolationsInternal(relativeFilePath, engineRelativePath, sourceLayer, listRawInclude)

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
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = tuple(sorted(kCppAllExtensions))
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
        return FileFindings(listViolation=processFile(source.relative, source.text))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        violations, _ = joinFindings(result.listFindings)
        return GateResult(listViolation=violations, summary=f"{result.fileCount} files scanned in parallel")


main = CheckEngineLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
