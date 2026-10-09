#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GameFramework 층 검사 — 기반의 층 · 폴더 순서(DAG)와 키트의 의존 방향.

강제 규칙:
  0) `Source/GameFramework/` 최상위 폴더는 `Base`(기반) · `Kits`(키트) 둘뿐이다 — 그 밖의 폴더는 실패다.
  1) 기반은 `Base/<층>/<폴더>/` 두 단이다. 층(_kBaseLayer)은 **자기보다 아래 층**만 include 한다. 같은 층 안의 폴더는 **자기보다 낮은 순서**
     (_kBaseFolderOrder)의 폴더만 include 한다 — 같은 순서끼리도 서로 모른다. 그래서 두 표가 곧 순환이 없다는 증거다.
     표에 없는 층 · 폴더는 실패다(새 폴더는 층과 순서를 정하고 넣는다). `Online` 층은 폴더 하나처럼 본다(안쪽은 자유).
  2) 기반은 키트(`GameFramework/Kits/`)를 include 하지 않는다 — 키트끼리 나눠 쓰는 것은 기반으로 내린다.
  3) 키트는 `Kits/<성격>/<묶음>/<키트>/` 다(성격은 `Genre` · `Feature`). 키트는 다른 키트를 include 하지 않는다. 묶음이 함께 쓰는 헤더
     (`Kits/<성격>/<묶음>/x.h` — 키트 폴더 밖)만 된다. 클라이언트 · 서버로 나뉘는 기능은 `Kits/<성격>/<묶음>/<기능>/` 아래 `Shared/`(공유 키트
     `GF_<기능>`) · `Server/`(`GF_Server_<기능>`) · `Client/`(`GF_Client_<기능>`)로 짝을 두고, 셋이 각각 키트 하나다. 예외는 하나 — 서버 · 클라이언트
     키트는 같은 기능의 `Shared/` 를 include 해도 된다(서버 → 공유 ← 클라이언트). 서버 키트끼리 · 다른 기능의 키트는 여전히 안 된다.
     옛 꼴 `Kits/<성격>/<묶음>/Server/<키트>/` 는 실패다.
  4) GameFramework 의 어느 파일도 `Games/` · `Editor/` 를 include 하지 않는다.

키트 → 기반은 어느 층이든 된다(키트는 기반 위의 층이다). 기반을 DLL 여럿으로 나누지는 않는다 — 층은 폴더로만 지킨다
(docs/09_Decisions.md 2절 "안 하기로 한 것").

  python Scripts/lint/gate/CheckGameFrameworkLayers.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectSourceFiles, kDirSourceGameFramework  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

_kKitsFolderName = "Kits"
_kBaseFolderName = "Base"

# ------------------------------------------------------------------------------
# 기반의 층 — 숫자가 큰 쪽이 위다. 층은 **자기보다 작은 층**만 include 한다(include 그래프를 재서 정했다, 2026-10-10).
#   0  Online     — 온라인 기반 계약(저장 · 캐시 · 보호 · 신원 · 감사 · 버스 · 예약 · 원격 설정). Core 만 본다. 세이브가 로컬 저장소를 쓴다.
#   1  Foundation — 계산 도구 · 데이터 틀 · 게임 모듈의 수명 · 배선 · 서비스 창구 · 세이브.
#   2  World      — 월드 상태(시계 · 날씨 · 중력 · 땅 · 플래그 · 질의) · 곡선. 액터와 게임플레이가 그 위에 선다.
#   3  Actor      — 액터 하나가 무엇이고 어떻게 움직이나: 입력 · 조종 · 이동 · 길 찾기 · AI · 카메라 · 전투 수치(체력 신호).
#   4  UI         — HUD · 체력 바 · 대화 · 데미지 숫자. 액터의 체력 신호와 월드 플래그를 읽는다.
#   5  Gameplay   — 그 위의 규칙: 어빌리티 · 인벤토리 · 상호작용 · 성장 · 퀘스트 · 경기 · 기믹 · 외형 · 탈것 · 공유 게임 상태.
# 언리얼에서 UMG 가 게임플레이 모듈(GameplayAbilities 등) 아래에 있고 게임 코드가 위젯을 만드는 것과 같은 방향이다 — 어빌리티가 데미지 숫자를 띄운다.
# 모델은 뷰를 모른다는 규칙은 Actor 층까지만 선다: HP 바는 `Combat/HealthListenerComponent` 로 체력 신호를 받는다(UI 가 Actor 위).
# ------------------------------------------------------------------------------
_kBaseLayer: dict[str, int] = {
    "Online": 0,
    "Foundation": 1,
    "World": 2,
    "Actor": 3,
    "UI": 4,
    "Gameplay": 5,
}

# 폴더 하나처럼 보는 층(안쪽 하위 폴더끼리는 자유).
_kSingleFolderLayers: frozenset[str] = frozenset({"Online"})

# ------------------------------------------------------------------------------
# 층 안 폴더의 순서 — 큰 쪽이 위다. 같은 층의 **자기보다 작은 순서**만 include 한다(같은 순서끼리도 금지).
#   Foundation: 계산 도구 < 데이터 틀 < 게임 모듈 배선(Framework — 데이터를 읽는다).
#   World     : 환경 · 질의(플래그) · 수명 · 곡선 < 땅(지역 그래프의 잠금이 플래그 조건식을 읽는다).
#   Actor     : 잎 시스템 < AI(길 찾기 위) · 조종(빙의 — 카메라 뷰 타깃 · 1인칭 시점 입력 위).
#   Gameplay  : 잎 시스템 < 퀘스트(인벤토리 위) · 외형(인벤토리 위) · 기믹(상호작용 위) < 공유 게임 상태(인벤토리 · 성장 · 퀘스트 위) ·
#               탈것(좌석 · 타기 — 외형 위). 기믹 센서는 상호작용의 완료 수를 끌어 읽는다(Interaction 은 Gimmick 을 모른다).
# ------------------------------------------------------------------------------
_kBaseFolderOrder: dict[str, int] = {
    "Foundation/Utility": 0,
    "Foundation/Data": 1,
    "Foundation/Framework": 2,
    "World/Environment": 0,
    "World/Query": 0,
    "World/Lifetime": 0,
    "World/Spline": 0,
    "World/Land": 1,
    "Actor/Input": 0,
    "Actor/Movement": 0,
    "Actor/Navigation": 0,
    "Actor/Combat": 0,
    "Actor/Camera": 0,
    "Actor/AI": 1,
    "Actor/Control": 1,
    "UI/Hud": 0,
    "UI/Marker": 0,
    "UI/Dialogue": 0,
    "Gameplay/Inventory": 0,
    "Gameplay/Progression": 0,
    "Gameplay/Match": 0,
    "Gameplay/Ability": 0,
    "Gameplay/Interaction": 0,
    "Gameplay/Quest": 1,
    "Gameplay/Appearance": 1,
    "Gameplay/Gimmick": 1,
    "Gameplay/GameState": 2,
    "Gameplay/Vehicle": 2,
}

# 클라이언트 · 서버로 나뉘는 기능 폴더 안의 짝 이름(공유 `GF_<기능>` · 서버 전용 `GF_Server_<기능>` · 클라이언트 전용 `GF_Client_<기능>`).
_kSharedFolderName = "Shared"
_kSideFolderNames: tuple[str, ...] = (_kSharedFolderName, "Server", "Client")
# `Kits/` 바로 아래 성격 폴더 — 장르 키트(`Genre`)와 기능 키트(`Feature` — 네트워크 · 온라인 · 저장 · 월드 라이브러리).
_kKitKinds: tuple[str, ...] = ("Genre", "Feature")
# GameFramework 아래 어디서 include 해도 금지인 경로 앞부분.
_kForbiddenPrefixes: tuple[str, ...] = ("Games/", "Editor/")


def classifyInternal(relativePath: str) -> tuple[str, str]:
    """
    GameFramework 아래 상대 경로 → (종류, 이름). 종류는 "base"(기반 폴더) · "kit"(키트 하나) · "kitGroup"(묶음 공용 헤더) · "root"(루트 파일) ·
    "stray"(최상위의 `Base` · `Kits` 아닌 폴더). 기반 이름은 `<층>/<폴더>`(폴더 하나처럼 보는 층은 `<층>`, 층 바로 아래 파일은 `<층>/`),
    키트 이름은 `<묶음>/<키트>`(짝으로 나뉜 기능은 `<묶음>/<기능>/<Shared|Server|Client>`)다. "sideMisplaced" 는 옛 꼴 `<묶음>/Server/<키트>/` 다.
    """
    parts = relativePath.split("/")
    if len(parts) == 1:
        return "root", ""
    if parts[0] == _kBaseFolderName:
        if len(parts) == 2:
            return "root", ""
        if parts[1] in _kSingleFolderLayers:
            return "base", parts[1]
        return "base", f"{parts[1]}/{parts[2]}" if len(parts) >= 4 else f"{parts[1]}/"
    if parts[0] != _kKitsFolderName:
        return "stray", parts[0]
    if len(parts) <= 3:
        return "root", ""
    # 키트는 성격(`Genre` 장르 키트 · `Feature` 기능 키트) 아래 그룹 아래에 있다 — `Kits/<성격>/<그룹>/<키트>/…`.
    if parts[1] not in _kKitKinds:
        return "strayKit", parts[1]
    kitParts = parts[2:]
    # 짝으로 나뉜 기능은 한 단 더 깊다 — `<그룹>/<기능>/Server/…` 가 키트 `<그룹>/<기능>/Server` 하나다.
    if len(kitParts) >= 4 and kitParts[2] in _kSideFolderNames:
        return "kit", f"{kitParts[0]}/{kitParts[1]}/{kitParts[2]}"
    if len(kitParts) >= 3 and kitParts[1] in _kSideFolderNames:
        return "sideMisplaced", f"{kitParts[0]}/{kitParts[1]}"
    if len(kitParts) >= 3:
        return "kit", f"{kitParts[0]}/{kitParts[1]}"
    return "kitGroup", kitParts[0]


def isSameFeatureSharedKitInternal(sourceKit: str, destKit: str) -> bool:
    """서버 · 클라이언트 키트(`<묶음>/<기능>/Server`)가 같은 기능의 공유 키트(`<묶음>/<기능>/Shared`)를 보는가."""
    sourceParts = sourceKit.split("/")
    if len(sourceParts) != 3 or sourceParts[2] == _kSharedFolderName:
        return False
    return destKit == f"{sourceParts[0]}/{sourceParts[1]}/{_kSharedFolderName}"


def isKnownBaseInternal(baseName: str) -> bool:
    """층 표 · 폴더 순서 표에 있는 기반 이름인가."""
    layer = baseName.split("/")[0]
    if layer not in _kBaseLayer:
        return False
    return layer in _kSingleFolderLayers or baseName in _kBaseFolderOrder


def describeBaseEdgeInternal(sourceName: str, destName: str) -> str:
    """기반 → 기반 include 하나가 규칙을 어기면 그 까닭, 지키면 빈 글자입니다."""
    sourceLayer = sourceName.split("/")[0]
    destLayer = destName.split("/")[0]
    if sourceLayer != destLayer:
        if _kBaseLayer[destLayer] >= _kBaseLayer[sourceLayer]:
            return f"층 {sourceLayer}(L{_kBaseLayer[sourceLayer]}) -> {destLayer}(L{_kBaseLayer[destLayer]}) — 아래 층만 볼 수 있다"
        return ""
    sourceOrder = _kBaseFolderOrder[sourceName]
    destOrder = _kBaseFolderOrder[destName]
    if destOrder >= sourceOrder:
        return f"같은 층 {sourceName}({sourceOrder}) -> {destName}({destOrder}) — 층 안에서는 낮은 순서의 폴더만 볼 수 있다"
    return ""


def checkFileInternal(relativeFilePath: str, text: str) -> list[str]:
    """파일 하나(저장소 기준 경로)의 위반 줄들입니다."""
    prefix = kDirSourceGameFramework + "/"
    gameFrameworkRelative = relativeFilePath[len(prefix):]
    sourceKind, sourceName = classifyInternal(gameFrameworkRelative)
    listViolation: list[str] = []
    if sourceKind == "sideMisplaced":
        listViolation.append(f"{relativeFilePath}: 서버 · 클라이언트 키트는 Kits/<성격>/<묶음>/<기능>/Server|Client/ 에 둔다(공유 키트는 같은 기능의 Shared/)")
        return listViolation
    if sourceKind == "strayKit":
        listViolation.append(f"{relativeFilePath}: 키트 성격 폴더 '{sourceName}' — 키트는 Kits/Genre/<그룹>/ 또는 Kits/Feature/<그룹>/ 아래에 둔다")
        return listViolation
    if sourceKind == "stray":
        listViolation.append(f"{relativeFilePath}: GameFramework 최상위 폴더 '{sourceName}' — 기반은 Base/ 아래, 키트는 Kits/ 아래에 둔다")
        return listViolation
    if sourceKind == "base" and isKnownBaseInternal(sourceName) is False:
        listViolation.append(f"{relativeFilePath}: 기반 '{sourceName}' 가 층 표(_kBaseLayer) · 폴더 순서 표(_kBaseFolderOrder)에 없습니다"
                             " — 기반 파일은 Base/<층>/<폴더>/ 에 둔다")
        return listViolation
    for includePath in _kIncludeRe.findall(text):
        include = includePath.replace("\\", "/")
        if include.startswith(_kForbiddenPrefixes):
            listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (GameFramework 는 Games · Editor 를 모른다)')
            continue
        if include.startswith("GameFramework/") is False:
            continue
        destKind, destName = classifyInternal(include[len("GameFramework/"):])
        if destKind in ("root", "stray", "strayKit", "sideMisplaced"):
            continue
        if sourceKind == "base":
            if destKind in ("kit", "kitGroup"):
                listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (기반 -> 키트)')
                continue
            if destName == sourceName:
                continue
            if isKnownBaseInternal(destName) is False:
                listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (표에 없는 기반 \'{destName}\')')
                continue
            reason = describeBaseEdgeInternal(sourceName, destName)
            if reason != "":
                listViolation.append(f'{relativeFilePath}: #include "{includePath}"  ({reason})')
            continue
        if sourceKind == "kit" and destKind == "kit" and destName != sourceName and isSameFeatureSharedKitInternal(sourceName, destName) is False:
            listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (키트 {sourceName} -> 다른 키트 {destName})')
            continue
        if sourceKind == "kit" and destKind == "kitGroup" and sourceName.split("/")[0] != destName:
            listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (키트 {sourceName} -> 다른 묶음 {destName} 의 공용 헤더)')
    return listViolation


class CheckGameFrameworkLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "GameFramework 기반 폴더 층 · 키트 의존 방향 검사"
    buildComment = "Checking GameFramework layer include rules..."
    timeoutSeconds = 15
    preCommitPattern = ("Source/GameFramework/*",)
    violationHeader = "층 위반"
    hint = "기반 폴더의 층은 Scripts/lint/gate/CheckGameFrameworkLayers.py 의 _kBaseLayer · _kBaseFolderOrder — 위층이 쓰는 것을 아래로 내리거나 신호(이벤트 · 끌어 읽기)로 뒤집는다"
    selfTestCases = [
        {
            # 순환을 다시 만드는 가장 쉬운 엣지 — 상호작용이 기믹을 직접 부른다.
            "name": "Interaction 이 Gimmick 을 include (아래층이 위층을)",
            "files": {
                "Source/GameFramework/Base/Gameplay/Interaction/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"\n',
            },
        },
        {
            # 모델이 뷰를 아는 방향 — 체력 시스템이 HP 바를 민다.
            "name": "Combat 이 UI 를 include",
            "files": {
                "Source/GameFramework/Base/Actor/Combat/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/UI/Marker/HealthBarComponent.h"\n',
            },
        },
        {
            "name": "아래 층(Foundation)이 위 층(World)을 include",
            "files": {
                "Source/GameFramework/Base/Foundation/Framework/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/World/Spline/SplineComponent.h"\n',
            },
        },
        {
            "name": "같은 층 · 같은 순서의 폴더끼리 include",
            "files": {
                "Source/GameFramework/Base/Actor/Input/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Actor/Movement/ArcadeVehicleMotor.h"\n',
            },
        },
        {
            "name": "층 표에 없는 층",
            "files": {
                "Source/GameFramework/Base/Stage/Probe/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"\n',
            },
        },
        {
            "name": "폴더 순서 표에 없는 기반 폴더",
            "files": {
                "Source/GameFramework/Base/Actor/Stage/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"\n',
            },
        },
        {
            # 층으로 묶기 전 자리(Base/<폴더>/)에 파일을 다시 만든다.
            "name": "층 바로 아래 파일",
            "files": {
                "Source/GameFramework/Base/Actor/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"\n',
            },
        },
        {
            # 기반 폴더를 Base/ 밖(최상위)에 다시 만든다.
            "name": "최상위에 Base · Kits 아닌 폴더",
            "files": {
                "Source/GameFramework/Stage/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"\n',
            },
        },
        {
            "name": "기반이 키트를 include",
            "files": {
                "Source/GameFramework/Base/Actor/Combat/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/UnitStatsComponent.h"\n',
            },
        },
        {
            # 성격 폴더(Genre · Feature) 없이 그룹을 Kits/ 바로 아래에 다시 만든다.
            "name": "키트가 성격 폴더 밖에 있다",
            "files": {
                "Source/GameFramework/Kits/Action/Probe/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"\n',
            },
        },
        {
            "name": "키트가 다른 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Genre/Rpg/ClassicJrpg/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Genre/Rpg/MonsterCollector/MonsterBattle.h"\n',
            },
        },
        {
            "name": "서버 키트가 다른 서버 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Feature/Online/Trade/Server/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Feature/Online/Account/Server/AccountService.h"\n',
            },
        },
        {
            "name": "서버 키트가 다른 기능의 공유 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Feature/Online/Trade/Server/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Feature/Online/Account/Shared/AccountMessages.h"\n',
            },
        },
        {
            "name": "키트가 게임을 include",
            "files": {
                "Source/GameFramework/Kits/Genre/Action/ActionCombat/Probe.cpp": '#include "pch.h"\n\n#include "Games/Shooter3D/ShooterEnemyComponent.h"\n',
            },
        },
        {
            # 방향이 거꾸로 — 공유 키트(클라이언트에도 들어간다)가 같은 기능의 서버 키트를 본다.
            "name": "공유 키트가 같은 기능의 서버 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Feature/Online/Trade/Shared/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"\n',
            },
        },
        {
            "name": "서버 키트를 옛 꼴(<묶음>/Server/<키트>/)에 둔다",
            "files": {
                "Source/GameFramework/Kits/Feature/Online/Server/Probe/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"\n',
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        gameFrameworkDir = repositoryRoot / kDirSourceGameFramework
        if not gameFrameworkDir.is_dir():
            raise GateError(f"GameFramework 경로 없음: {gameFrameworkDir}")
        listPath = collectSourceFiles([gameFrameworkDir])
        listViolation: list[str] = []
        for path, text in LintGate.readFiles(listPath, errors="strict"):
            listViolation.extend(checkFileInternal(path.relative_to(repositoryRoot).as_posix(), text))
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} files, {len(_kBaseLayer)} base layers, {len(_kBaseFolderOrder)} base folders")


main = CheckGameFrameworkLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
