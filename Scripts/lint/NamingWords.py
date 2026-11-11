#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/NamingWords.py

이름이 업계 용어(언리얼 · 유니티 · Godot · 일반 프로그래밍)를 쓰는지 표(`Config/Rules/Lint/NamingWords.json`)와 대조한다.

**왜 린트인가.** 이름이 업계 용어인지는 형식이 말할 수 없다. 표 없이 눈에 띈 이름만 고치면 `drain` · `Since` 처럼 같은 일에 새 낱말이
계속 들어온다(2026-11-07). 예전 검사는 함수 첫 단어와 형식 끝 단어만 봐서, 이름 가운데의 금지어(`hasPendingWork` · `XMLFields::readFields`)와
3인칭 문장형 술어(`shouldDrawBatchInPass`) · 동사 없는 게터(`materialGroupFor`)가 그대로 통과했다(2026-11-12 Source 전체 점검 약 470 건).

규칙:
- function · suffix — 함수 첫 단어는 `_functionWords`, 형식 끝 단어는 `_typeSuffixes` 에 있어야 한다.
- verb — 함수 첫 단어가 금지 동사(`_banned`)면 허용 표에 있어도 실패한다(예전에는 허용 표에 없을 때 안내로만 썼다).
- term — 금지 명사(`_bannedTerms`)는 모든 식별자(함수 · 형식 · 멤버 · 지역)의 **모든 단어**에서 본다. 표준 합성어(`WorkArea` — Win32 `SPI_GETWORKAREA`)는 `_keepTerms`.
- predicate — `bool` 을 돌려주는 함수의 첫 단어가 3인칭 동사(끝이 s)면 `_predicateWords`(언리얼 `Is · Has · Can · Should · Uses · Needs · Requires · Supports …`)여야 한다.
- lookup — 끝 단어가 `For` · `Of` 인 함수는 조회 동사(`_lookupWords` — get · find · compute …)로 시작해야 한다(`materialGroupFor` 는 "찾거나 만든다" 를 숨겼다).
- 이름 하나씩의 예외는 `_keep`, 전수 개명한 옛 이름은 `_bannedNames`(주석 · 문자열 밖 코드만 본다).

verb · term · predicate · lookup 은 기준선(`Config/Rules/Lint/NamingBaseline.json`)을 둔다 — 지금 있는 이름만 적혀 있고 **줄기만 한다**:
새 위반은 실패, 고쳐 사라진 이름이 기준선에 남아도 실패(`--lower-baseline` 으로 뺀다). 영역별 이전이 기준선을 0 으로 줄인다.

`py -3 -m Scripts lint naming-words [--self-check | --write-baseline | --lower-baseline]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
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
kBaselinePath = "Config/Rules/Lint/NamingBaseline.json"
kRatchetRules = ("verb", "term", "predicate", "lookup")
kExtensions = (".h", ".cpp", ".inl")
kComment = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
kString = re.compile(r'R"\((?:.|\n)*?\)"|"(?:[^"\\\n]|\\.)*"')
kFunction = re.compile(r"\b_?([a-z][a-z0-9]*)[A-Z]\w*\s*\(")
kType = re.compile(r"\b(?:class|struct|enum class)\s+(?:SW_\w+\s+)?([A-Z]\w+)")
kBoolFunction = re.compile(r"\bbool\s+(?:\w+::)*(_?[a-z][A-Za-z0-9]*)\s*\(")
kIdentifier = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
kWord = re.compile(r"[A-Z][a-z0-9]+|[A-Z]+(?![a-z])")
kAnyWord = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


def stripCode(text: str) -> str:
    return kString.sub('""', kComment.sub("", text))


def splitWords(name: str, bFunction: bool = False) -> list[str]:
    """식별자 하나의 단어들 — 앞 `_` 과 머리(`k` 상수 · `b` 불 · `s` 정적, 변수면 `arr` · `list` · `map` · `set` 컬렉션)를 떼고 낙타 · 밑줄로 가른다.
    함수 이름은 컬렉션 머리를 떼지 않는다(`setAxisTags` 의 `set` 은 동사다)."""
    words: list[str] = []
    for part in name.strip("_").split("_"):
        words.extend(kAnyWord.findall(part))
    prefixes = ("k", "b", "s", "g", "m", "p") if bFunction else ("k", "b", "s", "g", "m", "p", "arr", "list", "map", "set")
    if len(words) > 1 and words[0] in prefixes:
        words = words[1:]
    return words


def isThirdPersonVerb(word: str) -> bool:
    """3인칭 단수 동사 꼴(끝이 s)이면 true — s 로 끝나는 명사 · 명령형(`focus` · `axis` · `atlas` · `class`)은 뺀다."""
    return word.endswith("s") and not word.endswith(("ss", "us", "is", "as", "os"))


def collectWords(code: str) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """코드(주석 · 문자열을 뺀 글)의 (함수 앞 단어, 이름) 과 (형식 끝 단어, 이름) 을 돌려준다."""
    listFunction = [(match.group(1), match.group(0).split("(")[0].strip()) for match in kFunction.finditer(code)]
    listSuffix: list[tuple[str, str]] = []
    for match in kType.finditer(code):
        words = kWord.findall(match.group(1))
        if words:
            listSuffix.append((words[-1], match.group(1)))
    return listFunction, listSuffix


def findTermWord(name: str, bannedTerms: dict, keepTerms: Sequence[str]) -> str | None:
    """@p name 의 단어 가운데 금지 명사가 있으면 그 단어(표 철자)입니다. 표준 합성어(`_keepTerms`)는 먼저 지운다."""
    stripped = name
    for term in keepTerms:
        stripped = re.sub(re.escape(term), "", stripped, flags=re.I)
    lowered = {term.lower(): term for term in bannedTerms}
    for word in splitWords(stripped):
        if word.lower() in lowered:
            return lowered[word.lower()]
    return None


def classifyNames(listSource: Iterable[tuple[str, str]], table: dict) -> tuple[list[str], dict[str, dict[str, str]]]:
    """(기준선 없는 문제 목록, 규칙 → {이름: 경로 · 안내}) — 기준선 규칙의 위반은 이름으로 모아 기준선과 견준다."""
    allowedFunction = set(table["_functionWords"])
    allowedSuffix = set(table["_typeSuffixes"])
    bannedVerbs = table["_banned"]
    bannedTerms = table.get("_bannedTerms", {})
    keepTerms = table.get("_keepTerms", [])
    predicateWords = set(table.get("_predicateWords", []))
    lookupWords = set(table.get("_lookupWords", []))
    keep = table["_keep"]
    bannedNames = table.get("_bannedNames", {})
    bannedPattern = re.compile(r"\b(" + "|".join(map(re.escape, bannedNames)) + r")\b") if bannedNames else None

    listProblem: list[str] = []
    seen: set[str] = set()
    ratchet: dict[str, dict[str, str]] = {rule: {} for rule in kRatchetRules}

    def record(rule: str, name: str, path: str, hint: str) -> None:
        if name not in keep and name not in ratchet[rule]:
            ratchet[rule][name] = f"{path}: {hint}"

    for path, text in listSource:
        code = stripCode(text)
        if bannedPattern is not None:
            for match in bannedPattern.finditer(code):
                name = match.group(1)
                if f"name:{name}" not in seen:
                    seen.add(f"name:{name}")
                    listProblem.append(f"{path}: 옛 이름 '{name}' — {bannedNames[name]}")
        listFunction, listSuffix = collectWords(code)
        for word, name in listFunction:
            if name in keep:
                continue
            if word in bannedVerbs:
                record("verb", name, path, f"함수 앞 단어 '{word}'({name}) 는 금지 동사다 — {bannedVerbs[word]}")
            elif word not in allowedFunction and name not in seen:
                seen.add(name)
                listProblem.append(f"{path}: 함수 앞 단어 '{word}'({name}) 가 표에 없다 — 업계 용어와 대조한 뒤 {kConfigPath} 의 _functionWords 에 올린다")
            words = splitWords(name, bFunction=True)
            if len(words) > 1 and words[-1] in ("For", "Of") and words[0] not in lookupWords:
                record("lookup", name, path, f"'{name}' 는 동사 없는 For · Of 게터다 — get · find · findOrAdd · compute 로 시작한다(_lookupWords)")
        for word, name in listSuffix:
            if word not in allowedSuffix and name not in keep and name not in seen:
                seen.add(name)
                hint = bannedTerms.get(word, f"업계 용어와 대조한 뒤 {kConfigPath} 의 _typeSuffixes 에 올린다")
                listProblem.append(f"{path}: 형식 끝 단어 '{word}'({name}) 가 표에 없다 — {hint}")
        for match in kBoolFunction.finditer(code):
            name = match.group(1)
            words = splitWords(name, bFunction=True)
            first = words[0] if words else ""
            if isThirdPersonVerb(first) and first not in predicateWords:
                record("predicate", name, path, f"bool 함수 '{name}' 가 3인칭 문장형('{first}')이다 — is · has · can · should · uses · needs · requires · supports 로(_predicateWords, 언리얼 Is/Has/Uses/Requires…)")
        if bannedTerms:
            for match in kIdentifier.finditer(code):
                name = match.group(0)
                if name in keep or name in ratchet["term"]:
                    continue
                term = findTermWord(name, bannedTerms, keepTerms)
                if term is not None:
                    record("term", name, path, f"'{name}' 에 금지 명사 '{term}' 가 있다 — {bannedTerms[term]}")
    return listProblem, ratchet


def compareBaseline(ratchet: dict[str, dict[str, str]], baseline: dict[str, list[str]]) -> list[str]:
    listProblem: list[str] = []
    for rule in kRatchetRules:
        allowed = set(baseline.get(rule, []))
        for name, where in sorted(ratchet[rule].items()):
            if name not in allowed:
                listProblem.append(f"[{rule}] {where}")
        for name in sorted(allowed - set(ratchet[rule])):
            listProblem.append(f"[{rule}] 기준선의 '{name}' 가 더는 없다 — 고친 이름을 기준선에서 뺀다(`py -3 Scripts/lint/NamingWords.py --lower-baseline`)")
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


def readTable(projectRoot: Path) -> dict:
    return json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))


def readBaseline(projectRoot: Path) -> dict[str, list[str]]:
    path = projectRoot / kBaselinePath
    if path.exists() is False:
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def writeBaseline(projectRoot: Path, baseline: dict[str, list[str]]) -> None:
    data = {"_comment": "NamingWords 기준선 — 지금 남은 위반 이름(규칙마다). 줄기만 한다: 새 위반은 린트가 막고, 고친 이름은 --lower-baseline 으로 뺀다. 영역별 이전이 0 으로 줄인다."}
    for rule in kRatchetRules:
        data[rule] = sorted(set(baseline.get(rule, [])))
    (projectRoot / kBaselinePath).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def checkNamingWords(projectRoot: Path) -> list[str]:
    listProblem, ratchet = classifyNames(readSources(projectRoot), readTable(projectRoot))
    return listProblem + compareBaseline(ratchet, readBaseline(projectRoot))


def runSelfCheck() -> None:
    table = {"_functionWords": ["get", "has", "find", "draws", "material", "set", "focus", "starts"], "_typeSuffixes": ["Component", "Result"], "_banned": {"drain": "flush 또는 poll"},
             "_bannedTerms": {"Verdict": "Result", "Work": "Job"}, "_keepTerms": ["WorkArea"], "_predicateWords": ["is", "has", "uses", "starts"], "_lookupWords": ["get", "find"],
             "_keep": {"MailboxOutcome": "이유"}}
    clean = 'struct FooComponent {}; int getValue( int x ); struct MailboxOutcome {}; // drainEvents() hasWork\nconst char* s = "drainEvents() Verdict"; bool hasValue(); bool setAxisTags(); bool focusPanel(); bool startsWith(); RECT readWorkArea; int getSlotFor( int x );'
    problems, ratchet = classifyNames([("a.h", clean)], table)
    assert problems == [] and all(not names for names in ratchet.values()), (problems, ratchet)
    bad = 'void drainEvents( int x ); struct FooVerdict {}; bool hasPendingWork(); bool drawsBatch(); int materialGroupFor( int x ); int _verdictCount;'
    problems, ratchet = classifyNames([("a.h", bad)], table)
    assert len(problems) == 1 and "'Verdict'" in problems[0], problems   # 형식 끝 단어 Verdict 는 표에 없다
    assert set(ratchet["verb"]) == {"drainEvents"}, ratchet
    assert set(ratchet["term"]) == {"FooVerdict", "hasPendingWork", "_verdictCount"}, ratchet
    assert set(ratchet["predicate"]) == {"drawsBatch"}, ratchet
    assert set(ratchet["lookup"]) == {"materialGroupFor"}, ratchet
    baseline = {"verb": ["drainEvents"], "term": ["FooVerdict", "hasPendingWork", "_verdictCount", "goneName"], "predicate": ["drawsBatch"], "lookup": []}
    ratchetProblems = compareBaseline(ratchet, baseline)
    assert any("materialGroupFor" in line for line in ratchetProblems), ratchetProblems   # 기준선에 없는 새 위반
    assert any("goneName" in line for line in ratchetProblems), ratchetProblems   # 고쳐 사라진 이름은 빼라고 한다
    assert len(ratchetProblems) == 2, ratchetProblems
    tableNames = dict(table, _bannedNames={"CombatHealth": "HealthStateMachine"})
    assert classifyNames([("a.h", "// CombatHealth\nconst char* s = \"CombatHealth\";")], tableNames)[0] == [], "주석 · 문자열의 옛 이름은 본다고 하지 않는다"
    assert len(classifyNames([("a.h", "CombatHealth health; int getCombatHealthRatio();")], tableNames)[0]) == 1, "통째 이름만 막는다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="이름을 업계 용어 표와 대조한다")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--write-baseline", action="store_true", help="지금 위반을 모두 기준선으로 쓴다(처음 한 번 — 새 규칙을 들일 때)")
    parser.add_argument("--lower-baseline", action="store_true", help="고쳐 사라진 이름을 기준선에서 뺀다(늘리지 않는다)")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[NamingWords] self-check OK")
        return ExitCode.Passed
    projectRoot = getProjectRoot()
    if arguments.write_baseline or arguments.lower_baseline:
        _, ratchet = classifyNames(readSources(projectRoot), readTable(projectRoot))
        current = {rule: list(ratchet[rule]) for rule in kRatchetRules}
        if arguments.lower_baseline:
            old = readBaseline(projectRoot)
            current = {rule: [name for name in old.get(rule, []) if name in ratchet[rule]] for rule in kRatchetRules}
        writeBaseline(projectRoot, current)
        print(f"[NamingWords] 기준선: " + ", ".join(f"{rule} {len(current[rule])}" for rule in kRatchetRules))
        return ExitCode.Passed
    listProblem = checkNamingWords(projectRoot)
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[NamingWords] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
