#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/api_diff.py

묶음이 새로 들인 함수 이름(헤더 선언)을 모아 사람이 검토할 목록으로 내고, 두 갈래 이상이 같은 이름을 새로 더했으면 따로 낸다 — 통합 때 `code-review-and-quality` 와 함께 본다.

왜 게이트가 아니라 목록인가. `naming-words` 는 함수의 앞 단어(동사)만 표와 대조한다. `allowsOneMore` · `finishGraceFind` 처럼 동사는 흔하고
이름 전체가 무엇을 하는지 말하지 않는 것은 낱말 표로 가릴 수 없다(`Peak` · `Extra` 는 업계에서도 쓰는 말이라 낱말을 막으면 맞는 이름까지 막힌다,
2026-11-08). 그래서 기계는 새 이름만 빠짐없이 모으고, 판정(언리얼 · 유니티 · Godot 의 같은 일 이름과 대조)은 통합 담당이 한다. 바꾼 옛 이름은
이름 이력 표(`Config/Rules/Lint/NamingWordsBannedNames.json`)의 `_bannedNames` 에 올려 다른 브랜치가 되살리지 못하게 한다.

갈래 사이 겹침. 갈래 여럿이 같은 함수 · 멤버 · 등록 이름(감시 값 · 단계 · 차단 조건 — `SW_…( id, "이름"` 의 이름) · Config 키를 각자 더하면
병합은 글자 충돌 없이 지나가고 빌드 · 기동에서야 겹침이 드러났다(통합 묶음 5 — `RenderStabilitySample` · `EngineConfig::_renderStability`).
멤버 · Config 키는 같은 파일 안에서, 함수 · 등록 이름은 저장소 전체에서 같으면 겹침이다. 겹침이 있으면 실패로 끝난다 — 병합 전에 한 갈래로 모은다.

`py -3 -m Scripts api-diff --base <묶음 시작 커밋> [브랜치 …]` — 브랜치가 없으면 지금 작업 트리를 본다. 묶음 시작 전에도 있던 함수 이름은 뺀다.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from common.git import run_git_or_raise

DECLARATION = re.compile(r"^\+\s*(?:\[\[nodiscard\]\]\s*)?(?:(?:static|virtual|inline|constexpr|explicit|SW_\w+)\s+)*[\w:<>,\s\*&]+?\b([a-z][A-Za-z0-9]*)\s*\([^;{]*\)\s*(?:const\s*)?(?:(?:override|final|noexcept)\s*)*(?:=\s*0\s*)?[;{]")
MEMBER = re.compile(r"^\+\s*(?:(?:static|inline|constexpr|mutable|const)\s+)*[\w:<>,\*&\s]+?[\s\*&>](_[A-Za-z]\w*)\s*(?:\{[^;]*\}|=[^;]*)?;")
REGISTRATION = re.compile(r'^\+(?!\s*#\s*define).*?\bSW_[A-Z0-9_]+\(\s*\w+\s*,\s*"([^"]+)"')
CONFIG_KEY = re.compile(r'^\+\s*"(_\w+)"\s*:')
CALL_LIKE = re.compile(r"\b([a-z][A-Za-z0-9]*)\s*\(")
REMOVED_WORD = re.compile(r'\b_?[A-Za-z]\w*|"[^"]+"')
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "decltype", "alignof", "catch"}
PER_FILE_KIND = {"member", "config"}   # 같은 파일 안에서만 겹친다(다른 형식의 `_count` 는 겹침이 아니다)


@dataclass(frozen=True)
class NewName:
    """diff 가 새로 더한 이름 하나 — 종류(function · member · registration · config), 이름, 처음 보인 파일."""
    _kind: str
    _name: str
    _path: str

    def get_overlap_key(self) -> tuple[str, str, str]:
        return (self._kind, self._name, self._path if self._kind in PER_FILE_KIND else "")


def run_git(project_root: Path, *arguments: str) -> str:
    return run_git_or_raise(arguments, cwd=project_root)


def parse_new_names(diff: str) -> list[NewName]:
    """통합 diff 글에서 더한 이름을 모은다. 지운 줄에 있던 이름(옮기거나 고친 것)은 뺀다 — 함수는 헤더에서 지운 호출 모양만 본다."""
    list_removed: set[str] = set()
    list_removed_function: set[str] = set()
    added: dict[tuple[str, str, str], NewName] = {}
    path = ""
    old_path = ""
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:]
            continue
        if line.startswith("--- "):
            old_path = line[6:]   # 지운 파일은 `+++ /dev/null` 이라 지운 줄의 종류는 옛 경로로 본다
            continue
        if line.startswith("-"):
            list_removed.update(word.strip('"') for word in REMOVED_WORD.findall(line))
            if old_path.endswith(".h"):
                list_removed_function.update(CALL_LIKE.findall(line))
            continue
        if not line.startswith("+"):
            continue
        list_found: list[tuple[str, str]] = []
        if path.endswith(".json"):
            list_found += [("config", match) for match in CONFIG_KEY.findall(line)]
        else:
            list_found += [("registration", match) for match in REGISTRATION.findall(line)]
            if path.endswith(".h"):
                declaration = DECLARATION.match(line)
                if declaration and declaration.group(1) not in KEYWORDS:
                    list_found.append(("function", declaration.group(1)))
                member = MEMBER.match(line)
                if member:
                    list_found.append(("member", member.group(1)))
        for kind, name in list_found:
            new_name = NewName(kind, name, path)
            added.setdefault(new_name.get_overlap_key(), new_name)
    return [new_name for new_name in added.values() if new_name._name not in (list_removed_function if new_name._kind == "function" else list_removed)]


def collect_new_names(project_root: Path, base: str, ref: str | None) -> list[NewName]:
    """@p base 에서 @p ref(없으면 작업 트리)까지 소스 · Config 에 더한 이름."""
    diff_range = [base] if ref is None else [f"{base}...{ref}"]
    return parse_new_names(run_git(project_root, "diff", *diff_range, "--", "Source/*.h", "Source/*.cpp", "Config/*.json"))


def find_overlaps(map_ref_names: dict[str, list[NewName]]) -> dict[tuple[str, str, str], list[tuple[str, str]]]:
    """두 갈래 이상이 새로 더한 같은 이름 → [(갈래, 파일) …]."""
    map_owner: dict[tuple[str, str, str], list[tuple[str, str]]] = {}
    for ref, list_name in map_ref_names.items():
        for new_name in list_name:
            map_owner.setdefault(new_name.get_overlap_key(), []).append((ref, new_name._path))
    return {key: owners for key, owners in map_owner.items() if len({ref for ref, _ in owners}) >= 2}


def collect_existing_names(project_root: Path, base: str) -> set[str]:
    text = run_git(project_root, "grep", "-ohE", r"\b[a-z][A-Za-z0-9]*\s*\(", base, "--", "Source")
    return {re.sub(r"^[^:]*:", "", line).split("(")[0].strip() for line in text.splitlines()}


class ApiDiffCommand(Command):
    name = "api-diff"
    help = "묶음이 새로 들인 함수 이름을 검토 목록으로 내고 갈래 사이 같은 새 이름(함수 · 멤버 · 등록 이름 · Config 키)을 찾는다(--base <묶음 시작> [브랜치 …]) — 통합 검토용"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--base", required=True, help="묶음 시작 커밋")
        parser.add_argument("refs", nargs="*", help="볼 브랜치(없으면 작업 트리)")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        project_root = context.root
        existing = collect_existing_names(project_root, args.base)
        map_ref_names = {ref or "(작업 트리)": collect_new_names(project_root, args.base, ref) for ref in args.refs or [None]}
        list_row = sorted({(ref, new_name._path, new_name._name) for ref, list_name in map_ref_names.items() for new_name in list_name
                           if new_name._kind == "function" and new_name._name not in existing})
        for ref, where, name in list_row:
            print(f"{ref}  {where}  {name}")
        print(f"[api-diff] 새 함수 이름 {len({row[2] for row in list_row})}개 — 업계 이름과 대조해 검토한다")
        overlaps = find_overlaps(map_ref_names)
        for (kind, name, _), owners in sorted(overlaps.items()):
            print(f"[api-diff] 겹침 {kind} '{name}': " + ", ".join(f"{ref} ({where})" for ref, where in owners))
        if overlaps:
            print(f"[api-diff] 갈래 사이 같은 새 이름 {len(overlaps)}개 — 병합 전에 한 갈래로 모은다")
            return ExitCode.VIOLATION
        return ExitCode.PASSED
