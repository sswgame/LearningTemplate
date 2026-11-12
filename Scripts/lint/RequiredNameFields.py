#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/RequiredNameFields.py

데이터에 있어야 할 이름 · 경로 칸이 C++ 기본값으로 숨는 모양을 막는다.

`PROPERTY( … )` 칸의 형식이 이름 · 경로 계열(형식 목록은 `Config/Rules/Lint/RequiredNameFieldExceptions.json` 의 `_types`)이고, 헤더 초기값이나
같은 타입 생성자(같은 폴더 `.cpp` · `.inl`)의 초기화 목록에 비어 있지 않은 문자열 리터럴이 있으면서 주석에 `Required` · `Transient` 가 없으면 오류다.
예: Shooter3D 아바타의 발 뼈 `_leftFootBone{ "foot.l" }` 은 프리팹이 값을 적지 않아 실제 스켈레톤(`foot_l`)에 없는 이름으로 측정이 늘 0 이었다.

고치는 길: 데이터마다 다른 값이면 C++ 기본값을 지우고 `Required` 를 달아 데이터 파일에 적는다(빠지면 `Serialization.RequiredMissing`).
정말 엔진 상수(데이터가 아닌 값)이면 예외 표에 `경로::타입::칸` 과 까닭을 적는다. 표에서 사라진 칸의 항목도 오류다(표가 썩지 않게).

**왜 린트인가.** 리터럴 초기값은 합법 C++ 이고, 형식으로 막으려면 이름 칸마다 리터럴을 받지 않는 새 형식을 리플렉션 생성기 · 직렬화기에 들여야 한다
(생성기는 NEXT 5-4 W-R3 설계와 겹친다). 초기값이 "형식일 뿐인 빈 값"인지 "데이터가 덮을 기본값"인지는 형식이 말하지 못한다.

`py -3 -m Scripts lint required-name-fields [--all] [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
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
from lint.FactCache import loadFacts  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402

kConfigPath = "Config/Rules/Lint/RequiredNameFieldExceptions.json"
kPropertyHead = re.compile(r"^[ \t]*PROPERTY\s*\(", re.M)
kClassHead = re.compile(r"\b(?:class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)?(\w+)\s*(?:final\s*)?(?::[^{;]*)?$")
kOptOut = re.compile(r"\b(?:Required|required|Transient|transient|NonSerialized|nonSerialized)\b(?!\s*=\s*false)")
kLiteral = re.compile(r"\"[^\"\n]+\"")
kTypeNoise = re.compile(r"\b(?:const|std::|sw::|mutable)\s*|\s+")


def blankCode(text: str) -> tuple[str, str]:
    """(주석만 지운 글, 주석 · 문자열을 지운 글) — 둘 다 같은 길이라 자리를 함께 쓴다."""
    listKept: list[str] = []
    listCode: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i) or text.startswith("/*", i):
            end = text.find("\n", i) if text[i + 1] == "/" else text.find("*/", i) + 2
            end = n if end <= i else end
            blank = re.sub(r"[^\n]", " ", text[i:end])
            listKept.append(blank)
            listCode.append(blank)
            i = end
        elif text[i] in "\"'":
            end = i + 1
            while end < n and text[end] != text[i] and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            end = min(end + 1, n)
            listKept.append(text[i:end])
            listCode.append(text[i] + " " * (end - i - 2) + text[end - 1] if end - i >= 2 else text[i:end])
            i = end
        else:
            listKept.append(text[i])
            listCode.append(text[i])
            i += 1
    return "".join(listKept), "".join(listCode)


def skipGroup(code: str, start: int) -> int:
    """`code[start]` 이 여는 괄호일 때 짝 닫는 괄호 다음 자리."""
    depth = 0
    for i in range(start, len(code)):
        if code[i] in "({[":
            depth += 1
        elif code[i] in ")}]":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(code)


def findClassAt(code: str, listOffset: list[int]) -> dict[int, str]:
    """자리마다 그 자리를 감싼 가장 안쪽 class · struct 이름."""
    result: dict[int, str] = {}
    stack: list[str | None] = []
    boundary = 0
    pending = sorted(listOffset)
    cursor = 0
    for i, c in enumerate(code):
        while cursor < len(pending) and pending[cursor] <= i:
            result[pending[cursor]] = next((name for name in reversed(stack) if name), "")
            cursor += 1
        if c == "{":
            match = kClassHead.search(code[boundary:i].strip())
            stack.append(match.group(1) if match else None)
            boundary = i + 1
        elif c == "}":
            if stack:
                stack.pop()
            boundary = i + 1
        elif c == ";":
            boundary = i + 1
    return result


def findFields(kept: str, code: str, setType: set[str], tuplePrefix: tuple[str, ...]) -> list[tuple[str, str, str, int]]:
    """헤더의 이름 계열 PROPERTY 칸 (타입, 칸, 헤더 초기값, 줄) — Required · Transient 는 뺀다."""
    listField: list[tuple[int, str, str, int]] = []
    for match in kPropertyHead.finditer(code):
        argsEnd = skipGroup(code, match.end() - 1)
        if kOptOut.search(code[match.end():argsEnd]):
            continue
        declEnd = code.find(";", argsEnd)
        decl = code[argsEnd:declEnd]
        head = re.match(r"\s*([^{=;]*?)\s*\b(\w+)\s*(?=[{=]|$)", decl)
        if not head or "(" in head.group(1):
            continue
        typeName = kTypeNoise.sub("", head.group(1))
        if typeName not in setType and not typeName.startswith(tuplePrefix):
            continue
        initial = kept[argsEnd + head.end():declEnd]
        listField.append((match.start(), head.group(2), initial, code.count("\n", 0, match.start()) + 1))
    mapClass = findClassAt(code, [offset for offset, _, _, _ in listField])
    return [(mapClass[offset], name, initial, line) for offset, name, initial, line in listField]


def findConstructorInitials(kept: str, code: str, className: str) -> dict[str, list[str]]:
    """`.cpp` 글 하나에서 `Class::Class( … ) : _a{ … }, _b( … )` 의 칸마다 초기값 글."""
    result: dict[str, list[str]] = {}
    for match in re.finditer(rf"\b{className}::{className}\s*\(", code):
        cursor = skipGroup(code, match.end() - 1)
        tail = re.match(r"\s*(?:noexcept\s*)?:", code[cursor:])
        if not tail:
            continue
        cursor += tail.end()
        while True:
            member = re.match(r"\s*([\w:]+)\s*(?=[({])", code[cursor:])
            if not member:
                break
            groupStart = cursor + member.end()
            groupEnd = skipGroup(code, groupStart)
            result.setdefault(member.group(1), []).append(kept[groupStart:groupEnd])
            comma = re.match(r"\s*,", code[groupEnd:])
            if not comma:
                break
            cursor = groupEnd + comma.end()
    return result


kConstructorHead = re.compile(r"\b(\w+)::\1\s*\(")


def isTarget(path: str) -> bool:
    return path.startswith("Source/") and path.endswith((".h", ".cpp", ".inl")) and "Tests" not in path.split("/") and not path.endswith(".generated.h")


def extractFacts(path: str, text: str, config: dict) -> dict:
    """파일 하나의 사실 — 헤더는 이름 계열 PROPERTY 칸(타입, 칸, 헤더 초기값, 줄), 본문(`.cpp` · `.inl`)은 생성자마다 칸 → 초기값 글."""
    kept, code = blankCode(text)
    if path.endswith(".h"):
        bProperty = "PROPERTY" in text
        return {"fields": [list(field) for field in findFields(kept, code, set(config["_types"]), tuple(config["_typePrefixes"]))] if bProperty else []}
    mapConstructor = {className: findConstructorInitials(kept, code, className) for className in dict.fromkeys(kConstructorHead.findall(code))}
    return {"constructors": {className: initials for className, initials in mapConstructor.items() if initials}}


def judgeFacts(mapFact: dict[str, dict], config: dict, bWholeRepo: bool = True) -> list[str]:
    """mapFact: 경로 → `extractFacts`(경로 순). 헤더 칸의 생성자 초기값은 같은 폴더의 본문에서만 찾는다."""
    exceptions: dict[str, str] = config["_exceptions"]
    listProblem: list[str] = []
    setSeen: set[str] = set()
    for headerPath, fact in mapFact.items():
        listField = fact.get("fields")
        if not listField:
            continue
        folder = headerPath.rpartition("/")[0]
        mapInitial: dict[str, dict[str, list[str]]] = {}
        for className, name, initial, line in listField:
            if className not in mapInitial:
                mapInitial[className] = {}
                for sourcePath, sourceFact in mapFact.items():
                    if className and sourcePath.rpartition("/")[0] == folder and className in sourceFact.get("constructors", {}):
                        for member, listText in sourceFact["constructors"][className].items():
                            mapInitial[className].setdefault(member, []).extend(listText)
            literal = kLiteral.search(initial) or next((found for found in map(kLiteral.search, mapInitial[className].get(name, [])) if found), None)
            if not literal:
                continue
            key = f"{headerPath}::{className}::{name}"
            setSeen.add(key)
            if key not in exceptions:
                listProblem.append(f"{headerPath}:{line}: {className}::{name} = {literal.group(0)} — 데이터에 있어야 할 이름이면 기본값을 지우고 PROPERTY( Required ) + 데이터 파일에, 엔진 상수면 예외 표({kConfigPath})에 까닭과 함께")
    for key in exceptions if bWholeRepo else ():   # 낡은 예외는 저장소 전체를 봐야 안다
        if key not in setSeen:
            listProblem.append(f"{kConfigPath}: '{key}' 는 더 이상 위반이 아니다 — 표에서 지운다")
    return listProblem


def findProblems(mapHeader: dict[str, str], mapSource: dict[str, str], config: dict, bWholeRepo: bool = True) -> list[str]:
    """mapHeader · mapSource: 경로 → 글(`.cpp` 는 헤더와 같은 폴더의 것만 본다) — 자체 검사의 입구."""
    mapText = {**mapHeader, **mapSource}
    return judgeFacts({path: extractFacts(path, mapText[path], config) for path in sorted(mapText)}, config, bWholeRepo)


def checkRequiredNameFields(projectRoot: Path, scope: LintScope) -> list[str]:
    """
    scope 의 파일이 있던 폴더(지운 파일 포함 — .cpp 를 지우면 헤더 칸의 생성자 초기값이 사라진다)의 헤더 · 본문을 본다. 파일마다의 사실(칸 · 생성자 초기값)은
    내용 해시 색인(`FactCache`)에 두어, 스테이지 파일 하나에 그 폴더를 다시 읽지 않는다.
    """
    config = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    setFolder = {path.rpartition("/")[0] for path in (*scope.listFile, *scope.listRemoved) if isTarget(path)}
    if not setFolder:
        return []
    listPath = [path for path in listRepoFiles(projectRoot) if isTarget(path)]
    mapFact = loadFacts(projectRoot, "required-name-fields", [__file__, str(projectRoot / kConfigPath)], listPath,
                        lambda path, text: extractFacts(path, text, config), errors="strict",
                        want=[path for path in listPath if path.rpartition("/")[0] in setFolder], shardOf=lambda path: path.rpartition("/")[0])   # 한 폴더는 한 조각
    return judgeFacts(mapFact, config, scope.bWholeRepo)


def runSelfCheck() -> None:
    config = {"_types": ["string", "hashed_string"], "_typePrefixes": ["AssetRef<"], "_exceptions": {}}
    header = (
        "namespace sw\n{\n    class SW_API Foo : public Component\n    {\n"
        "        PROPERTY( Category = \"A\", Tooltip = \"Required later\" )\n        string _bone;\n"
        "        PROPERTY()\n        hashed_string _slot{ \"Head\" };\n"
        "        PROPERTY( Required )\n        string _must;\n"
        "        PROPERTY( Transient )\n        hashed_string _cache{ \"X\" };\n"
        "        PROPERTY()\n        float32 _speed;\n"
        "        PROPERTY()\n        string _empty;\n"
        "    };\n}\n"
    )
    source = "namespace sw\n{\n    Foo::Foo()\n        : _bone{ \"foot.l\" } // 주석 \"x\"\n        , _must{ \"m\" }\n        , _speed( 1.0f )\n        , _empty{}\n    {\n    }\n}\n"
    listProblem = findProblems({"a/Foo.h": header}, {"a/Foo.cpp": source}, config)
    assert len(listProblem) == 2, listProblem
    assert "Foo::_bone" in listProblem[0] and "Foo::_slot" in listProblem[1], listProblem
    config["_exceptions"] = {"a/Foo.h::Foo::_slot": "엔진 상수", "a/Foo.h::Foo::_gone": "까닭"}
    listProblem = findProblems({"a/Foo.h": header}, {"a/Foo.cpp": source}, config)
    assert len(listProblem) == 2 and "_gone" in listProblem[1], listProblem
    assert findProblems({"a/Foo.h": header}, {"b/Foo.cpp": source}, {**config, "_exceptions": {"a/Foo.h::Foo::_slot": "까닭"}}) == [], "다른 폴더의 .cpp 는 보지 않는다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 기본값으로 숨은 이름 · 경로 PROPERTY 칸을 찾는다")
    parser.add_argument("--all", action="store_true", help="전체 소스(기본과 같다 — 다른 규칙과 인자를 맞춘다)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[RequiredNameFields] self-check OK")
        return ExitCode.Passed
    listProblem = checkRequiredNameFields(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[RequiredNameFields] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
