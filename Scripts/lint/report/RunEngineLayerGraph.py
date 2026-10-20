#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 폴더 간 include 그래프를 다시 측정해서 **강결합 묶음과 티어를 계산**한다.

[왜 필요한가 — 티어 표는 손으로 고른 순서가 아니다]
`Scripts/lint/rules/CheckEngineLayers.toml` 의 `[tier]` 는 include 그래프를 위상 정렬한 결과다. 코드가 바뀌면 표도
다시 계산해야 하는데, 그 계산을 매번 손으로 하면 "지금 표가 참인가" 를 아무도 확인하지 않게 된다.
이 스크립트는 **게이트와 같은 규칙**으로(prelude·배선 예외, 레이어 이름 — 전부 게이트
모듈에서 그대로 가져온다) 묶음(Tarjan SCC)과 티어를 찍는다. 그래서 여기 답과 게이트 표가 다르면
표가 낡은 것이다.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다. 방향 위반은 `CheckEngineLayers.py` 가 막고, 여기는 "묶음이
생겼나 · 티어가 바뀌었나" 를 보여 준다. 묶음은 없다(DAG) — 이 스크립트가 묶음을
하나라도 찍으면 누군가 위층 것을 아래층에 들여온 것이다.

사용법:
  py -3 Scripts/lint/report/RunEngineLayerGraph.py            # 묶음과 티어
  py -3 Scripts/lint/report/RunEngineLayerGraph.py --edges    # 묶음 안의 엣지를 파일 단위로
  py -3 Scripts/lint/report/RunEngineLayerGraph.py --apply    # 폴더를 옮긴 뒤 — 게이트 티어 표를 계산값으로
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

from common import iterIncludes, kDirSourceEngine, normalizePath  # noqa: E402
from common.RuleData import kExemptionKey  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402
from LayerRules import engineLayerOf, kEnginePreludePrefix, kEngineRootLayerName, kEngineWiringPrefix, readEngineLayerRules  # noqa: E402

kSourceSuffix = (".h", ".cpp", ".inl", ".xxx")


def collectEdgesInternal(repositoryRoot: Path) -> dict[str, dict[str, list[str]]]:
    """레이어 → 레이어 → [그 엣지를 만든 파일]. 게이트가 빼는 파일·헤더는 여기서도 뺀다."""
    engineDir = repositoryRoot / kDirSourceEngine
    mapExemption = readEngineLayerRules()[kExemptionKey]
    listEdge: dict[str, dict[str, list[str]]] = defaultdict(lambda: defaultdict(list))
    for filePath in engineDir.rglob("*"):
        if filePath.suffix not in kSourceSuffix or not filePath.is_file():
            continue
        relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
        if kEngineWiringPrefix + relativeFilePath in mapExemption:
            continue
        sourceLayer = engineLayerOf(filePath.relative_to(engineDir).as_posix())
        text = filePath.read_text(encoding="utf-8", errors="replace")
        for _, includePath in iterIncludes(text):
            normalizedInclude = normalizePath(includePath)
            if not normalizedInclude.startswith("Engine/") or kEnginePreludePrefix + normalizedInclude in mapExemption:
                continue
            destLayer = engineLayerOf(normalizedInclude[len("Engine/"):])
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


def computeTiersFillingComponentsInternal(listEdge: dict[str, dict[str, list[str]]]) -> dict[str, int]:
    """`computeTiersInternal` 에 묶음까지 놓는다 — 바깥 의존이 다 놓인 묶음은 한 덩어리로 의존 대상의 최대 + 1, 그 위도 같은 규칙(표에 쓸 값)."""
    mapTier = computeTiersInternal(listEdge)
    uniqueNode = set(listEdge)
    for source in list(listEdge):
        uniqueNode.update(listEdge[source])
    uniqueRemaining = uniqueNode - set(mapTier)

    def tierAboveInternal(listMember: list[str]) -> int:
        return max((mapTier[dep] for member in listMember for dep in listEdge.get(member, {}) if dep in mapTier), default=-1) + 1

    while uniqueRemaining:
        listReady = sorted(node for node in uniqueRemaining if all(dep in mapTier for dep in listEdge.get(node, {})))
        if listReady:
            for node in listReady:
                mapTier[node] = tierAboveInternal([node])
            uniqueRemaining -= set(listReady)
            continue
        mapSubEdge = {node: {dep: [] for dep in listEdge.get(node, {}) if dep in uniqueRemaining} for node in uniqueRemaining}
        listCycle = [component for component in findComponentsInternal(mapSubEdge)
                     if all(dep in mapTier or dep in component for member in component for dep in listEdge.get(member, {}))]
        if not listCycle:
            break
        tier = tierAboveInternal(listCycle[0])
        for node in listCycle[0]:
            mapTier[node] = tier
        uniqueRemaining -= set(listCycle[0])
    return mapTier


#: 티어 표(`rules/CheckEngineLayers.toml` 의 `[tier]`) 한 줄 — `이름 = 숫자` 또는 `"<root>" = 숫자`, 끝 주석은 그대로 둔다.
_kTierEntryRe = re.compile(r'^(?:"([^"]+)"|([A-Za-z0-9_-]+))\s*=\s*(\d+)(\s*#.*)?$')


def readKeepingNewlineInternal(path: Path) -> tuple[str, str]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        text = stream.read()
    return text, ("\r\n" if "\r\n" in text else "\n")


def writeTierTablesInternal(repositoryRoot: Path, mapTier: dict[str, int]) -> list[str]:
    """
    티어 표(`Scripts/lint/rules/CheckEngineLayers.toml` 의 `[tier]`)를 계산값으로 고쳐 쓴다.
    TOML 항목(바로 위 주석 줄 + 키 줄)은 (티어, 원래 순서)로 다시 줄 세운다. 고친 파일의 저장소 상대 경로를 돌려준다.
    """
    listChanged: list[str] = []
    rulePath = Path(__file__).resolve().parents[1] / "rules" / "CheckEngineLayers.toml"   # 규칙 데이터는 스크립트 트리의 것(RuleData 와 같다)
    text, newline = readKeepingNewlineInternal(rulePath)
    listLine = text.split(newline)
    beginIndex = next(index for index, line in enumerate(listLine) if line == "[tier]")
    endIndex = next((index for index in range(beginIndex + 1, len(listLine)) if listLine[index].startswith("[")), len(listLine))
    listEntry: list[tuple[int, int, str, list[str]]] = []
    listComment: list[str] = []
    for line in listLine[beginIndex + 1 : endIndex]:
        match = _kTierEntryRe.match(line)
        if not match:
            listComment.append(line)
            continue
        layerName = match.group(1) or match.group(2)
        tier = mapTier.get(layerName, int(match.group(3)))
        if layerName == kEngineRootLayerName and layerName not in mapTier:
            tier = max(mapTier.values(), default=0) + 1
        keyText = f'"{match.group(1)}"' if match.group(1) else match.group(2)
        listEntry.append((tier, len(listEntry), layerName, listComment + [f"{keyText} = {tier}{match.group(4) or ''}"]))
        listComment = []
    listEntry.sort(key=lambda entry: (entry[0], entry[1]))
    listBody = [line for entry in listEntry for line in entry[3]] + listComment
    newText = newline.join(listLine[: beginIndex + 1] + listBody + listLine[endIndex:])
    if newText != text:
        with rulePath.open("w", encoding="utf-8", newline="") as stream:
            stream.write(newText)
        listChanged.append("Scripts/lint/rules/CheckEngineLayers.toml")

    return listChanged


class RunEngineLayerGraphReport(LintReport):
    description = "Engine 폴더 include 그래프의 묶음과 티어를 계산한다(`--apply` 는 티어 표를 고쳐 쓴다)"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--edges", action="store_true", help="묶음 안의 엣지를 파일 단위로 찍는다")
        parser.add_argument("--apply", action="store_true",
                            help="게이트 티어 표(rules/CheckEngineLayers.toml)를 계산값으로 고쳐 쓴다(폴더를 옮긴 뒤)")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
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
        mapGateTier: dict[str, int] = readEngineLayerRules()["tier"]
        print("\n티어 (0 = 토대). rules/CheckEngineLayers.toml 의 [tier] 와 다르면 표가 낡은 것이다:")
        for layer in sorted(mapTier, key=lambda name: (mapTier[name], name)):
            expected = mapGateTier.get(layer)
            marker = "" if expected == mapTier[layer] else f"   <- 게이트 표는 {expected}"
            listDependency = sorted(listEdge.get(layer, {}))
            print(f"  {mapTier[layer]:2d}  {layer:20s} -> {', '.join(listDependency)}{marker}")
        if args.apply:
            listChanged = writeTierTablesInternal(repositoryRoot, computeTiersFillingComponentsInternal(listEdge))
            print("\n[RunEngineLayerGraph] 티어 표를 고쳤다: " + " · ".join(listChanged) if listChanged else "\n[RunEngineLayerGraph] 티어 표가 이미 계산값과 같다")
        return 0


main = RunEngineLayerGraphReport.run


if __name__ == "__main__":
    sys.exit(main())
