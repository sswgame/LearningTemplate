#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/Acronyms.py

약어 표기 검사 — 식별자 · 파일 · 폴더 이름의 낱말이 약어 목록(`Config/Rules/Lint/Acronyms.json` `_acronyms`)의 약어를 첫 글자만 대문자로 쓰면(`Json` · `Id` · `FourCc`) 막는다.
규칙은 `docs/agents/NamingCpp.md` Acronyms: 약어는 어디서나 모두 대문자, 소문자로 시작하는 첫 낱말일 때만 모두 소문자(`jsonText`). `gv_` 이름 · 주석 · 문자열은 보지 않는다.

**왜 린트인가.** 약어 일괄 변경(1dafbae0c9) 뒤 도구를 지워 새 어긋남(`FourCcUtil` · `HpackDecoder` · `kDxgiFormat…`)이 다시 쌓였다(FolderMoves 11 절) — 이름은 형식이 막지 못한다.
바깥 이름(서드파티 · OS API 를 그대로 쓰는 자리)과 데이터 철자(직렬화 키)는 `_keep` 에 이름 하나씩 까닭과 함께 둔다.

`py -3 -m Scripts lint acronyms [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 스테이지된 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import LintScope, makeWholeScope, readText  # noqa: E402

kConfigPath = "Config/Rules/Lint/Acronyms.json"
kExtensions = (".h", ".cpp", ".inl")
kLiteralOrComment = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)"|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])+\'', re.S)
kIdentifier = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
# 낱말: 약어 복수(`PSOs` · `IDs`)를 먼저, 그다음 대문자 덩어리 · 첫 대문자 낱말 · 소문자 낱말
kWord = re.compile(r"[A-Z]{2,}s(?![a-z])|[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+")


def findBadWords(name: str, acronyms: set[str]) -> list[str]:
    """@p name 안에서 약어를 첫 글자만 대문자로 쓴 낱말들."""
    if name.startswith("gv_") or re.search(r"[a-z]", name) is None:
        return []
    listBad = []
    for part in name.split("_"):
        for word in kWord.findall(part):
            letters = re.sub(r"\d+$", "", word)
            if len(letters) >= 2 and letters[0].isupper() and letters[1:].islower() and letters.upper() in acronyms:
                listBad.append(word)
    return listBad


def readTable(projectRoot: Path) -> tuple[set[str], dict[str, str]]:
    table = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    return {item.upper() for item in table["_acronyms"]}, table.get("_keep", {})


def listSourceFiles(scope: LintScope) -> list[str]:
    return scope.select(("Source",), kExtensions)


def checkAcronyms(projectRoot: Path, scope: LintScope) -> list[str]:
    acronyms, keep = readTable(projectRoot)
    listProblem: list[str] = []
    seen: set[str] = set()
    listFile = listSourceFiles(scope)
    for relative in listFile:
        # 파일 · 폴더 이름(확장자 앞)
        for segment in Path(relative).with_suffix("").parts[1:]:
            name = segment.split(".")[0]
            if name not in seen and name not in keep and findBadWords(name, acronyms):
                seen.add(name)
                listProblem.append(f"{relative}: 경로 낱말 '{name}' 의 약어는 모두 대문자({', '.join(findBadWords(name, acronyms))}) — NamingCpp Acronyms")
        text = readText(projectRoot / relative, errors="ignore")
        if text is None:
            continue
        code = kLiteralOrComment.sub(" ", text)
        for match in kIdentifier.finditer(code):
            name = match.group(0)
            if name in seen or name in keep:
                continue
            listBad = findBadWords(name, acronyms)
            if listBad:
                seen.add(name)
                listProblem.append(f"{relative}: '{name}' 의 약어는 모두 대문자({', '.join(listBad)}) — 바깥 이름 · 데이터 철자면 {kConfigPath} 의 _keep 에 까닭과 함께")
    return listProblem


def runSelfCheck() -> int:
    acronyms = {"ID", "JSON", "UV", "PSO", "UI"}
    assert findBadWords("AssetId", acronyms) == ["Id"]
    assert findBadWords("jsonText", acronyms) == []
    assert findBadWords("JsonRoundTrip", acronyms) == ["Json"]
    assert findBadWords("countMaterialPSOs", acronyms) == []
    assert findBadWords("Uint32", acronyms) == []
    assert findBadWords("Idle", acronyms) == []
    assert findBadWords("_pUiSystem", acronyms) == ["Ui"]
    assert findBadWords("gv_editorUiScale", acronyms) == []
    assert findBadWords("kArrUv2", acronyms) == ["Uv2"]
    print("[Acronyms] self-check OK")
    return ExitCode.Passed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="약어 표기 검사(식별자 · 파일 · 폴더 — Config/Rules/Lint/Acronyms.json)")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args(argv)
    if args.self_check:
        return runSelfCheck()
    listProblem = checkAcronyms(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[Acronyms] {'OK' if not listProblem else f'FAIL {len(listProblem)}건'}")
    return ExitCode.Passed if not listProblem else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
