#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 폴더 간 include 그래프를 다시 재서 **강결합 묶음과 티어를 계산**한다.

[왜 필요한가 — 티어 표는 손으로 고른 순서가 아니다]
`CheckEngineLayers.py` 의 `_kEngineTier` 는 include 그래프를 위상 정렬한 결과다. 코드가 바뀌면 표도
다시 계산해야 하는데, 그 계산을 매번 손으로 하면 "지금 표가 참인가" 를 아무도 확인하지 않게 된다.
이 스크립트는 **게이트와 같은 규칙**으로(prelude·배선 예외, `Graphics/Renderer` 분리 — 전부 게이트
모듈에서 그대로 가져온다) 묶음(Tarjan SCC)과 티어를 찍는다. 그래서 여기 답과 게이트 표가 다르면
표가 낡은 것이다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다. 방향 위반은 `CheckEngineLayers.py` 가 막고, 여기는 "묶음이
생겼나 · 티어가 바뀌었나" 를 보여 준다. 2026-09-21 기준으로 묶음은 없다(DAG) — 이 스크립트가 묶음을
하나라도 찍으면 누군가 위층 것을 아래층에 들여온 것이다.

사용법:
  py -3 Scripts/lint/report/RunEngineLayerGraph.py            # 묶음과 티어
  py -3 Scripts/lint/report/RunEngineLayerGraph.py --edges    # 묶음 안의 엣지를 파일 단위로
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gate"))   # 게이트 모듈의 규칙을 그대로 쓴다

from common import kDirSourceEngine, normalizePath, useUtf8Stdout  # noqa: E402
import CheckEngineLayers as gate  # noqa: E402

kSourceSuffix = (".h", ".cpp", ".inl", ".xxx")


def collectEdgesInternal(repositoryRoot: Path) -> dict[str, dict[str, list[str]]]:
    """레이어 → 레이어 → [그 엣지를 만든 파일]. 게이트가 빼는 파일·헤더는 여기서도 뺀다."""
    engineDir = repositoryRoot / kDirSourceEngine
    listEdge: dict[str, dict[str, list[str]]] = defaultdict(lambda: defaultdict(list))
    for filePath in engineDir.rglob("*"):
        if filePath.suffix not in kSourceSuffix or not filePath.is_file():
            continue
        relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
        if relativeFilePath in gate._kWiringFiles:
            continue
        sourceLayer = gate.engineLayerOfInternal(filePath.relative_to(engineDir).as_posix())
        text = filePath.read_text(encoding="utf-8", errors="replace")
        for includePath in gate._kIncludeRe.findall(text):
            normalizedInclude = normalizePath(includePath)
            if not normalizedInclude.startswith("Engine/") or normalizedInclude in gate._kUbiquitousHeaders:
                continue
            destLayer = gate.engineLayerOfInternal(normalizedInclude[len("Engine/"):])
            if destLayer != sourceLayer:
                listEdge[sourceLayer][destLayer].append(relativeFilePath)
    return listEdge


def findComponentsInternal(listEdge: dict[str, dict[str, list[str]]]) -> list[list[str]]:
    """Tarjan SCC. 크기 2 이상인 묶음만 돌려준다."""
    uniqueNode = set(listEdge)
    for sourceLayer in list(listEdge):
        uniqueNode.update(listEdge[sourceLayer])
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
        for nextNode in listEdge.get(node, {}):
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
    for node in sorted(uniqueNode):
        if node not in mapIndex:
            visit(node)
    return [component for component in listComponent if len(component) > 1]


def computeTiersInternal(listEdge: dict[str, dict[str, list[str]]]) -> dict[str, int]:
    """Kahn — 의존 대상이 전부 놓인 뒤에 놓는다. 묶음이 있으면 그 노드들은 빠진다."""
    uniqueNode = set(listEdge)
    for sourceLayer in list(listEdge):
        uniqueNode.update(listEdge[sourceLayer])
    mapDependency = {node: set(listEdge.get(node, {})) for node in uniqueNode}
    mapTier: dict[str, int] = {}
    uniqueRemaining = set(uniqueNode)
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


def main() -> int:
    useUtf8Stdout()
    parser = argparse.ArgumentParser(description="Engine 폴더 include 그래프의 묶음과 티어를 계산한다")
    parser.add_argument("--root", default=None, help="저장소 루트 (기본: 스크립트 위치에서 추정)")
    parser.add_argument("--edges", action="store_true", help="묶음 안의 엣지를 파일 단위로 찍는다")
    args = parser.parse_args()

    repositoryRoot = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[3]
    listEdge = collectEdgesInternal(repositoryRoot)
    listComponent = findComponentsInternal(listEdge)

    if listComponent:
        print(f"[RunEngineLayerGraph] 강결합 묶음 {len(listComponent)}개 — 누군가 위층 것을 아래층에 들여왔다:")
        for component in listComponent:
            print("  " + " · ".join(component))
            uniqueComponent = set(component)
            for sourceLayer in component:
                for destLayer, listFile in sorted(listEdge[sourceLayer].items()):
                    if destLayer not in uniqueComponent:
                        continue
                    print(f"    {sourceLayer} -> {destLayer}: {len(listFile)}")
                    if args.edges:
                        for relativeFilePath in sorted(set(listFile)):
                            print(f"        {relativeFilePath}")
    else:
        print("[RunEngineLayerGraph] 강결합 묶음 없음 — DAG")

    mapTier = computeTiersInternal(listEdge)
    print("\n티어 (0 = 토대). 게이트의 _kEngineTier 와 다르면 표가 낡은 것이다:")
    for layer in sorted(mapTier, key=lambda name: (mapTier[name], name)):
        expected = gate._kEngineTier.get(layer)
        marker = "" if expected == mapTier[layer] else f"   <- 게이트 표는 {expected}"
        listDependency = sorted(listEdge.get(layer, {}))
        print(f"  {mapTier[layer]:2d}  {layer:20s} -> {', '.join(listDependency)}{marker}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
