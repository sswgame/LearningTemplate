#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/rename_identifier.py

식별자 이름을 저장소 전체에서 한 번에 옮긴다 — 코드(Source · Test · Scripts) · 데이터(Content — 리플렉션 칸 · 감시 값 · 개발 명령 이름이 글로 든다) ·
설정(Config) · 문서(docs). 형식 이름이 파일 줄기와 같으면 파일도 옮긴다(`git mv` — include 경로는 같은 치환이 따라간다).
옛 이름은 `Config/Rules/Lint/NamingWords.json` 의 `_bannedNames` 에 올려 다른 갈래 · 붙여 넣은 코드가 되살리지 못하게 하고, 이름 기준선을 줄인다.

**왜 도구인가.** 이름 하나를 손으로 옮기면 데이터 XML 의 칸 이름 · 시나리오의 감시 값 이름 · 문서가 남아, 빌드는 지나는데 데이터 읽기가 조용히 빈 값이 된다.
언리얼은 CoreRedirects 로 옛 이름을 읽어 주지만 이 저장소는 옛 이름을 남기지 않고 데이터까지 한 번에 옮긴다(되돌리는 리다이렉트 표를 쌓지 않는다).

안전: 단어 단위로만 바꾸고, 흔한 이름(단어 하나 · 6 글자 미만)과 새 이름이 이미 있는 경우는 거절한다. `--dry-run` 으로 파일별 횟수를 먼저 본다.

사용: `py -3 Scripts/dev/rename_identifier.py <쌍 파일.json> [--dry-run]`
쌍 파일: `[{"old": "isCursorCaptured", "new": "shouldShowCursor", "why": "실제 값은 UI 가 커서를 보이길 바란다"}, …]`  (keep-old-name)
"""

from __future__ import annotations

import argparse
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.git import run_git_or_raise  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from common.rename_scope import check_rewrite_unmarked_lines, rewrite_unmarked_lines  # noqa: E402

ROOTS = ("Source", "Content", "Config", "docs", "Scripts", "Test", ".claude/skills", "AGENTS.md", "CLAUDE.md")
TEXT_SUFFIXES = {".h", ".cpp", ".inl", ".hlsl", ".hlsli", ".py", ".md", ".json", ".xml", ".txt", ".cmake", ".material", ".ini", ".toml", ".yaml", ".yml"}
SKIP_PARTS = {"ThirdParty", "build", ".git", "graphify-out", "__pycache__", "node_modules"}
NAMING_PATH = "Config/Rules/Lint/NamingWords.json"
MIN_LENGTH = 6
WORD_SPLIT = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


def count_words(name: str) -> int:
    return len([part for chunk in name.strip("_").split("_") for part in WORD_SPLIT.findall(chunk)])


def check_pair(old: str, new: str) -> str | None:
    """옮길 수 없는 쌍이면 그 까닭입니다."""
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", old) is None or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new) is None:
        return "식별자가 아니다"
    if old == new:
        return "같은 이름이다"
    member = old.startswith("_")   # 멤버(`_bNot`)는 머리 `_` 가 있어 짧아도 다른 식별자와 겹치지 않는다
    is_common = len(old) < 4 if member else ( len(old) < MIN_LENGTH or count_words(old) < 2 )
    if is_common:
        return f"'{old}' 는 흔한 이름이라 단어 단위 치환이 엉뚱한 식별자를 바꾼다 — 손으로 옮긴다"
    return None


def list_text_files(root: Path) -> list[Path]:
    list_path: list[Path] = []
    for base in ROOTS:
        folder = root / base
        if folder.exists() is False:
            continue
        if folder.is_file():
            list_path.append(folder)
            continue
        for path in folder.rglob("*"):
            if path.is_file() and path.suffix.lower() in TEXT_SUFFIXES and SKIP_PARTS.isdisjoint(path.relative_to(root).parts):
                list_path.append(path)
    return list_path


def replace_word(text: str, old: str, new: str) -> tuple[str, int]:
    pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])")
    return pattern.subn(new, text)


def replace_word_outside_marked_lines(text: str, old: str, new: str) -> tuple[str, int]:
    """`replace_word` 와 같되 옛 이름을 일부러 적은 줄(`keep-old-name` 표지)은 남긴다."""
    count = 0

    def rewrite(part: str) -> str:
        nonlocal count
        changed, part_count = replace_word(part, old, new)
        count += part_count
        return changed

    return rewrite_unmarked_lines(text, rewrite), count


def find_files_named(root: Path, stem: str) -> list[Path]:
    """파일 줄기가 @p stem 인 소스 파일(형식 이름 = 파일 이름) — 테스트 파일 `Test<stem>.cpp` 도."""
    list_path: list[Path] = []
    for path in (root / "Source").rglob("*"):
        if path.is_file() and path.stem in (stem, f"Test{stem}") and SKIP_PARTS.isdisjoint(path.relative_to(root).parts):
            list_path.append(path)
    return list_path


def rename_all(root: Path, list_pair: Sequence[dict], dry_run: bool) -> int:
    list_file = list_text_files(root)
    texts = {path: path.read_text(encoding="utf-8", errors="strict") for path in list_file}
    problems = 0
    for pair in list_pair:
        reason = check_pair(pair["old"], pair["new"])
        if reason is not None:
            print(f"[거절] {pair['old']} → {pair['new']}: {reason}")
            problems += 1
            continue
        clash = [path for path, text in texts.items() if path.suffix in (".h", ".cpp", ".inl") and replace_word(text, pair["new"], pair["new"])[1] > 0]
        if clash and pair.get("allowExisting") is not True:
            print(f"[거절] {pair['new']} 가 이미 있다({clash[0].relative_to(root).as_posix()} …) — 겹치는 이름이면 다른 이름을, 일부러 합치는 것이면 \"allowExisting\": true")
            problems += 1
            continue
    if problems:
        return problems

    stamp = datetime.date.today().isoformat()
    for pair in list_pair:
        old, new = pair["old"], pair["new"]
        touched: list[tuple[str, int]] = []
        for path in list_file:
            changed, count = replace_word_outside_marked_lines(texts[path], old, new)
            if count:
                texts[path] = changed
                touched.append((path.relative_to(root).as_posix(), count))
        data = [entry for entry in touched if entry[0].startswith("Content/")]
        print(f"{old} → {new}: 파일 {len(touched)} · 바꿈 {sum(count for _, count in touched)}" + (f" · 데이터 {len(data)}" if data else ""))
        for path, count in data[:20]:
            print(f"    데이터 {path} ({count})")
        if dry_run:
            continue
        for path in list_file:
            relative = path.relative_to(root).as_posix()
            if any(relative == entry for entry, _ in touched):
                path.write_text(texts[path], encoding="utf-8", newline="\n")
        for path in find_files_named(root, old):
            target = path.with_name(path.name.replace(old, new, 1))
            run_git_or_raise(["mv", str(path), str(target)], cwd=root)
            list_file = [target if item == path else item for item in list_file]
            texts[target] = texts.pop(path, target.read_text(encoding="utf-8"))
            print(f"    파일 {path.relative_to(root).as_posix()} → {target.name}")
        table = json.loads((root / NAMING_PATH).read_text(encoding="utf-8"))
        table["_bannedNames"][old] = f"{new} — {pair.get('why', '')}({stamp})"
        (root / NAMING_PATH).write_text(json.dumps(table, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        texts[root / NAMING_PATH] = (root / NAMING_PATH).read_text(encoding="utf-8")
    if dry_run is False:
        subprocess.run([sys.executable, "Scripts/lint/naming_words.py", "--lower-baseline"], cwd=root, check=True)
    return 0


def self_check() -> None:
    # 예시 이름은 이 도구의 치환이 다시 바꾸지 않게 표지를 단다 — 첫 묶음이 이 줄까지 바꿔 old == new 가 되어 자체 확인이 깨져 있었다.
    assert check_pair("isCursorCaptured", "shouldShowCursor") is None  # keep-old-name
    assert check_pair("reads", "uses") is not None, "단어 하나는 거절"
    assert check_pair("When", "Condition") is not None, "흔한 이름은 거절"
    assert check_pair("_bNot", "_bNegate") is None, "멤버 머리 이름은 단어 하나여도 받는다"
    text, count = replace_word("isCursorCaptured(); bool isCursorCapturedLater; x.isCursorCaptured", "isCursorCaptured", "shouldShowCursor")  # keep-old-name
    assert count == 2 and "isCursorCapturedLater" in text, (text, count)
    text, count = replace_word_outside_marked_lines("`oldHelperName`\nnot `oldHelperName` <!-- keep-old-name -->\n", "oldHelperName", "newHelperName")
    assert count == 1 and text == "`newHelperName`\nnot `oldHelperName` <!-- keep-old-name -->\n", (text, count)
    check_rewrite_unmarked_lines()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="식별자 이름을 코드 · 데이터 · 문서에서 한 번에 옮긴다")
    parser.add_argument("pairs", nargs="?", help="쌍 파일(JSON 목록: old · new · why)")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        self_check()
        print("[RenameIdentifier] self-check OK")
        return 0
    if not arguments.pairs:
        parser.error("쌍 파일을 준다")
    list_pair = json.loads(Path(arguments.pairs).read_text(encoding="utf-8"))
    return 1 if rename_all(get_project_root(), list_pair, arguments.dry_run) else 0


if __name__ == "__main__":
    sys.exit(main())
