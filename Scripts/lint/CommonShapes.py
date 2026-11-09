#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CommonShapes.py

공통 기반이 있는데 손으로 다시 적은 모양을 막는다. 기반을 만들어 일부만 옮겨 태우면 옛 모양이 남아 복사되는 것이 근본 원인이었다(2026-11-07 조사 `refactor_common.md`).
**왜 형식이 아니라 린트인가.** 여섯 다 합법인 C++ 이다 — 칸 이름 · switch 의 글자 · 어떤 함수를 불렀는가는 형식이 말하지 못한다.

1. 철자 같은 열거 이름 표: 모든 case 가 열거자 철자 글을 돌려주는 switch. `ENUM()` + `getEnumName` 이 맡는다(`Core` 는 리플렉션 아래 층이라 보지 않는다).
2. 다시 읽기 횟수 손 대조: `_seen…ReloadCount` 칸 · `getReloadCount` 호출. `AssetReloadWatch` 가 맡는다.
3. 첫 컴포넌트 손 찾기: `if ( pFound == nullptr ) pFound = …` 람다. `AutomationComponentWatch::findFirst` 가 맡는다.
4. 스키마만 읽는 한 줄 검사 함수: `AutomationStep::validateFields<TParams>` 가 맡는다.
5. 캔버스 지우기 손 구간: `ed::BeginDelete`. `EditorNodeGraph::collectRemovals` 가 맡는다.
6. 저장 글 칸 · 부모 차이 손 저장: `string _savedText` 는 `EditorDocumentState`, `DataInheritance::makeDelta` 는 `ResourceComposer::saveDelta` 가 맡는다.
7. id 를 버리는 리소스 입구: 이름이 `load…` 인 입구(`loadFromAsset` · `loadFromFile` · `loadFromPack` · `load` …)가 리소스 글(`AssetIO::readTextAsset`)을 읽은 뒤
   id 없이 글 입구로 넘기면 덮어쓰기 층 · 다시 읽기 기록 · 접속 대조를 받지 못한다(조용히 빠진다). id 를 싣는 길(`ResourceContext::withAsset` ·
   `XMLDocument::parseResourceText` · `XMLReadUtil::parseResourceRoot` · `loadResource…`)을 지나거나, 읽은 id 변수를 그대로 다음 함수에 넘겨야 한다.
   예전에는 이름 둘(`loadFromAsset` · `loadFromFile( const AssetID& )`)만 보아 다른 이름의 입구(타일맵 `load`)가 빠졌다 — 입구는 이름이 아니라 "load 로 시작하고 리소스 글을 읽는다" 로 고른다.

예외는 `Config/Rules/Lint/CommonShapeExceptions.json` 에 까닭과 함께 적는다. 낡은 예외(더는 없는 자리)도 위반이다.
`py -3 -m Scripts lint common-shapes [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kConfigPath = "Config/Rules/Lint/CommonShapeExceptions.json"
kExtensions = (".h", ".cpp", ".inl")
kSkipParts = ("ThirdParty", "Tests")
kCommentPattern = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)

kSwitchPattern = re.compile(r"\bswitch\s*\([^\n]*\)\s*\{")
kCasePattern = re.compile(r"\bcase\s+([\w:]+)\s*:\s*(?:\{\s*)?return\s+\"([^\"]*)\"")
kAnyCasePattern = re.compile(r"\bcase\s+[\w:]+\s*:")
kDefaultNullPattern = re.compile(r"\bdefault\s*:\s*return\s+nullptr")
kSeenCountPattern = re.compile(r"\b_seen\w*ReloadCount\b")
kReloadCallPattern = re.compile(r"::getReloadCount\(\s*\)")
kFirstComponentPattern = re.compile(r"forEachComponentOfType<[^>]*>\([^\n]*\n\s*\{\s*\n\s*if \( pFound == nullptr \)\s*\n\s*pFound = ")
kOneLineValidatorPattern = re.compile(r"expected<void>\s+(?:\w+::)?\w+\(\s*const AutomationStep& step\s*\)\s*\{\s*\w+ params;\s*return step\.readFields\( params \);\s*\}")
kCanvasDeletePattern = re.compile(r"\bed::BeginDelete\(")
kSavedTextPattern = re.compile(r"^\s*string\s+_savedText\s*;", re.M)
kMakeDeltaPattern = re.compile(r"\bDataInheritance::makeDelta\(")
# 정의 줄만 본다(줄머리에 반환 형식이 있다) — `if ( A::loadX( y ) )` 같은 호출은 걸리지 않는다.
kAssetEntrancePattern = re.compile(r"^[ \t]*(?:\[\[nodiscard\]\]\s*)?(?:static\s+)?[\w:<>]+[\s*&]+(\w+::load\w*)\([^;{}]*\)\s*(?:const\s*)?\{", re.M)
kReadTextAssetPattern = re.compile(r"\bAssetIO::readTextAsset\(\s*(\w+)?")
kAssetCarrierPattern = re.compile(r"withAsset\(|parseResourceText\(|parseResourceRoot\(|loadResource\w*\(|loadFromAsset\(")


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


def findSameSpellingSwitches(code: str) -> list[str]:
    """모든 case 가 열거자 철자를 돌려주는 switch 의 열거 이름을 돌려준다. `default: return nullptr`(거르기)는 이름 표가 아니라 보지 않는다."""
    listEnum: list[str] = []
    for match in kSwitchPattern.finditer(code):
        body = code[match.end() - 1:findClosingBrace(code, match.end() - 1)]
        listCase = kCasePattern.findall(body)
        if len(listCase) < 2 or len(listCase) != len(kAnyCasePattern.findall(body)) or kDefaultNullPattern.search(body):
            continue
        if all(case.split("::")[-1] == text for case, text in listCase):
            parts = listCase[0][0].split("::")
            listEnum.append(parts[-2] if len(parts) >= 2 else "?")
    return listEnum


def findIdDroppingEntrances(code: str) -> list[str]:
    """글을 읽은 뒤 id 를 싣지 않는 리소스 입구(`형식::함수`)를 돌려준다."""
    listEntrance: list[str] = []
    for match in kAssetEntrancePattern.finditer(code):
        body = code[match.end() - 1:findClosingBrace(code, match.end() - 1)]
        readMatch = kReadTextAssetPattern.search(body)
        if readMatch is None or kAssetCarrierPattern.search(body, readMatch.end()):
            continue
        idName = readMatch.group(1)
        bPassesID = idName is not None and idName != "AssetID" and re.search(rf"[(,]\s*{re.escape(idName)}\s*[,)]", body[readMatch.end():]) is not None
        if not bPassesID:
            listEntrance.append(match.group(1))
    return listEntrance


def collectViolations(path: str, text: str) -> list[tuple[str, str, str]]:
    """글 하나의 (규칙, 예외 열쇠, 설명) 들을 돌려준다."""
    code = kCommentPattern.sub("", text)
    listViolation: list[tuple[str, str, str]] = []
    if not path.startswith("Source/Core/"):
        for enumName in findSameSpellingSwitches(code):
            listViolation.append(("enumNameSwitch", f"{path}|{enumName}", f"철자 같은 열거 이름 표({enumName}) — ENUM() + getEnumName"))
    if kSeenCountPattern.search(code):
        listViolation.append(("reloadCount", path, "_seen…ReloadCount 칸 — AssetReloadWatch"))
    if kReloadCallPattern.search(code):
        listViolation.append(("reloadCount", path, "getReloadCount 손 호출 — AssetReloadWatch"))
    if kFirstComponentPattern.search(code):
        listViolation.append(("firstComponent", path, "첫 컴포넌트 손 찾기 — AutomationComponentWatch::findFirst"))
    if kOneLineValidatorPattern.search(code):
        listViolation.append(("oneLineValidator", path, "스키마만 읽는 한 줄 검사 함수 — AutomationStep::validateFields<TParams>"))
    if kCanvasDeletePattern.search(code):
        listViolation.append(("canvasDelete", path, "캔버스 지우기 손 구간 — EditorNodeGraph::collectRemovals"))
    if kSavedTextPattern.search(code):
        listViolation.append(("savedText", path, "저장 글 칸 — EditorDocumentState"))
    if kMakeDeltaPattern.search(code):
        listViolation.append(("makeDelta", path, "부모 차이 손 저장 — ResourceComposer::saveDelta"))
    for entrance in findIdDroppingEntrances(code):
        listViolation.append(("assetEntrance", f"{path}|{entrance}", f"id 를 버리는 리소스 입구({entrance}) — ResourceContext::withAsset · parseResourceText · parseResourceRoot"))
    return listViolation


def findProblems(listSource: Iterable[tuple[str, str]], exceptions: dict) -> list[str]:
    listProblem: list[str] = []
    seenKey: set[tuple[str, str]] = set()
    for path, text in listSource:
        for rule, key, message in collectViolations(path, text):
            seenKey.add((rule, key))
            if key not in exceptions.get(rule, {}):
                listProblem.append(f"{path}: {message} (예외는 {kConfigPath} 의 {rule} 에 '{key}' 와 까닭)")
    for rule, table in exceptions.items():
        if rule.startswith("_"):
            continue
        for key in table:
            if (rule, key) not in seenKey:
                listProblem.append(f"{kConfigPath}: 낡은 예외 {rule} '{key}' — 그 모양이 더는 없다, 지운다")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix in kExtensions and not any(part in path.parts for part in kSkipParts):
            try:
                listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
            except (UnicodeDecodeError, OSError):
                continue
    return listSource


def checkCommonShapes(projectRoot: Path) -> list[str]:
    exceptions = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return findProblems(readSources(projectRoot), exceptions)


def runSelfCheck() -> None:
    same = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "Aa";\ncase Kind::Bb: return "Bb";\n}\n}'
    other = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "aa";\ncase Kind::Bb: return "Bb";\n}\n}'
    filtered = 'const utf8* f( Kind k ) { switch ( k )\n{\ncase Kind::Aa: return "Aa";\ncase Kind::Bb: return "Bb";\ndefault: return nullptr;\n}\n}'
    assert findSameSpellingSwitches(same) == ["Kind"]
    assert findSameSpellingSwitches(other) == []
    assert findSameSpellingSwitches(filtered) == []
    assert len(findProblems([("Source/A/a.cpp", same)], {})) == 1
    assert findProblems([("Source/Core/a.cpp", same)], {}) == []
    assert findProblems([("Source/A/a.cpp", same)], {"enumNameSwitch": {"Source/A/a.cpp|Kind": "이유"}}) == []
    stale = findProblems([("Source/A/a.cpp", "int x;")], {"enumNameSwitch": {"Source/A/a.cpp|Kind": "이유"}})
    assert len(stale) == 1 and "낡은" in stale[0], stale
    validator = "static expected<void> validateX( const AutomationStep& step )\n{\n    MyParams params;\n    return step.readFields( params );\n}"
    assert [v[0] for v in collectViolations("Source/A/a.cpp", validator)] == ["oneLineValidator"]
    assert collectViolations("Source/A/a.cpp", "// ed::BeginDelete()\nint x;") == []
    assert [v[0] for v in collectViolations("Source/A/a.cpp", "if ( ed::BeginDelete() )")] == ["canvasDelete"]
    dropping = "bool A::loadFromAsset( const AssetID& assetID )\n{\n    string text;\n    if ( AssetIO::readTextAsset( assetID, text ) == false )\n        return false;\n    return loadFromXMLText( text, assetID.view() );\n}"
    carrying = dropping.replace("loadFromXMLText( text, assetID.view() )", "loadComposed( text, assetID.view(), ResourceContext::forType( *StaticType() ).withAsset( assetID ) )")
    assert findIdDroppingEntrances(dropping) == ["A::loadFromAsset"], findIdDroppingEntrances(dropping)
    assert findIdDroppingEntrances(carrying) == []
    otherName = "bool B::loadFromPack( const DataPath& root )\n{\n    string text;\n    if ( AssetIO::readTextAsset( AssetID{ path }, text ) == false )\n        return false;\n    return loadFromXMLText( text, path );\n}"
    assert findIdDroppingEntrances(otherName) == ["B::loadFromPack"], "이름이 loadFromAsset 이 아니어도 입구다"
    passing = "bool C::loadDesc( const AssetID& assetID )\n{\n    string text;\n    if ( AssetIO::readTextAsset( assetID, text ) == false )\n        return false;\n    return parseDesc( assetID.view(), assetID, text );\n}"
    assert findIdDroppingEntrances(passing) == [], "읽은 id 를 그대로 넘기면 싣는 것이다"
    calling = "void D::f()\n{\n    if ( E::loadThing( x ) )\n    {\n        AssetIO::readTextAsset( AssetID{ p }, t );\n    }\n}"
    assert findIdDroppingEntrances(calling) == [], "호출은 입구가 아니다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="공통 기반이 있는데 손으로 다시 적은 모양을 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[CommonShapes] self-check OK")
        return ExitCode.Passed
    listProblem = checkCommonShapes(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[CommonShapes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
