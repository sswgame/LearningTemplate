"""
폴더 층 그래프의 공용 계산 — 게이트(`gate/CheckEngineLayers.py` · `gate/CheckCoreLayers.py`)와 그래프 보고서(`report/RunEngineLayerGraph.py` ·
`report/RunCoreLayerGraph.py`)가 같은 묶음(SCC) · 티어 계산과 같은 "표 = 계산값" 판정을 쓴다.

왜 필요한가: 티어 표는 `RunEngineLayerGraph.py --apply` 의 출력이기도 해서, 위반이 생겨도 표를 다시 계산하면 흡수된다. 게이트가 방향만 보면
(표가 낡을 때) 같은 티어 역방향 include 와 필요 없이 높은 티어가 조용히 남는다. 그래서 게이트가 **그래프에서 직접 계산한 티어와 표를 맞춰 본다**.
표가 계산값보다 높은 폴더(여유)는 `[tierReason]` 에 이유를 적은 것만 허용한다 — 이유 없는 상향은 실패, 계산값과 같아진 이유 줄은 낡은 줄이라 실패다.

엣지 지도 `mapEdge` 는 `원본 폴더 -> 대상 폴더 -> [그 엣지를 만든 항목]` 이다(항목의 모양은 쓰는 쪽 몫 — 여기서는 키만 본다).
"""

from __future__ import annotations

import sys
from collections import defaultdict
from typing import Any, Iterable, Mapping

EdgeMap = dict[str, dict[str, list[Any]]]


def newEdgeMap() -> EdgeMap:
    return defaultdict(lambda: defaultdict(list))


def listEdgeNodes(mapEdge: Mapping[str, Mapping[str, Any]]) -> list[str]:
    uniqueNode = set(mapEdge)
    for sourceLayer in mapEdge:
        uniqueNode.update(mapEdge[sourceLayer])
    return sorted(uniqueNode)


def findComponents(mapEdge: Mapping[str, Mapping[str, Any]]) -> list[list[str]]:
    """Tarjan SCC. 크기 2 이상인 묶음만 돌려준다."""
    mapIndex: dict[str, int] = {}
    mapLow: dict[str, int] = {}
    listStack: list[str] = []
    uniqueOnStack: set[str] = set()
    listComponent: list[list[str]] = []
    counter = [0]

    def visit(node: str) -> None:
        mapIndex[node] = mapLow[node] = counter[0]
        counter[0] += 1
        listStack.append(node)
        uniqueOnStack.add(node)
        for nextNode in mapEdge.get(node, {}):
            if nextNode not in mapIndex:
                visit(nextNode)
                mapLow[node] = min(mapLow[node], mapLow[nextNode])
            elif nextNode in uniqueOnStack:
                mapLow[node] = min(mapLow[node], mapIndex[nextNode])
        if mapLow[node] == mapIndex[node]:
            component: list[str] = []
            while True:
                popped = listStack.pop()
                uniqueOnStack.discard(popped)
                component.append(popped)
                if popped == node:
                    break
            listComponent.append(sorted(component))

    sys.setrecursionlimit(10000)
    for node in listEdgeNodes(mapEdge):
        if node not in mapIndex:
            visit(node)
    return [component for component in listComponent if len(component) > 1]


def computeTiers(mapEdge: Mapping[str, Mapping[str, Any]]) -> dict[str, int]:
    """Kahn — 의존 대상이 전부 놓인 뒤에 놓는다(티어 0 = 토대). 묶음 안의 노드와 그 위는 빠진다."""
    listNode = listEdgeNodes(mapEdge)
    mapDependency = {node: set(mapEdge.get(node, {})) for node in listNode}
    mapTier: dict[str, int] = {}
    uniqueRemaining = set(listNode)
    level = 0
    while uniqueRemaining:
        listReady = sorted(node for node in uniqueRemaining if all(dep in mapTier for dep in mapDependency[node]))
        if not listReady:
            break
        for node in listReady:
            mapTier[node] = level
        uniqueRemaining -= set(listReady)
        level += 1
    return mapTier


def computeTiersFillingComponents(mapEdge: Mapping[str, Mapping[str, Any]]) -> dict[str, int]:
    """`computeTiers` 에 묶음까지 놓는다 — 바깥 의존이 다 놓인 묶음은 한 덩어리로 의존 대상의 최대 + 1, 그 위도 같은 규칙(표에 쓸 값)."""
    mapTier = computeTiers(mapEdge)
    uniqueRemaining = set(listEdgeNodes(mapEdge)) - set(mapTier)

    def tierAbove(listMember: list[str]) -> int:
        return max((mapTier[dep] for member in listMember for dep in mapEdge.get(member, {}) if dep in mapTier), default=-1) + 1

    while uniqueRemaining:
        listReady = sorted(node for node in uniqueRemaining if all(dep in mapTier for dep in mapEdge.get(node, {})))
        if listReady:
            for node in listReady:
                mapTier[node] = tierAbove([node])
            uniqueRemaining -= set(listReady)
            continue
        mapSubEdge = {node: {dep: [] for dep in mapEdge.get(node, {}) if dep in uniqueRemaining} for node in uniqueRemaining}
        listCycle = [component for component in findComponents(mapSubEdge)
                     if all(dep in mapTier or dep in component for member in component for dep in mapEdge.get(member, {}))]
        if not listCycle:
            break
        tier = tierAbove(listCycle[0])
        for node in listCycle[0]:
            mapTier[node] = tier
        uniqueRemaining -= set(listCycle[0])
    return mapTier


def findTierTableProblems(mapEdge: Mapping[str, Mapping[str, Any]], mapTable: Mapping[str, int], mapReason: Mapping[str, str],
                          *, tableLabel: str, reasonLabel: str) -> list[str]:
    """
    표(`mapTable`)가 그래프에서 계산한 티어와 맞는지 — 위반 문구 목록.

    - 강결합 묶음(SCC)이 있다 — 표가 어떻든 순환이다(같은 티어 · 역방향 include 가 표에 흡수되지 않게 여기서 직접 본다).
    - 표 < 계산값: 표가 낡았다(의존 대상이 같거나 높은 티어에 있다).
    - 표 > 계산값: `mapReason` 에 이유가 있어야 한다(여유를 사람이 승인한 것). 이유가 없으면 상향이 흡수된 것이다.
    - 표 == 계산값인데 이유 줄이 있다: 낡은 이유 줄이다. 표에 없는 폴더의 이유 줄도 같다.
    엣지가 하나도 없는 폴더는 검증할 값이 없어 보지 않는다. 표에 없는 폴더는 소스 게이트가 파일마다 따로 알린다.
    """
    listProblem: list[str] = []
    listComponent = findComponents(mapEdge)
    for component in listComponent:
        listProblem.append(f"강결합 묶음(순환): {' · '.join(component)} — 위층 것을 아래층이 들였다. include 를 끊는다(티어 표를 다시 계산해 덮지 않는다)")
    uniqueInCycle = {node for component in listComponent for node in component}
    mapComputed = computeTiers(mapEdge)
    for layer in listEdgeNodes(mapEdge):
        if layer in uniqueInCycle or layer not in mapComputed or layer not in mapTable:
            continue
        tableTier, computedTier = mapTable[layer], mapComputed[layer]
        if tableTier < computedTier:
            listProblem.append(f"{layer}: {tableLabel} 의 티어 {tableTier} 가 그래프 계산값 {computedTier} 보다 낮다 — 표가 낡았거나 같은 티어 · 역방향 include 가 있다")
        elif tableTier > computedTier and layer not in mapReason:
            listProblem.append(f"{layer}: {tableLabel} 의 티어 {tableTier} 가 계산값 {computedTier} 보다 높은데 {reasonLabel} 에 이유가 없다 — 계산값으로 내리거나 이유를 적는다")
        elif tableTier == computedTier and layer in mapReason:
            listProblem.append(f"{layer}: 티어 {tableTier} 가 계산값과 같아 {reasonLabel} 의 이유 줄이 낡았다 — 지운다")
    for layer in sorted(mapReason):
        if layer not in mapTable:
            listProblem.append(f"{layer}: {reasonLabel} 에 있지만 {tableLabel} 에 없는 폴더다")
    return listProblem


def edgesOfFile(sourceLayer: str, listDestLayer: Iterable[str]) -> list[tuple[str, str]]:
    """한 파일이 만드는 (원본, 대상) 폴더 쌍 — 같은 폴더끼리는 빼고 중복은 하나로."""
    return sorted({(sourceLayer, destLayer) for destLayer in listDestLayer if destLayer != sourceLayer})
