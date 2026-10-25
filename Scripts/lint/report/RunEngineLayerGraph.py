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
  py -3 Scripts/lint/report/RunEngineLayerGraph.py --apply    # 폴더를 옮긴 뒤 — 게이트 티어 표를 계산값으로(내리는 것만)
  py -3 Scripts/lint/report/RunEngineLayerGraph.py --apply --raise-reason "<사유>"   # 올리는 것까지(사유가 표 위 주석으로 남는다)

[표를 덮어쓰는 것은 내리기만 한다 — 올리기는 사유가 있어야 한다]
`--apply` 가 계산값을 그대로 쓰면 위층 것을 아래층에 들인 위반(= 계산 티어 상승)이 표 재계산으로 흡수되어 게이트가 통과한다.
그래서 `--apply` 는 **내리기 · 같은 값만** 쓰고, 올리기는 `--raise-reason` 사유가 있을 때만 쓰며 사유를 그 줄 위 주석으로 남긴다.
`[tierReason]`(계산값보다 일부러 높게 둔 폴더 → 이유)에 있는 폴더는 사람이 정한 값이라 `--apply` 가 건드리지 않는다.
이 막이 못 막는 틈: 사유 글 자체가 맞는지(리뷰의 몫)와 표를 손으로 고치는 것 — 후자는 `CheckEngineLayers` 의 "표 = 계산값" 검사가 잡는다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import iterIncludes  # noqa: E402
from common.Constants import kCppAllExtensions, kDirSourceEngine  # noqa: E402
from common.RuleData import kExemptionKey  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from lint.LayerGraph import computeTiers, computeTiersFillingComponents, findComponents, newEdgeMap  # noqa: E402
from lint.LintReport import LintReport, ReportContext  # noqa: E402
from lint.LayerRules import engineFileEdges, kEngineRootLayerName, readEngineLayerRules  # noqa: E402

#: 규칙 데이터는 스크립트 트리의 것이다(`common.RuleData` 가 읽는 자리와 같다).
kRulePath = Path(__file__).resolve().parents[1] / "rules" / "CheckEngineLayers.toml"
kSourceSuffix = tuple(sorted(kCppAllExtensions | {".xxx"}))   # 게이트(`CheckEngineLayers.sourceSuffixes`)와 같은 파일


def collectEdgesInternal(repositoryRoot: Path) -> dict[str, dict[str, list[str]]]:
    """레이어 → 레이어 → [그 엣지를 만든 파일]. 게이트가 빼는 파일·헤더는 여기서도 뺀다(`lint.LayerRules.engineFileEdges` — 게이트와 같은 함수)."""
    mapExemption = readEngineLayerRules()[kExemptionKey]
    listEdge: dict[str, dict[str, list[str]]] = newEdgeMap()
    for filePath in (repositoryRoot / kDirSourceEngine).rglob("*"):
        if filePath.suffix not in kSourceSuffix or not filePath.is_file():
            continue
        relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
        text = filePath.read_text(encoding="utf-8", errors="replace")
        for sourceLayer, destLayer in engineFileEdges(relativeFilePath, [includePath for _, includePath in iterIncludes(text)], mapExemption):
            listEdge[sourceLayer][destLayer].append(relativeFilePath)
    return listEdge


#: 티어 표(`rules/CheckEngineLayers.toml` 의 `[tier]`) 한 줄 — `이름 = 숫자` 또는 `"<root>" = 숫자`, 끝 주석은 그대로 둔다.
_kTierEntryRe = re.compile(r'^(?:"([^"]+)"|([A-Za-z0-9_-]+))\s*=\s*(\d+)(\s*#.*)?$')


def readKeepingNewlineInternal(path: Path) -> tuple[str, str]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        text = stream.read()
    return text, ("\r\n" if "\r\n" in text else "\n")


def writeTierTablesInternal(rulePath: Path, mapTier: dict[str, int], mapReason: dict[str, str], raiseReason: str) -> tuple[list[str], list[str]]:
    """
    티어 표(`Scripts/lint/rules/CheckEngineLayers.toml` 의 `[tier]`)를 계산값으로 고쳐 쓴다 — **내리기 · 같은 값만**.
    올리기는 `raiseReason`(빈 글이면 거부)이 있을 때만 쓰고 사유를 그 줄 위 주석으로 남긴다. `[tierReason]` 에 있는 폴더는 건드리지 않는다.
    TOML 항목(바로 위 주석 줄 + 키 줄)은 (티어, 원래 순서)로 다시 줄 세운다. (고친 파일의 저장소 상대 경로, 거부한 올리기 설명)을 돌려준다.
    """
    listChanged: list[str] = []
    listRefused: list[str] = []
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
        currentTier = int(match.group(3))
        tier = mapTier.get(layerName, currentTier)
        if layerName == kEngineRootLayerName and layerName not in mapTier:
            tier = max(mapTier.values(), default=0) + 1
        if layerName in mapReason:
            tier = currentTier   # 사람이 정한 값(`[tierReason]`) — 계산값으로 덮지 않는다
        elif tier > currentTier:
            if raiseReason:
                listComment.append(f"# 상향 {currentTier} -> {tier}: {raiseReason}")
            else:
                listRefused.append(f"{layerName} {currentTier} -> {tier}")
                tier = currentTier
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

    return listChanged, listRefused


class RunEngineLayerGraphReport(LintReport):
    description = "Engine 폴더 include 그래프의 묶음과 티어를 계산한다(`--apply` 는 티어 표를 내리는 쪽으로 고쳐 쓴다)"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--edges", action="store_true", help="묶음 안의 엣지를 파일 단위로 찍는다")
        parser.add_argument("--apply", action="store_true",
                            help="게이트 티어 표(rules/CheckEngineLayers.toml)를 계산값으로 고쳐 쓴다 — 내리기 · 같은 값만(폴더를 옮긴 뒤)")
        parser.add_argument("--raise-reason", default="",
                            help="--apply 가 티어를 올리는 것까지 쓰게 한다 — 사유가 그 줄 위 주석으로 남는다(사유 없이는 올리지 않는다)")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
        listEdge = collectEdgesInternal(repositoryRoot)
        listComponent = findComponents(listEdge)

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

        mapTier = computeTiers(listEdge)
        ruleData = readEngineLayerRules()
        mapGateTier: dict[str, int] = ruleData["tier"]
        mapReason: dict[str, str] = ruleData["tierReason"]
        print("\n티어 (0 = 토대). rules/CheckEngineLayers.toml 의 [tier] 와 다르면 표가 낡은 것이다(`[tierReason]` 폴더는 이유가 있는 여유):")
        for layer in sorted(mapTier, key=lambda name: (mapTier[name], name)):
            expected = mapGateTier.get(layer)
            marker = "" if expected == mapTier[layer] else f"   <- 게이트 표는 {expected}" + (" (이유 있음)" if layer in mapReason else "")
            listDependency = sorted(listEdge.get(layer, {}))
            print(f"  {mapTier[layer]:2d}  {layer:20s} -> {', '.join(listDependency)}{marker}")
        if args.apply:
            listChanged, listRefused = writeTierTablesInternal(kRulePath, computeTiersFillingComponents(listEdge), mapReason, args.raise_reason.strip())
            print("\n[RunEngineLayerGraph] 티어 표를 고쳤다: " + " · ".join(listChanged) if listChanged else "\n[RunEngineLayerGraph] 티어 표가 이미 계산값과 같다")
            if listRefused:
                print("[RunEngineLayerGraph] 올리지 않았다(사유 없이 올리면 위반이 표에 흡수된다): " + " · ".join(listRefused)
                      + "\n  include 를 끊거나, 정말 올릴 일이면 --raise-reason \"<사유>\" 를 준다.")
                return ExitCode.Violation
        return ExitCode.Passed


main = RunEngineLayerGraphReport.run


if __name__ == "__main__":
    sys.exit(main())
