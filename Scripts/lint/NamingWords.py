#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/NamingWords.py

함수 이름의 앞 단어와 형식 이름의 끝 단어를 허용 표와 대조한다.

**왜 린트인가.** 이름이 업계 용어인지는 형식이 말할 수 없다. 표 없이 눈에 띈 이름만 고치면 `drain` · `Since` 처럼 같은 일에 새 낱말이
계속 들어온다(2026-11-07). 새 낱말은 언리얼 · 유니티 · Godot 과 대조한 뒤 `Config/Rules/Lint/NamingWords.json` 에 올린다.
`_banned` 는 쓰지 않기로 한 낱말과 대신 쓸 말이고, `_keep` 은 그 낱말을 쓰는 까닭이 있는 이름 하나씩이다.
`_bannedNames` 는 전수 개명한 옛 식별자(통째 이름)와 새 이름이다 — 개명 뒤 다른 작업자 브랜치 · 붙여 넣은 코드가 옛 이름을 되살리지 못하게 한다(주석 · 문자열 밖 코드만 본다).

`py -3 -m Scripts lint naming-words [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
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

kConfigPath = "Config/Rules/Lint/NamingWords.json"
kExtensions = (".h", ".cpp", ".inl")
kComment = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
kString = re.compile(r'R"\((?:.|\n)*?\)"|"(?:[^"\\\n]|\\.)*"')
kFunction = re.compile(r"\b_?([a-z][a-z0-9]*)[A-Z]\w*\s*\(")
kType = re.compile(r"\b(?:class|struct|enum class)\s+(?:SW_\w+\s+)?([A-Z]\w+)")
kWord = re.compile(r"[A-Z][a-z0-9]+|[A-Z]+(?![a-z])")


def collectWords(text: str) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """글 하나의 (함수 앞 단어, 이름) 과 (형식 끝 단어, 이름) 을 돌려준다."""
    code = kString.sub('""', kComment.sub("", text))
    listFunction = [(match.group(1), match.group(0).split("(")[0].strip()) for match in kFunction.finditer(code)]
    listSuffix: list[tuple[str, str]] = []
    for match in kType.finditer(code):
        words = kWord.findall(match.group(1))
        if words:
            listSuffix.append((words[-1], match.group(1)))
    return listFunction, listSuffix


def findProblems(listSource: Iterable[tuple[str, str]], table: dict) -> list[str]:
    allowedFunction = set(table["_functionWords"])
    allowedSuffix = set(table["_typeSuffixes"])
    banned = table["_banned"]
    keep = table["_keep"]
    listProblem: list[str] = []
    seen: set[str] = set()

    def report(path: str, word: str, name: str, label: str, column: str) -> None:
        if name in seen:
            return
        seen.add(name)
        hint = banned[word] if word in banned else f"업계 용어와 대조한 뒤 {kConfigPath} 의 {column} 에 올린다"
        listProblem.append(f"{path}: {label} '{word}'({name}) 가 표에 없다 — {hint}")

    bannedNames = table.get("_bannedNames", {})
    bannedPattern = re.compile(r"\b(" + "|".join(map(re.escape, bannedNames)) + r")\b") if bannedNames else None
    for path, text in listSource:
        if bannedPattern is not None:
            for match in bannedPattern.finditer(kString.sub('""', kComment.sub("", text))):
                name = match.group(1)
                if f"name:{name}" not in seen:
                    seen.add(f"name:{name}")
                    listProblem.append(f"{path}: 옛 이름 '{name}' — {bannedNames[name]}")
        listFunction, listSuffix = collectWords(text)
        for word, name in listFunction:
            if word not in allowedFunction and name not in keep:
                report(path, word, name, "함수 앞 단어", "_functionWords")
        for word, name in listSuffix:
            if word not in allowedSuffix and name not in keep:
                report(path, word, name, "형식 끝 단어", "_typeSuffixes")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for path in (projectRoot / "Source").rglob("*"):
        if path.suffix in kExtensions:
            try:
                listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
            except (UnicodeDecodeError, OSError):
                continue
    return listSource


def checkNamingWords(projectRoot: Path) -> list[str]:
    table = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return findProblems(readSources(projectRoot), table)


def runSelfCheck() -> None:
    table = {"_functionWords": ["get"], "_typeSuffixes": ["Component"], "_banned": {"drain": "flush 또는 poll"}, "_keep": {"MailboxOutcome": "이유"}}
    clean = 'struct FooComponent {}; int getValue( int x ); struct MailboxOutcome {}; // drainEvents()\nconst char* s = "drainEvents()";'
    assert findProblems([("a.h", clean)], table) == [], "허용 · 주석 · 문자열 · 예외는 통과해야 한다"
    bad = findProblems([("a.h", "void drainEvents( int x ); struct FooVerdict {};")], table)
    assert len(bad) == 2, bad
    assert "flush 또는 poll" in bad[0], bad[0]
    assert "'Verdict'" in bad[1], bad[1]
    tableNames = dict(table, _bannedNames={"CombatHealth": "HealthStateMachine"})
    assert findProblems([("a.h", "// CombatHealth\nconst char* s = \"CombatHealth\";")], tableNames) == [], "주석 · 문자열의 옛 이름은 본다고 하지 않는다"
    assert len(findProblems([("a.h", "CombatHealth health; int getCombatHealthRatio();")], tableNames)) == 1, "통째 이름만 막는다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="함수 앞 단어 · 형식 끝 단어를 허용 표와 대조한다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[NamingWords] self-check OK")
        return ExitCode.Passed
    listProblem = checkNamingWords(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[NamingWords] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
