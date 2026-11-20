#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/acronyms.py

약어 표기 검사 — 식별자 · 파일 · 폴더 이름의 낱말이 약어 목록(`Config/Rules/Lint/NamingWords.json` 의 `_acronyms`)의 약어를 첫 글자만 대문자로 쓰면(`Json` · `Id` · `FourCc`) 막는다.
규칙은 `docs/agents/NamingCpp.md` Acronyms: 약어는 어디서나 모두 대문자, 소문자로 시작하는 첫 낱말일 때만 모두 소문자(`jsonText`). `gv_` 이름 · 주석 · 문자열은 보지 않는다.

**왜 린트인가.** 약어 일괄 변경(1dafbae0c9) 뒤 도구를 지워 새 어긋남(`FourCcUtil` · `HpackDecoder` · `kDxgiFormat…`)이 다시 쌓였다(FolderMoves 11 절) — 이름은 형식이 막지 못한다.
바깥 이름(서드파티 · OS API 를 그대로 쓰는 자리)과 데이터 철자(직렬화 키)는 `_acronymKeep` 에 이름 하나씩 까닭과 함께 둔다.

규칙 id 는 따로 없다 — `naming-words` 가 이 판정을 부른다(같은 이름 검사가 표 둘 · 규칙 둘이던 것을 낱말 표 하나로 합쳤다).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from lint.lint_scope import LintScope, read_text  # noqa: E402

EXTENSIONS = (".h", ".cpp", ".inl")
LITERAL_OR_COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)"|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])+\'', re.S)
IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
# 낱말: 약어 복수(`PSOs` · `IDs`)를 먼저, 그다음 대문자 덩어리 · 첫 대문자 낱말 · 소문자 낱말
WORD = re.compile(r"[A-Z]{2,}s(?![a-z])|[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+")


def find_bad_words(name: str, acronyms: set[str]) -> list[str]:
    """@p name 안에서 약어를 첫 글자만 대문자로 쓴 낱말들."""
    if name.startswith("gv_") or re.search(r"[a-z]", name) is None:
        return []
    list_bad = []
    for part in name.split("_"):
        for word in WORD.findall(part):
            letters = re.sub(r"\d+$", "", word)
            if len(letters) >= 2 and letters[0].isupper() and letters[1:].islower() and letters.upper() in acronyms:
                list_bad.append(word)
    return list_bad


def list_source_files(scope: LintScope) -> list[str]:
    return scope.select(("Source",), EXTENSIONS)


def find_acronym_problems(project_root: Path, scope: LintScope, table: dict) -> list[str]:
    """naming-words 규칙의 약어 판정 — 낱말 표(`table`)의 `_acronyms`(약어) · `_acronymKeep`(바깥 이름 · 데이터 철자 → 까닭)으로 본다."""
    acronyms = {item.upper() for item in table["_acronyms"]}
    keep = table["_acronymKeep"]
    list_problem: list[str] = []
    seen: set[str] = set()
    list_file = list_source_files(scope)
    for relative in list_file:
        # 파일 · 폴더 이름(확장자 앞)
        for segment in Path(relative).with_suffix("").parts[1:]:
            name = segment.split(".")[0]
            if name not in seen and name not in keep and find_bad_words(name, acronyms):
                seen.add(name)
                list_problem.append(f"{relative}: 경로 낱말 '{name}' 의 약어는 모두 대문자({', '.join(find_bad_words(name, acronyms))}) — NamingCpp Acronyms")
        text = read_text(project_root / relative, errors="ignore")
        if text is None:
            continue
        code = LITERAL_OR_COMMENT.sub(" ", text)
        for match in IDENTIFIER.finditer(code):
            name = match.group(0)
            if name in seen or name in keep:
                continue
            list_bad = find_bad_words(name, acronyms)
            if list_bad:
                seen.add(name)
                list_problem.append(f"{relative}: '{name}' 의 약어는 모두 대문자({', '.join(list_bad)}) — 바깥 이름 · 데이터 철자면 Config/Rules/Lint/NamingWords.json 의 _acronymKeep 에 까닭과 함께")
    return list_problem


def check_find_bad_words() -> None:
    acronyms = {"ID", "JSON", "UV", "PSO", "UI"}
    assert find_bad_words("AssetId", acronyms) == ["Id"]
    assert find_bad_words("jsonText", acronyms) == []
    assert find_bad_words("JsonRoundTrip", acronyms) == ["Json"]
    assert find_bad_words("countMaterialPSOs", acronyms) == []
    assert find_bad_words("Uint32", acronyms) == []
    assert find_bad_words("Idle", acronyms) == []
    assert find_bad_words("_pUiSystem", acronyms) == ["Ui"]
    assert find_bad_words("gv_editorUiScale", acronyms) == []
    assert find_bad_words("kArrUv2", acronyms) == ["Uv2"]
