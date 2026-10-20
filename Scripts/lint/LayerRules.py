"""
C++ 폴더 층 규칙의 공용 자리 — 게이트(`gate/CheckEngineLayers.py` · `gate/CheckCoreLayers.py`)와 그래프 보고서(`report/RunEngineLayerGraph.py` ·
`report/RunCoreLayerGraph.py`)가 같은 표와 같은 레이어 판정을 쓴다.

규칙 데이터는 `rules/CheckEngineLayers.toml` · `rules/CheckCoreLayers.toml` 이다. 보고서가 게이트 모듈의 비공개 이름을 import 하면
린트 명령끼리 의존하게 되고(층 2 끼리, `docs/plans/ScriptsStructure.md`), 게이트가 이름을 바꾸면 보고서가 조용히 깨진다. 그래서 둘이 읽는 것을 여기 둔다.
"""

from __future__ import annotations

from common.RuleData import kKindInteger, kKindTextList
from lint.LintGate import LintGate

#: Engine 최상위 폴더를 담지 않는 파일(Engine.cpp 등)의 가상 레이어 이름.
kEngineRootLayerName = "<root>"
#: 티어 예외 키 접두 — `prelude:<include 경로>` 는 어느 티어에서 include 해도 되는 헤더, `wiring:<파일>` 은 모든 서브시스템을 알아야 해
#: 위 티어를 include 해도 되는 배선 파일이다(`rules/CheckEngineLayers.toml` 의 `[exemption]`).
kEnginePreludePrefix = "prelude:"
kEngineWiringPrefix = "wiring:"

#: `rules/CheckEngineLayers.toml` 에서 예외 표 말고 읽는 것.
kEngineLayerSchema: dict[str, str] = {"tier": kKindInteger, "subtier": kKindInteger, "coreFolder": kKindTextList, "coreTree": kKindTextList,
                                      "coreBannedLayer": kKindTextList}
#: `rules/CheckCoreLayers.toml` 에서 예외 표 말고 읽는 것 — 폴더 → 티어(숫자가 큰 쪽이 위).
kCoreLayerSchema: dict[str, str] = {"tier": kKindInteger}


def readEngineLayerRules() -> dict:
    """`rules/CheckEngineLayers.toml` 전체(`[exemption]` 포함). `[tier]` 가 없으면 `RuleDataError` 로 멈춘다."""
    return LintGate.readRules("CheckEngineLayers", kEngineLayerSchema, requiredKeys=("tier",))


def readCoreTiers() -> dict[str, int]:
    """`rules/CheckCoreLayers.toml` 의 `[tier]`(Core 폴더 → 티어). 없으면 `RuleDataError` 로 멈춘다."""
    return LintGate.readRules("CheckCoreLayers", kCoreLayerSchema, requiredKeys=("tier",))["tier"]


def engineLayerOf(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → 레이어 이름(최상위 폴더, 루트 파일은 `kEngineRootLayerName`)."""
    parts = engineRelativePath.split("/")
    if len(parts) == 1:
        return kEngineRootLayerName
    return parts[0]
