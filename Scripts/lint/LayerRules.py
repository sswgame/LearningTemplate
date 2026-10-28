"""
C++ 폴더 층 규칙의 공용 자리 — 그래프 보고서(`report/RunEngineLayerGraph.py` ·
`report/RunCoreLayerGraph.py`)가 같은 표와 같은 레이어 판정을 쓴다.

규칙 데이터는 `rules/CheckEngineLayers.toml` · `rules/CheckCoreLayers.toml` 이다. 보고서가 게이트 모듈의 비공개 이름을 import 하면
린트 명령끼리 의존하게 되고(층 2 끼리, `docs/decisions/5-2_Build.md`), 게이트가 이름을 바꾸면 보고서가 조용히 깨진다. 그래서 둘이 읽는 것을 여기 둔다.
"""

from __future__ import annotations

from typing import Iterable, Mapping

from common.Constants import kDirSourceEngine
from common.Paths import normalizePath
from common.RuleData import kKindInteger, kKindReason, kKindTextList
from lint.LayerGraph import edgesOfFile
from lint.LintGate import LintGate

#: Engine 최상위 폴더를 담지 않는 파일(Engine.cpp 등)의 가상 레이어 이름.
kEngineRootLayerName = "<root>"
#: 티어 예외 키 접두 — `prelude:<include 경로>` 는 어느 티어에서 include 해도 되는 헤더, `wiring:<파일>` 은 모든 서브시스템을 알아야 해
#: 위 티어를 include 해도 되는 배선 파일이다(`rules/CheckEngineLayers.toml` 의 `[exemption]`).
kEnginePreludePrefix = "prelude:"
kEngineWiringPrefix = "wiring:"

#: `rules/CheckEngineLayers.toml` 에서 예외 표 말고 읽는 것.
#: `tierReason` 는 표(`tier`)가 그래프 계산값보다 높은 폴더 → 이유(여유를 사람이 승인한 것, `lint/LayerGraph.py`).
kEngineLayerSchema: dict[str, str] = {"tier": kKindInteger, "tierReason": kKindReason, "subtier": kKindInteger, "coreFolder": kKindTextList,
                                      "coreTree": kKindTextList, "coreBannedLayer": kKindTextList}
#: `rules/CheckCoreLayers.toml` 에서 예외 표 말고 읽는 것 — 폴더 → 티어(숫자가 큰 쪽이 위).
kCoreLayerSchema: dict[str, str] = {"tier": kKindInteger, "tierReason": kKindReason}


def readEngineLayerRules() -> dict:
    """`rules/CheckEngineLayers.toml` 전체(`[exemption]` 포함). `[tier]` 가 없으면 `RuleDataError` 로 멈춘다."""
    return LintGate.readRules("CheckEngineLayers", kEngineLayerSchema, requiredKeys=("tier",))


def readCoreLayerRules() -> dict:
    """`rules/CheckCoreLayers.toml` 전체(`[tier]` · `[tierReason]` · `[exemption]`). `[tier]` 가 없으면 `RuleDataError` 로 멈춘다."""
    return LintGate.readRules("CheckCoreLayers", kCoreLayerSchema, requiredKeys=("tier",))


def readCoreTiers() -> dict[str, int]:
    """`rules/CheckCoreLayers.toml` 의 `[tier]`(Core 폴더 → 티어). 없으면 `RuleDataError` 로 멈춘다."""
    return readCoreLayerRules()["tier"]


def engineLayerOf(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → 레이어 이름(최상위 폴더, 루트 파일은 `kEngineRootLayerName`)."""
    parts = engineRelativePath.split("/")
    if len(parts) == 1:
        return kEngineRootLayerName
    return parts[0]


def engineFileEdges(relativeFilePath: str, listRawInclude: Iterable[str], mapExemption: Mapping[str, str]) -> list[tuple[str, str]]:
    """
    `Source/Engine/` 파일 하나가 만드는 폴더 간 엣지 — (원본 폴더, 대상 폴더). 게이트가 빼는 파일(배선)과 헤더(prelude)는 여기서도 빼서,
    게이트와 보고서가 같은 그래프에서 티어를 계산한다.
    """
    if kEngineWiringPrefix + relativeFilePath in mapExemption:
        return []
    sourceLayer = engineLayerOf(relativeFilePath[len(kDirSourceEngine) + 1:])
    listDest: list[str] = []
    for rawInclude in listRawInclude:
        normalizedInclude = normalizePath(rawInclude)
        if normalizedInclude.startswith("Engine/") is False or kEnginePreludePrefix + normalizedInclude in mapExemption:
            continue
        listDest.append(engineLayerOf(normalizedInclude[len("Engine/"):]))
    return edgesOfFile(sourceLayer, listDest)
