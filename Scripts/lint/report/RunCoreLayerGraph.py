#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core 폴더 간 include 그래프를 파일 단위로 뽑아 **강결합 묶음과 거꾸로 가는 include** 를 보여 준다.

[왜 필요한가]
`Source/Core` 의 폴더(`Common` · `Container` · `Log` …)는 하나의 정적 라이브러리 안에 있어서, 폴더 사이에 순환이
생겨도 빌드는 깨지지 않는다. 그런데 Core 를 여러 라이브러리로 나누거나 아래층만 따로 쓰려면 폴더 그래프가 DAG 여야 한다.
이 스크립트는 `#include "Core/…"` 를 전부 읽어 폴더 → 폴더 간선과 그 간선을 만든 파일을 모으고,
묶음(Tarjan SCC)과 티어(Kahn)를 계산한다. 묶음이 있으면 거꾸로 가는 간선 수가 가장 적은 순서를 찾아
(가중치 = include 건수, 삽입 이동으로 국소 개선) 끊어야 할 간선을 파일 단위로 찍는다.

[게이트와의 관계]
`Check*` 이 막고 `Run*` 은 보고한다. 방향 위반은 `Scripts/lint/gate/CheckCoreLayers.py` 가 막는다. 여기서 계산한 티어가
게이트의 `_kCoreTier` 와 다르면 표가 낡은 것이다.

[규칙]
- 노드는 `Core/` 바로 아래 폴더다. 루트 파일(`CoreMinimal.h` · `pch.h`)은 모아 주는 헤더라 노드가 아니다 — 대신 폴더 안 파일이
  그것을 include 하면 거꾸로 가는 간선(`(root)`)으로 센다.
- `Network` 는 위층이다. 다른 Core 폴더가 그것을 include 하면 거꾸로 가는 간선이다.

사용법:
  py -3 Scripts/lint/report/RunCoreLayerGraph.py              # 묶음 · 티어 · 거꾸로 가는 간선 요약
  py -3 Scripts/lint/report/RunCoreLayerGraph.py --edges      # 거꾸로 가는 간선을 파일 단위로
  py -3 Scripts/lint/report/RunCoreLayerGraph.py --all-edges  # 모든 폴더 간 간선을 파일 단위로
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

from common import normalizePath  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

kDirSourceCore = "Source/Core"
kRootNode = "(root)"
kSourceSuffix = (".h", ".hpp", ".cpp", ".inl", ".xxx")
_kIncludeRe = re.compile(r'^\s*#\s*include\s*"(Core/[^"]+)"', re.MULTILINE)

#: (원본 폴더, 대상 폴더) → [(파일, 대상 헤더)]
EdgeMap = dict[str, dict[str, list[tuple[str, str]]]]


def coreLayerOf(pathAfterCore: str) -> str:
    """`Core/` 뒤의 경로 → 폴더 이름. 루트 파일이면 `(root)`."""
    listPart = pathAfterCore.split("/")
    return listPart[0] if len(listPart) > 1 else kRootNode


def collectEdges(repositoryRoot: Path) -> EdgeMap:
    coreDir = repositoryRoot / kDirSourceCore
    mapEdge: EdgeMap = defaultdict(lambda: defaultdict(list))
    for filePath in sorted(coreDir.rglob("*")):
        if filePath.suffix not in kSourceSuffix or not filePath.is_file():
            continue
        sourceLayer = coreLayerOf(filePath.relative_to(coreDir).as_posix())
        if sourceLayer == kRootNode:
            continue
        relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
        text = filePath.read_text(encoding="utf-8", errors="replace")
        for includePath in _kIncludeRe.findall(text):
            destLayer = coreLayerOf(normalizePath(includePath)[len("Core/"):])
            if destLayer != sourceLayer:
                mapEdge[sourceLayer][destLayer].append((relativeFilePath, includePath))
    return mapEdge


def listNodes(mapEdge: EdgeMap) -> list[str]:
    uniqueNode = set(mapEdge)
    for sourceLayer in mapEdge:
        uniqueNode.update(mapEdge[sourceLayer])
    return sorted(uniqueNode)


def findComponents(mapEdge: EdgeMap) -> list[list[str]]:
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
    for node in listNodes(mapEdge):
        if node not in mapIndex:
            visit(node)
    return [component for component in listComponent if len(component) > 1]


def computeTiers(mapEdge: EdgeMap) -> dict[str, int]:
    """Kahn — 의존 대상이 전부 놓인 뒤에 놓는다. 묶음 안의 노드는 빠진다."""
    listNode = listNodes(mapEdge)
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


def countBackwardEdges(mapEdge: EdgeMap, listOrder: list[str]) -> int:
    """`listOrder` 는 아래층부터. 앞의 노드가 뒤의 노드를 include 하면 거꾸로다."""
    mapPosition = {node: index for index, node in enumerate(listOrder)}
    total = 0
    for sourceLayer, mapDest in mapEdge.items():
        for destLayer, listFile in mapDest.items():
            if mapPosition[destLayer] > mapPosition[sourceLayer]:
                total += len(listFile)
    return total


def findMinimalOrder(mapEdge: EdgeMap) -> list[str]:
    """거꾸로 가는 include 수가 적은 순서(아래층부터). Eades 탐욕 순서를 삽입 이동으로 더는 줄지 않을 때까지 다듬는다."""
    listNode = listNodes(mapEdge)
    mapWeight: dict[tuple[str, str], int] = {}
    for sourceLayer, mapDest in mapEdge.items():
        for destLayer, listFile in mapDest.items():
            mapWeight[(sourceLayer, destLayer)] = len(listFile)

    # 탐욕: 의존이 가장 "아래로 많이" 향하는 노드부터 아래층에 놓는다.
    uniqueRemaining = set(listNode)
    listOrder: list[str] = []
    while uniqueRemaining:
        def score(node: str) -> int:
            outWeight = sum(mapWeight.get((node, other), 0) for other in uniqueRemaining)
            inWeight = sum(mapWeight.get((other, node), 0) for other in uniqueRemaining)
            return inWeight - outWeight
        best = max(sorted(uniqueRemaining), key=score)
        listOrder.append(best)
        uniqueRemaining.discard(best)

    bestCost = countBackwardEdges(mapEdge, listOrder)
    bImproved = True
    while bImproved:
        bImproved = False
        for node in list(listOrder):
            listWithout = [other for other in listOrder if other != node]
            for position in range(len(listWithout) + 1):
                listCandidate = listWithout[:position] + [node] + listWithout[position:]
                cost = countBackwardEdges(mapEdge, listCandidate)
                if cost < bestCost:
                    bestCost = cost
                    listOrder = listCandidate
                    bImproved = True
    return listOrder


def loadGateTiers() -> dict[str, int] | None:
    """게이트가 있으면 그 표를 쓴다(없으면 None — 표 비교를 건너뛴다)."""
    gateDir = Path(__file__).resolve().parents[1] / "gate"
    if not (gateDir / "CheckCoreLayers.py").is_file():
        return None
    sys.path.insert(0, str(gateDir))
    import CheckCoreLayers as gate  # noqa: E402
    return dict(gate._kCoreTier)


def printEdges(mapEdge: EdgeMap, sourceLayer: str, destLayer: str, bDetail: bool, indent: str) -> None:
    listFile = mapEdge[sourceLayer][destLayer]
    print(f"{indent}{sourceLayer} -> {destLayer}: {len(listFile)}")
    if bDetail:
        for relativeFilePath, includePath in sorted(listFile):
            print(f"{indent}    {relativeFilePath}  <{includePath}>")


class RunCoreLayerGraphReport(LintReport):
    description = "Core 폴더 include 그래프의 묶음 · 티어 · 거꾸로 가는 include 를 계산한다"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--edges", action="store_true", help="거꾸로 가는 간선을 파일 단위로 찍는다")
        parser.add_argument("--all-edges", action="store_true", help="모든 폴더 간 간선을 파일 단위로 찍는다")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        mapEdge = collectEdges(context.repositoryRoot)
        listComponent = findComponents(mapEdge)
        mapGateTier = loadGateTiers()

        if args.all_edges:
            print("[RunCoreLayerGraph] 폴더 간 간선:")
            for sourceLayer in sorted(mapEdge):
                for destLayer in sorted(mapEdge[sourceLayer]):
                    printEdges(mapEdge, sourceLayer, destLayer, True, "  ")
            print()

        if listComponent:
            print(f"[RunCoreLayerGraph] 강결합 묶음 {len(listComponent)}개:")
            for component in listComponent:
                print("  " + " · ".join(component))
            listOrder = findMinimalOrder(mapEdge)
            backwardCount = countBackwardEdges(mapEdge, listOrder)
            print(f"\n거꾸로 가는 include 가 가장 적은 순서(아래층부터, {backwardCount} 건):")
            print("  " + " → ".join(listOrder))
            mapPosition = {node: index for index, node in enumerate(listOrder)}
            print("\n거꾸로 가는 간선:")
            for sourceLayer in listOrder:
                for destLayer in sorted(mapEdge.get(sourceLayer, {}), key=lambda name: mapPosition[name]):
                    if mapPosition[destLayer] > mapPosition[sourceLayer]:
                        printEdges(mapEdge, sourceLayer, destLayer, args.edges, "  ")
        else:
            print("[RunCoreLayerGraph] 강결합 묶음 없음 — DAG")

        mapTier = computeTiers(mapEdge)
        print("\n티어 (0 = 토대)" + (". 게이트의 _kCoreTier 와 다르면 표가 낡은 것이다:" if mapGateTier is not None else ":"))
        for layer in sorted(mapTier, key=lambda name: (mapTier[name], name)):
            marker = ""
            if mapGateTier is not None and mapGateTier.get(layer) != mapTier[layer]:
                marker = f"   <- 게이트 표는 {mapGateTier.get(layer)}"
            print(f"  {mapTier[layer]:2d}  {layer:16s} -> {', '.join(sorted(mapEdge.get(layer, {})))}{marker}")
        return 0


main = RunCoreLayerGraphReport.run


if __name__ == "__main__":
    sys.exit(main())
