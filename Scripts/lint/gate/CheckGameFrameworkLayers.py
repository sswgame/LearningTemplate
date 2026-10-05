#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GameFramework 층 검사 — 기반 폴더의 층(DAG)과 키트의 의존 방향.

강제 규칙:
  0) `Source/GameFramework/` 최상위 폴더는 `Base`(기반) · `Kits`(키트) 둘뿐이다 — 그 밖의 폴더는 실패다.
  1) 기반 폴더(`Source/GameFramework/Base/<폴더>/`)는 **자기보다 낮은 층**의 기반 폴더만 include 한다(_kBaseTier).
     같은 층끼리도 서로 모른다 — 그래서 표가 곧 순환이 없다는 증거다. 표에 없는 폴더는 실패다(새 폴더는 층을 정하고 넣는다).
  2) 기반은 키트(`GameFramework/Kits/`)를 include 하지 않는다 — 키트끼리 나눠 쓰는 것은 기반으로 내린다.
  3) 키트(`Kits/<묶음>/<키트>/`)는 다른 키트를 include 하지 않는다. 묶음이 함께 쓰는 헤더(`Kits/<묶음>/x.h` — 키트 폴더 밖)만 된다.
     서버 · 클라이언트 전용 키트(`Kits/<묶음>/Server/<키트>/` · `Kits/<묶음>/Client/<키트>/` — 모듈 `GF_Server_<키트>` · `GF_Client_<키트>`)도
     키트 하나다. 예외는 하나 — 같은 기능의 공유 키트(`Kits/<묶음>/<키트>/`)는 include 해도 된다(서버 → 공유 ← 클라이언트).
     서버 키트끼리 · 다른 기능의 키트는 여전히 안 된다.
  4) GameFramework 의 어느 파일도 `Games/` · `Editor/` 를 include 하지 않는다.

키트 → 기반은 어느 층이든 된다(키트는 기반 위의 층이다). 기반을 DLL 여럿으로 나누지는 않는다 — 층은 폴더로만 지킨다
(docs/06_Backlog.md 2절 "안 하기로 한 것").

  python Scripts/lint/gate/CheckGameFrameworkLayers.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectSourceFiles, kDirSourceGameFramework, readTextFiles  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

_kKitsFolderName = "Kits"
_kBaseFolderName = "Base"

# ------------------------------------------------------------------------------
# 기반 폴더의 층 — 숫자가 큰 쪽이 위다. 폴더는 **자기보다 작은 층**만 include 한다(같은 층끼리도 금지).
#
# 층은 include 그래프에서 읽었다(언리얼 GameplayAbilities · AIModule 이 Engine GameFramework 위에 서는 모양):
#   0  계산 도구 — 무엇도 모른다.
#   1  데이터 틀 · 의존 없는 잎 시스템(판 규칙 · 격자 길 찾기 · 곡선).
#   2  게임 모듈의 수명 · 배선 · 서비스 창구(Framework) — 데이터를 읽는다.
#   3  장르 공통 시스템 — 전투 수치 · 입력 · 인벤토리 · 성장 · 이동 · 월드(씬 컴포넌트 포함).
#   4  그 위의 시스템 — AI(길 찾기 · 월드 위) · 외형(인벤토리 위) · 카메라(1인칭 시점 입력 위) · 상호작용(입력 · 월드 위) · 퀘스트(인벤토리 위) ·
#      UI(체력 신호 위).
#   5  가장 위 — 어빌리티(UI 의 데미지 숫자를 띄운다) · 기믹(상호작용 · 곡선 위).
# 모델은 뷰를 모른다: HP 바는 `Combat/HealthListenerComponent` 로 체력 신호를 받는다(UI 가 Combat 위). 기믹 센서는 상호작용의 완료 수를
# 끌어 읽는다(Interaction 은 Gimmick 을 모른다).
# ------------------------------------------------------------------------------
_kBaseTier: dict[str, int] = {
    "Utility": 0,
    "Data": 1,
    "Match": 1,
    "Navigation": 1,
    "Spline": 1,
    "Framework": 2,
    "Combat": 3,
    "Input": 3,
    "Inventory": 3,
    "Movement": 3,
    "Progression": 3,
    "World": 3,
    "AI": 4,
    "Appearance": 4,
    "Camera": 4,
    "Interaction": 4,
    "Quest": 4,
    "UI": 4,
    "Ability": 5,
    "Gimmick": 5,
    "GameState": 5,
}

# 키트 묶음 안에서 서버 · 클라이언트 전용 키트를 담는 폴더 이름(모듈 `GF_Server_<키트>` · `GF_Client_<키트>`).
_kSideFolderNames: tuple[str, ...] = ("Server", "Client")
# GameFramework 아래 어디서 include 해도 금지인 경로 앞부분.
_kForbiddenPrefixes: tuple[str, ...] = ("Games/", "Editor/")


def classifyInternal(relativePath: str) -> tuple[str, str]:
    """
    GameFramework 아래 상대 경로 → (종류, 이름). 종류는 "base"(기반 폴더) · "kit"(키트 하나) · "kitGroup"(묶음 공용 헤더) · "root"(루트 파일) ·
    "stray"(최상위의 `Base` · `Kits` 아닌 폴더). 키트 이름은 `<묶음>/<키트>` 다.
    """
    parts = relativePath.split("/")
    if len(parts) == 1:
        return "root", ""
    if parts[0] == _kBaseFolderName:
        if len(parts) == 2:
            return "root", ""
        return "base", parts[1]
    if parts[0] != _kKitsFolderName:
        return "stray", parts[0]
    # 서버 · 클라이언트 전용 키트는 한 단 더 깊다 — `Kits/<묶음>/Server/<키트>/…` 가 키트 `<묶음>/Server/<키트>` 하나다.
    if len(parts) >= 5 and parts[2] in _kSideFolderNames:
        return "kit", f"{parts[1]}/{parts[2]}/{parts[3]}"
    if len(parts) == 4 and parts[2] in _kSideFolderNames:
        return "kitGroup", parts[1]
    if len(parts) >= 4:
        return "kit", f"{parts[1]}/{parts[2]}"
    if len(parts) == 3:
        return "kitGroup", parts[1]
    return "root", ""


def isSameFeatureSharedKitInternal(sourceKit: str, destKit: str) -> bool:
    """서버 · 클라이언트 전용 키트(`<묶음>/Server/<키트>`)가 같은 기능의 공유 키트(`<묶음>/<키트>`)를 보는가."""
    sourceParts = sourceKit.split("/")
    return len(sourceParts) == 3 and sourceParts[1] in _kSideFolderNames and destKit == f"{sourceParts[0]}/{sourceParts[2]}"


def checkFileInternal(relativeFilePath: str, text: str) -> list[str]:
    """파일 하나(저장소 기준 경로)의 위반 줄들입니다."""
    prefix = kDirSourceGameFramework + "/"
    gameFrameworkRelative = relativeFilePath[len(prefix):]
    sourceKind, sourceName = classifyInternal(gameFrameworkRelative)
    listViolation: list[str] = []
    if sourceKind == "stray":
        listViolation.append(f"{relativeFilePath}: GameFramework 최상위 폴더 '{sourceName}' — 기반은 Base/ 아래, 키트는 Kits/ 아래에 둔다")
        return listViolation
    if sourceKind == "base" and sourceName not in _kBaseTier:
        listViolation.append(f"{relativeFilePath}: 기반 폴더 '{sourceName}' 가 층 표(_kBaseTier)에 없습니다")
        return listViolation
    for includePath in _kIncludeRe.findall(text):
        include = includePath.replace("\\", "/")
        if include.startswith(_kForbiddenPrefixes):
            listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (GameFramework 는 Games · Editor 를 모른다)')
            continue
        if include.startswith("GameFramework/") is False:
            continue
        destKind, destName = classifyInternal(include[len("GameFramework/"):])
        if destKind in ("root", "stray"):
            continue
        if sourceKind == "base":
            if destKind in ("kit", "kitGroup"):
                listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (기반 -> 키트)')
                continue
            if destName == sourceName:
                continue
            destTier = _kBaseTier.get(destName)
            if destTier is None:
                listViolation.append(f'{relativeFilePath}: #include "{includePath}"  (층 표에 없는 기반 폴더 \'{destName}\')')
                continue
            sourceTier = _kBaseTier[sourceName]
            if destTier >= sourceTier:
                listViolation.append(
                    f'{relativeFilePath}: #include "{includePath}"  (층 {sourceName}(L{sourceTier}) -> {destName}(L{destTier}) — 아래 층만 볼 수 있다)')
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
    hint = "기반 폴더의 층은 Scripts/lint/gate/CheckGameFrameworkLayers.py 의 _kBaseTier — 위층이 쓰는 것을 아래로 내리거나 신호(이벤트 · 끌어 읽기)로 뒤집는다"
    selfTestCases = [
        {
            # 순환을 다시 만드는 가장 쉬운 엣지 — 상호작용이 기믹을 직접 부른다.
            "name": "Interaction 이 Gimmick 을 include (아래층이 위층을)",
            "files": {
                "Source/GameFramework/Base/Interaction/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Gimmick/GimmickSensorComponent.h"\n',
            },
        },
        {
            # 모델이 뷰를 아는 방향 — 체력 시스템이 HP 바를 민다.
            "name": "Combat 이 UI 를 include",
            "files": {
                "Source/GameFramework/Base/Combat/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/UI/HealthBarComponent.h"\n',
            },
        },
        {
            "name": "같은 층끼리 include",
            "files": {
                "Source/GameFramework/Base/World/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Combat/Vitality.h"\n',
            },
        },
        {
            "name": "층 표에 없는 기반 폴더",
            "files": {
                "Source/GameFramework/Base/Stage/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Utility/GameRandom.h"\n',
            },
        },
        {
            # 기반 폴더를 Base/ 밖(최상위)에 다시 만든다.
            "name": "최상위에 Base · Kits 아닌 폴더",
            "files": {
                "Source/GameFramework/Stage/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Base/Utility/GameRandom.h"\n',
            },
        },
        {
            "name": "기반이 키트를 include",
            "files": {
                "Source/GameFramework/Base/Combat/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Action/ActionCombat/UnitStatsComponent.h"\n',
            },
        },
        {
            "name": "키트가 다른 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Rpg/ClassicJrpg/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Rpg/MonsterCollector/MonsterBattle.h"\n',
            },
        },
        {
            "name": "서버 키트가 다른 서버 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Online/Server/Trade/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Online/Server/Account/AccountService.h"\n',
            },
        },
        {
            "name": "서버 키트가 다른 기능의 공유 키트를 include",
            "files": {
                "Source/GameFramework/Kits/Online/Server/Trade/Probe.cpp": '#include "pch.h"\n\n#include "GameFramework/Kits/Online/Account/AccountMessages.h"\n',
            },
        },
        {
            "name": "키트가 게임을 include",
            "files": {
                "Source/GameFramework/Kits/Action/ActionCombat/Probe.cpp": '#include "pch.h"\n\n#include "Games/Shooter3D/ShooterEnemyComponent.h"\n',
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        gameFrameworkDir = repositoryRoot / kDirSourceGameFramework
        if not gameFrameworkDir.is_dir():
            raise GateError(f"GameFramework 경로 없음: {gameFrameworkDir}")
        listPath = collectSourceFiles([gameFrameworkDir])
        listViolation: list[str] = []
        for path, text in readTextFiles(listPath, errors="strict"):
            listViolation.extend(checkFileInternal(path.relative_to(repositoryRoot).as_posix(), text))
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} files, {len(_kBaseTier)} base folders")


main = CheckGameFrameworkLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
