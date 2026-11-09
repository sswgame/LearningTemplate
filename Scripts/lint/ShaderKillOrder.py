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
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

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
    listFunction: list[tuple[str, str, str]] = []
    for path, text in listSource:
        listFunction.extend(collectFunctions(path, text))
    setDerivative = closeOver(listFunction, kDerivativePattern)
    setKill = closeOver(listFunction, kKillPattern)
    listProblem: list[str] = []
    for path, name, body in listFunction:
        killIndex = findFirstKill(body, setKill)
        if killIndex < 0:
            continue
        rest = body[killIndex:]
        rest = rest[rest.find("(") + 1:] if rest.startswith(("clip", "SW_APPEARANCE_CLIP")) or rest[:1].isalpha() else rest
        derivative = kDerivativePattern.search(rest)
        lateCall = next((call.group(1) for call in kCallPattern.finditer(rest) if call.group(1) in setDerivative and call.group(1) != name), None)
        if derivative or lateCall:
            what = derivative.group(0).strip() if derivative else f"{lateCall}()"
            listProblem.append(f"{path}: {name} 이 픽셀을 버린 뒤 미분을 쓰는 읽기({what})를 한다 — 버리기를 출력을 다 만든 뒤로 옮긴다(binding.hlsli 8-1 절)")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for root in (projectRoot / "Content", projectRoot / "Source"):
        for path in root.rglob("*"):
            if path.suffix in kExtensions and "ThirdParty" not in path.parts:
                try:
                    listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
                except (UnicodeDecodeError, OSError):
                    continue
    return listSource


def checkShaderKillOrder(projectRoot: Path) -> list[str]:
    return findProblems(readSources(projectRoot))


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


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="픽셀 버리기 뒤의 미분 읽기를 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ShaderKillOrder] self-check OK")
        return ExitCode.Passed
    listProblem = checkShaderKillOrder(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ShaderKillOrder] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
