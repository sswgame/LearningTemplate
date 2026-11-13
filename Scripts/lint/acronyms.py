#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/acronyms.py

약어 표기 검사 — 식별자 · 파일 · 폴더 이름의 낱말이 약어 목록(`Config/Rules/Lint/Acronyms.json` `_acronyms`)의 약어를 첫 글자만 대문자로 쓰면(`Json` · `Id` · `FourCc`) 막는다.
규칙은 `docs/agents/NamingCpp.md` Acronyms: 약어는 어디서나 모두 대문자, 소문자로 시작하는 첫 낱말일 때만 모두 소문자(`jsonText`). `gv_` 이름 · 주석 · 문자열은 보지 않는다.

**왜 린트인가.** 약어 일괄 변경(1dafbae0c9) 뒤 도구를 지워 새 어긋남(`FourCcUtil` · `HpackDecoder` · `kDxgiFormat…`)이 다시 쌓였다(FolderMoves 11 절) — 이름은 형식이 막지 못한다.
바깥 이름(서드파티 · OS API 를 그대로 쓰는 자리)과 데이터 철자(직렬화 키)는 `_keep` 에 이름 하나씩 까닭과 함께 둔다.

`py -3 -m Scripts lint acronyms [--self-check]` 로 돌리고, 커밋 훅(`rules.py` 표)이 스테이지된 파일에 돌린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, make_whole_scope, read_text  # noqa: E402

CONFIG_PATH = "Config/Rules/Lint/Acronyms.json"
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


def read_table(project_root: Path) -> tuple[set[str], dict[str, str]]:
    table = json.loads((project_root / CONFIG_PATH).read_text(encoding="utf-8"))
    return {item.upper() for item in table["_acronyms"]}, table.get("_keep", {})


def list_source_files(scope: LintScope) -> list[str]:
    return scope.select(("Source",), EXTENSIONS)


def check_acronyms(project_root: Path, scope: LintScope) -> list[str]:
    acronyms, keep = read_table(project_root)
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
                list_problem.append(f"{relative}: '{name}' 의 약어는 모두 대문자({', '.join(list_bad)}) — 바깥 이름 · 데이터 철자면 {CONFIG_PATH} 의 _keep 에 까닭과 함께")
    return list_problem


def run_self_check() -> int:
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
    print("[Acronyms] self-check OK")
    return ExitCode.PASSED


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="약어 표기 검사(식별자 · 파일 · 폴더 — Config/Rules/Lint/Acronyms.json)")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args(argv)
    if args.self_check:
        return run_self_check()
    list_problem = check_acronyms(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[Acronyms] {'OK' if not list_problem else f'FAIL {len(list_problem)}건'}")
    return ExitCode.PASSED if not list_problem else ExitCode.VIOLATION


if __name__ == "__main__":
    sys.exit(main())
