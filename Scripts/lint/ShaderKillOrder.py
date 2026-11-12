#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ShaderKillOrder.py

픽셀 버리기(`clip` · `discard` · `SW_APPEARANCE_CLIP`) 뒤에 미분을 쓰는 읽기(암묵 밉 `Sample` · `SampleBias` · `SampleCmp`, `ddx` · `ddy` · `fwidth`)가 오는 함수를 막는다.
Vulkan 1.2 타깃의 `clip` 은 OpKill 이라 버린 픽셀이 쿼드 이웃의 미분에서 빠져 이웃의 밉 선택이 틀린다(binding.hlsli 8-1 절). 1.3 의 demote 를 요구하지 않으려고 순서로 막는다.
**왜 형식이 아니라 린트인가.** HLSL 에는 "이 함수는 미분을 쓴다" 를 형식으로 적을 자리가 없고, 순서는 한 함수 본문 안의 문장 순서다.

- 미분을 쓰는 함수: 본문에 위 읽기가 있거나 그런 함수를 부르는 함수(셰이더 전체의 호출 닫힘). 이름은 `::` 뒤의 마지막 이름으로 본다 — 같은 이름의 정의가 여럿(`#if` 갈래)이면 하나라도 쓰면 쓴다고 본다.
- 버리는 함수: 본문에 버리기가 있거나 그런 함수를 부르는 함수(`Appearance::clipDither`). `SW_APPEARANCE_CLIP(` 는 그 자체로 버리기다.
- 위반: 한 함수 본문에서 첫 버리기 뒤에 미분을 쓰는 읽기 · 호출이 있다. 전처리 갈래는 보지 않는다(보수적).

`py -3 -m Scripts lint shader-kill-order [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.FactCache import Facts, computeVersion, kCacheFolder, openFacts, writeJson  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402

kExtensions = (".hlsl", ".hlsli")
kCommentPattern = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
kPreprocessorPattern = re.compile(r"^[ \t]*#[^\n]*(?:\\\n[^\n]*)*", re.M)
kFunctionPattern = re.compile(r"(?:^|[;}\n])[ \t]*(?:static[ \t]+|inline[ \t]+|precise[ \t]+)*[\w:<>,]+[ \t]+([A-Za-z_][\w:]*)[ \t]*\(([^;{}()]|\([^;{}()]*\))*\)\s*(?::\s*\w+\s*)?\{")
kDerivativePattern = re.compile(r"\.(?:Sample|SampleBias|SampleCmp)\s*\(|\b(?:ddx|ddy|ddx_coarse|ddy_coarse|ddx_fine|ddy_fine|fwidth)\s*\(")
kKillPattern = re.compile(r"\bclip\s*\(|\bdiscard\b|\bSW_APPEARANCE_CLIP\s*\(")
kCallPattern = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
kKeywords = {"if", "for", "while", "switch", "return", "clip", "discard"}


def findClosingBrace(code: str, openIndex: int) -> int:
    depth = 0
    for index in range(openIndex, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code)


def collectFunctions(path: str, text: str) -> list[tuple[str, str, str]]:
    """글 하나의 (경로, 마지막 이름, 본문) 들을 돌려준다. 전처리 줄은 지운다(갈래를 모두 본문으로 본다)."""
    code = kPreprocessorPattern.sub("", kCommentPattern.sub("", text))
    listFunction: list[tuple[str, str, str]] = []
    for match in kFunctionPattern.finditer(code):
        name = match.group(1).split("::")[-1]
        if name in kKeywords:
            continue
        openIndex = match.end() - 1
        listFunction.append((path, name, code[openIndex + 1:findClosingBrace(code, openIndex)]))
    return listFunction


def closeOver(listFunction: list[tuple[str, str, str]], pattern: re.Pattern) -> set[str]:
    """본문에 @p pattern 이 있거나 그런 함수를 부르는 함수 이름의 닫힘이다."""
    setName = {name for _, name, body in listFunction if pattern.search(body)}
    bChanged = True
    while bChanged:
        bChanged = False
        for _, name, body in listFunction:
            if name in setName:
                continue
            if any(call in setName for call in kCallPattern.findall(body)):
                setName.add(name)
                bChanged = True
    return setName


def findFirstKill(body: str, setKill: set[str]) -> int:
    first = len(body)
    match = kKillPattern.search(body)
    if match:
        first = match.start()
    for call in kCallPattern.finditer(body):
        if call.group(1) in setKill and call.start() < first:
            first = call.start()
    return first if first < len(body) else -1


def findProblems(listSource: Iterable[tuple[str, str]]) -> list[str]:
    return findProblemsInFunctions([function for path, text in listSource for function in collectFunctions(path, text)])


def judgeFunction(path: str, name: str, body: str, setKill: set[str], setDerivative: set[str]) -> str | None:
    """함수 하나의 위반 줄(없으면 None) — 버리기 · 미분 닫힘(`setKill` · `setDerivative`)과 그 함수 본문만 본다."""
    killIndex = findFirstKill(body, setKill)
    if killIndex < 0:
        return None
    rest = body[killIndex:]
    rest = rest[rest.find("(") + 1:] if rest.startswith(("clip", "SW_APPEARANCE_CLIP")) or rest[:1].isalpha() else rest
    derivative = kDerivativePattern.search(rest)
    lateCall = next((call.group(1) for call in kCallPattern.finditer(rest) if call.group(1) in setDerivative and call.group(1) != name), None)
    if not (derivative or lateCall):
        return None
    what = derivative.group(0).strip() if derivative else f"{lateCall}()"
    return f"{path}: {name} 이 픽셀을 버린 뒤 미분을 쓰는 읽기({what})를 한다 — 버리기를 출력을 다 만든 뒤로 옮긴다(binding.hlsli 8-1 절)"


def findProblemsInFunctions(listFunction: list[tuple[str, str, str]]) -> list[str]:
    """(경로, 이름, 몸) 목록 — 버리기 · 미분을 부르는 함수 닫힘은 파일을 건너므로 셰이더 전체를 넘긴다(자체 검사의 입구)."""
    setDerivative = closeOver(listFunction, kDerivativePattern)
    setKill = closeOver(listFunction, kKillPattern)
    return [problem for path, name, body in listFunction if (problem := judgeFunction(path, name, body, setKill, setDerivative)) is not None]


def foldFunctions(path: str, listFunction: list[list[str]]) -> list[str]:
    """파일 하나의 함수마다 요약 토큰 — (경로, 차례, 이름, 본문 해시, 직접 버리기, 직접 미분, 부르는 이름들). 본문은 조각에만 둔다."""
    return ["\t".join([path, str(index), name, hashlib.sha1(body.encode("utf-8")).hexdigest()[:16], "1" if kKillPattern.search(body) else "0",
                       "1" if kDerivativePattern.search(body) else "0", ",".join(sorted(set(kCallPattern.findall(body))))])
            for index, (name, body) in enumerate(listFunction)]


@dataclass(frozen=True)
class FunctionRow:
    path: str
    index: int
    name: str
    bodyHash: str
    bKill: bool
    bDerivative: bool
    setCall: frozenset[str]


def parseRow(token: str) -> FunctionRow:
    path, index, name, bodyHash, kill, derivative, calls = token.split("\t")
    return FunctionRow(path, int(index), name, bodyHash, kill == "1", derivative == "1", frozenset(calls.split(",")) - {""})


def closeOverRows(listRow: list[FunctionRow], bDirect: Callable[[FunctionRow], bool]) -> set[str]:
    """`closeOver` 와 같은 닫힘을 역방향 간선(부르는 이름 → 그 이름을 부르는 함수 이름)으로 — 본문 없이 요약만으로."""
    mapCaller: dict[str, set[str]] = {}
    for row in listRow:
        for call in row.setCall:
            mapCaller.setdefault(call, set()).add(row.name)
    setName = {row.name for row in listRow if bDirect(row)}
    listPending = list(setName)
    while listPending:
        for caller in mapCaller.get(listPending.pop(), ()):
            if caller not in setName:
                setName.add(caller)
                listPending.append(caller)
    return setName


def judgeIncremental(listRow: list[FunctionRow], mapCached: dict[str, str], readBodies: Callable[[list[str]], dict[str, list[list[str]]]],
                     listPathOrder: Sequence[str]) -> tuple[list[str], dict[str, str]]:
    """
    요약(함수마다의 행)으로 닫힘을 내고, 버리기에 닿는 함수(직접 버리거나 버리는 이름을 부른다)만 판정한다. 판정은 (경로, 차례, 이름, 본문 해시, 부르는 이름 중 닫힘에 든 것)을
    열쇠로 `mapCached` 에서 꺼내고, 없을 때만 그 파일의 본문(`readBodies`)을 읽는다 — 바뀐 파일의 함수와, 닫힘 소속이 바뀐 이름을 부르는 함수만 다시 판정한다.
    돌려주는 것은 (위반 줄, 새 판정 캐시).
    """
    setKill = closeOverRows(listRow, lambda row: row.bKill)
    setDerivative = closeOverRows(listRow, lambda row: row.bDerivative)
    mapKey: dict[str, FunctionRow] = {}
    for row in listRow:
        if row.bKill or row.setCall & setKill:
            mapKey[json.dumps([row.path, row.index, row.name, row.bodyHash, sorted(row.setCall & setKill), sorted(row.setCall & setDerivative)])] = row
    listMiss = sorted({row.path for key, row in mapKey.items() if key not in mapCached})
    mapBody = readBodies(listMiss) if listMiss else {}
    mapResult: dict[str, str] = {}
    for key, row in mapKey.items():
        if key in mapCached:
            mapResult[key] = mapCached[key]
        else:
            name, body = mapBody[row.path][row.index]
            mapResult[key] = judgeFunction(row.path, name, body, setKill, setDerivative) or ""
    mapOrder = {path: order for order, path in enumerate(listPathOrder)}
    listProblem = [mapResult[key] for key, row in sorted(mapKey.items(), key=lambda item: (mapOrder.get(item[1].path, len(mapOrder)), item[1].path, item[1].index))
                   if mapResult[key]]
    return listProblem, mapResult


def checkShaderKillOrder(projectRoot: Path, scope: LintScope) -> list[str]:
    """
    Content · Source 의 셰이더 함수 — 함수마다의 요약 행(`foldFunctions`)만 읽어 버리기 · 미분 닫힘을 내고(역방향 간선), 버리기에 닿는 함수의 판정은
    `judgement.json` 에 둔다. 본문 조각은 판정이 없는(바뀐 파일 · 닫힘 소속이 바뀐 이름을 부르는) 함수의 파일만 읽는다(`judgeIncremental`).
    """
    if not scope.bWholeRepo and not scope.touches(("Content", "Source"), kExtensions):
        return []
    listPath = [path for path in listRepoFiles(projectRoot)
                if path.startswith(("Content/", "Source/")) and path.endswith(kExtensions) and "ThirdParty" not in path.split("/")]

    def openFunctions(want: Iterable[str]) -> Facts:
        return openFacts(projectRoot, "shader-kill-order", [__file__], listPath, lambda path, text: [[name, body] for _, name, body in collectFunctions(path, text)],
                         want=want, contribute=foldFunctions)

    listRow = [parseRow(token) for token in openFunctions(()).summary]
    judgementPath = projectRoot / kCacheFolder / "shader-kill-order" / "judgement.json"
    version = computeVersion([__file__])   # 판정 글이 바뀌면 캐시한 판정을 버린다
    try:
        document = json.loads(judgementPath.read_text(encoding="utf-8"))
        mapCached = document["results"] if document.get("version") == version else {}
    except (OSError, ValueError, KeyError):
        mapCached = {}
    listProblem, mapResult = judgeIncremental(listRow, mapCached, lambda listMiss: openFunctions(listMiss).mapFact, listPath)
    if mapResult != mapCached:
        judgementPath.parent.mkdir(parents=True, exist_ok=True)
        writeJson(judgementPath, {"version": version, "results": mapResult})
    return listProblem


def runSelfCheck() -> None:
    helper = "float4 readAlbedo( float2 uv )\n{\n    return g_Albedo.Sample( g_Sampler, uv );\n}\n"
    killer = "static void Appearance::clipDither( float a, float2 p )\n{\n    clip( a - 0.5f );\n}\n"
    bad = helper + killer + "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    Appearance::clipDither( 0.5f, uv );\n    return readAlbedo( uv );\n}\n"
    good = helper + killer + "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    const float4 albedo = readAlbedo( uv );\n    Appearance::clipDither( 0.5f, uv );\n    return albedo;\n}\n"
    direct = "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    clip( uv.x - 0.5f );\n    return float4( ddx( uv ), 0.0f, 1.0f );\n}\n"
    level = "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    discard;\n    return g_Albedo.SampleLevel( g_Sampler, uv, 0.0f );\n}\n"
    assert len(findProblems([("a.hlsl", bad)])) == 1, findProblems([("a.hlsl", bad)])
    assert findProblems([("a.hlsl", good)]) == [], findProblems([("a.hlsl", good)])
    assert len(findProblems([("a.hlsl", direct)])) == 1
    assert findProblems([("a.hlsl", level)]) == []  # 밉을 적은 읽기는 미분을 쓰지 않는다
    assert findProblems([("a.hlsl", "// clip( x ); g.Sample( s, uv );\nfloat f() { return 1.0f; }\n")]) == []
    # 요약 + 판정 캐시(증분)가 전체 판정과 같다 — 처음(캐시 없음), 그대로(본문을 읽지 않는다), 버리기 함수를 고친 뒤(그 이름을 부르는 함수만 다시 판정).
    mapSource = {"a.hlsl": helper + killer, "b.hlsl": "float4 PSMain( float2 uv : TEXCOORD0 ) : SV_Target\n{\n    Appearance::clipDither( 0.5f, uv );\n    return readAlbedo( uv );\n}\n",
                 "c.hlsl": good.replace("PSMain", "PSGood"), "d.hlsl": direct}

    def incremental(mapText: dict[str, str], mapCached: dict[str, str]) -> tuple[list[str], dict[str, str], list[str]]:
        mapFact = {path: [[name, body] for _, name, body in collectFunctions(path, text)] for path, text in mapText.items()}
        listRead: list[str] = []
        listRow = [parseRow(token) for path in sorted(mapFact) for token in foldFunctions(path, mapFact[path])]
        listProblem, mapResult = judgeIncremental(listRow, mapCached, lambda listMiss: (listRead.extend(listMiss), {path: mapFact[path] for path in listMiss})[1], sorted(mapText))
        assert listProblem == findProblems(sorted(mapText.items())), (listProblem, findProblems(sorted(mapText.items())))
        return listProblem, mapResult, listRead

    listProblem, mapCached, listRead = incremental(mapSource, {})
    assert len(listProblem) == 2 and listRead, listProblem
    assert incremental(mapSource, mapCached)[2] == [], "바뀐 것이 없으면 본문을 읽지 않는다"
    mapSplit = {"h.hlsl": helper, "k.hlsl": killer, "p.hlsl": mapSource["b.hlsl"], "d.hlsl": direct}
    _, mapCached, _ = incremental(mapSplit, {})
    listProblem, _, listRead = incremental(dict(mapSplit, **{"h.hlsl": helper.replace("g_Albedo.Sample( g_Sampler, uv )", "float4( uv, 0.0f, 1.0f )")}), mapCached)
    assert listRead == ["p.hlsl"] and len(listProblem) == 1, (listRead, listProblem)   # 미분 닫힘에서 빠진 이름을 부르는 함수의 파일만 다시 읽는다


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="픽셀 버리기 뒤의 미분 읽기를 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ShaderKillOrder] self-check OK")
        return ExitCode.Passed
    listProblem = checkShaderKillOrder(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ShaderKillOrder] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
