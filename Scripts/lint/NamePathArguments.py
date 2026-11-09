#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/NamePathArguments.py

경로 · 리소스 id 를 받는 함수의 인자에 `x.c_str()` · `x.view()` 를 그대로 넘기는 곳을 찾는다.

**왜 린트인가.** 빈 `hashed_string` 의 `c_str()` · `view()` 는 언리얼 FName 처럼 "None" 을 낸다(로그 · 직렬화가 그 글을 쓴다).
그 글이 경로 자리로 가면 'None' 파일을 찾는다(CharacterEditorModel::openBodyData 가 매 프레임 그랬다). 경로 쪽 글은
`viewOrEmpty()` 가 내고, 형식으로는 막지 못한다 — `string_view` 를 받는 함수는 어떤 글이든 받는 정상 코드이고, 받는 쪽이
`hashed_string` 인지 린트는 모른다. 그래서 그 자리에서 `c_str()` · `view()` 를 통째로 막는다. 다른 형식에는 고칠 길이 하나씩 있다:
`string` 은 그 값을 그대로, `AssetID` 는 `str()`, `AssetRef` 는 그대로(경로로 바뀐다), `hashed_string` 은 `viewOrEmpty()`.
함수 목록은 `Config/Rules/Lint/NamePathArguments.json` 에 있다. 글을 지역 변수에 담았다가 넘기는 곳은 못 본다(머리말의 한계).

`py -3 -m Scripts lint name-path-arguments [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
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

kConfigPath = "Config/Rules/Lint/NamePathArguments.json"
kCommentOrString = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)
kBareText = re.compile(r"^[\w.\->\[\]()\s]*?(?:\.|->)\s*(c_str|view)\s*\(\s*\)$")


def blankCommentsAndStrings(text: str) -> str:
    """주석 · 글 리터럴을 같은 길이의 빈칸으로 바꾼다(줄 번호와 괄호 짝을 지킨다)."""
    return kCommentOrString.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def splitArguments(code: str, start: int) -> list[str]:
    """`start` 가 여는 괄호 바로 뒤일 때 최상위 쉼표로 가른 인자들을 돌려준다."""
    listArgument: list[str] = []
    depth = 0
    begin = start
    cursor = start
    while cursor < len(code):
        char = code[cursor]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            if depth == 0:
                listArgument.append(code[begin:cursor])
                return listArgument
            depth -= 1
        elif char == "," and depth == 0:
            listArgument.append(code[begin:cursor])
            begin = cursor + 1
        cursor += 1
    return listArgument


def findBareTextArguments(text: str, config: dict) -> list[tuple[int, str, str]]:
    """글 하나에서 (줄, 함수, 인자) 를 돌려준다."""
    code = blankCommentsAndStrings(text)
    mapPathIndex = {**config["_functions"], **config["_cacheFunctions"]}
    cacheReceiver = re.compile(config["_cacheReceiver"])
    call = re.compile(r"\b(" + "|".join(map(re.escape, mapPathIndex)) + r")\s*[({]")
    listFound: list[tuple[int, str, str]] = []
    for match in call.finditer(code):
        name = match.group(1)
        if name in config["_cacheFunctions"] and cacheReceiver.search(code[max(0, match.start() - 120):match.start()]) is None:
            continue
        listArgument = splitArguments(code, match.end())
        index = mapPathIndex[name]
        if index < len(listArgument) and kBareText.match(listArgument[index].strip()):
            listFound.append((code.count("\n", 0, match.start()) + 1, name, " ".join(listArgument[index].split())))
    return listFound


def findProblems(listSource: Iterable[tuple[str, str]], config: dict) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        for line, name, argument in findBareTextArguments(text, config):
            listProblem.append(f"{path}:{line}: {name}( {argument} ) — 빈 이름이면 \"None\" 을 찾는다: hashed_string 은 viewOrEmpty(), string 은 그대로, AssetID 는 str()")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix not in (".cpp", ".h", ".inl"):
            continue
        try:
            listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
        except (UnicodeDecodeError, OSError):
            continue
    return listSource


def readConfig(projectRoot: Path) -> dict:
    return json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))


def checkNamePathArguments(projectRoot: Path) -> list[str]:
    return findProblems(readSources(projectRoot), readConfig(projectRoot))


def runSelfCheck() -> None:
    config = readConfig(getProjectRoot())
    bad = "void f()\n{\n    cache.acquire( part._material.c_str() );\n    pMesh->setMeshID( part._asset.view() );\n    AssetIO::hasAsset( AssetID{ name.c_str() } );\n}\n"
    assert len(findProblems([("a.cpp", bad)], config)) == 3, "경로 자리의 c_str · view 는 잡아야 한다"
    spawn = "void f()\n{\n    getAssetCache<PrefabCache>().spawn( &manager, part._asset.c_str(), part._partName.c_str() );\n}\n"
    assert len(findProblems([("a.cpp", spawn)], config)) == 1, "경로 인자 자리만 본다(뒤의 인스턴스 이름은 경로가 아니다)"
    clean = ("void f()\n{\n    cache.acquire( part._material.viewOrEmpty() );\n    client.request( builder.view(), body );\n"
             "    TagID::request( tag.view() );\n    // setMeshID( a.c_str() )\n    log( \"setMeshID( a.c_str() )\" );\n"
             "    pMesh->setMeshID( a.empty() ? string_view{} : a.view() );\n}\n")
    assert findProblems([("a.cpp", clean)], config) == [], "viewOrEmpty · 캐시가 아닌 request · 주석 · 글 · 걸러 낸 식은 통과해야 한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="경로 · 리소스 id 를 받는 함수에 c_str() · view() 를 그대로 넘기는 곳을 찾는다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[NamePathArguments] self-check OK")
        return ExitCode.Passed
    listProblem = checkNamePathArguments(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[NamePathArguments] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
