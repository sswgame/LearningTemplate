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

사용: `py -3 Scripts/dev/rename_identifier.py <쌍 파일.json> [--dry-run] [--reapply]`
쌍 파일: `[{"old": "isCursorCaptured", "new": "shouldShowCursor", "why": "실제 값은 UI 가 커서를 보이길 바란다"}, …]`  (keep-old-name)
- `"scope": "cmake"` — CMake 파일(`*.cmake` · `CMakeLists.txt`)만 바꾼다. CMake 지역 변수처럼 C++ · 파이썬에 같은 철자가 있어도 다른 이름인 것.
  CMake 안의 낙타 이름 · 밑줄 이름은 모두 우리 식별자라 흔한 이름 검사 대신 그 꼴을 본다.
- `"ban": false` — 옛 이름을 `_bannedNames` 에 올리지 않는다(지역 변수: 옛 철자가 다른 뜻으로 다시 쓰일 수 있다).
- 리플렉션 프로퍼티(`PROPERTY(...)` 가 붙은 `_` 멤버)는 데이터에 직렬화 이름(`_bWaitForStability` → `waitForStability`,
  `XMLFields::getAttributeName` 규칙)으로 적힌다 — 그 쌍도 속성 · 요소 · 따옴표 키 자리에서 함께 바꾼다. 같은 직렬화 이름을 다른 멤버도 쓰거나
  흔한 낱말이면 겹친 자리를 알리고 멈춘다. 그 데이터를 손으로 옮긴 뒤 `"serializedName": false` 로 멤버 이름만 바꾼다.
- `--reapply` — 다른 갈래를 병합한 뒤 같은 쌍 표(`Config/Rules/Renames/`)를 다시 돌린다. 새 이름이 이미 있는 것이 정상이라 겹침 검사를 건너뛴다.
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
from common.rename_scope import (  # noqa: E402
    check_rewrite_unmarked_lines, check_serialized_rename, collect_reflected_properties, plan_serialized_rename, replace_serialized_name, rewrite_rename_target,
)

ROOTS = ("Source", "Content", "Config", "docs", "Scripts", "Test", "cmake", ".claude/skills", "AGENTS.md", "CLAUDE.md", "CMakeLists.txt", "CMakePresets.json", "ThirdParty")
TEXT_SUFFIXES = {".h", ".cpp", ".inl", ".hlsl", ".hlsli", ".py", ".md", ".json", ".xml", ".txt", ".cmake", ".material", ".ini", ".toml", ".yaml", ".yml",
                 ".in", ".manifest"}   # .in · .manifest: cmake/Templates
# ThirdParty 는 우리 감싸기(`ThirdParty/<lib>/CMakeLists.txt` — sw_add_vcpkg_package 를 부른다)만 본다. vcpkg 포트(`vcpkg-port/`)는 남의 코드다.
SKIP_PARTS = {"vcpkg-port", "build", ".git", "graphify-out", "__pycache__", "node_modules"}
CMAKE_SCOPE = "cmake"
NAMING_PATH = "Config/Rules/Lint/NamingWords.json"
MIN_LENGTH = 6
WORD_SPLIT = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


def count_words(name: str) -> int:
    return len([part for chunk in name.strip("_").split("_") for part in WORD_SPLIT.findall(chunk)])


def check_pair(old: str, new: str, scope: str | None = None) -> str | None:
    """옮길 수 없는 쌍이면 그 까닭입니다."""
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", old) is None or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new) is None:
        return "식별자가 아니다"
    if old == new:
        return "같은 이름이다"
    if scope == CMAKE_SCOPE:
        # CMake 명령 · 키워드는 모두 소문자 한 낱말이나 대문자다 — 낙타 꼴 · 밑줄 이름만 우리 것이라 낱말 단위 치환이 안전하다
        is_ours = re.search(r"[a-z][A-Z]|_", old) is not None
        return None if is_ours else f"'{old}' 는 CMake 낱말과 겹칠 수 있는 꼴이다 — 손으로 옮긴다"
    if scope is not None:
        return f"모르는 scope '{scope}'"
    member = old.startswith("_")   # 멤버(`_bNot`)는 머리 `_` 가 있어 짧아도 다른 식별자와 겹치지 않는다
    is_common = len(old) < 4 if member else ( len(old) < MIN_LENGTH or count_words(old) < 2 )
    if is_common:
        return f"'{old}' 는 흔한 이름이라 단어 단위 치환이 엉뚱한 식별자를 바꾼다 — 손으로 옮긴다"
    return None


def is_cmake_file(path: Path) -> bool:
    return path.suffix.lower() == ".cmake" or path.name == "CMakeLists.txt"


def is_in_scope(path: Path, pair: dict) -> bool:
    return pair.get("scope") != CMAKE_SCOPE or is_cmake_file(path)


def is_clash_file(path: Path, pair: dict) -> bool:
    """새 이름이 이미 있으면 겹친다고 볼 파일 — C++. CMake 범위(지역 변수)는 보지 않는다: 같은 지역 이름이 함수마다 되풀이되는 것이 정상이라
    파일 단위로는 겹침을 가릴 수 없다 — 쌍 표를 만든 쪽이 스코프마다 확인한다."""
    return pair.get("scope") != CMAKE_SCOPE and path.suffix in (".h", ".cpp", ".inl")


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
    if old not in text:   # 정규식 전에 거른다 — 쌍 수백 개 × 저장소 전체 파일이면 정규식만으로 몇 분이 걸린다
        return text, 0
    pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])")
    return pattern.subn(new, text)


def replace_word_outside_marked_lines(text: str, old: str, new: str, relative_path: str = "") -> tuple[str, int]:
    """`replace_word` 와 같되 옛 이름을 일부러 적은 줄(`keep-old-name` 표지) · 표(금지 이름 표의 키 · 이름 기준선)는 남긴다."""
    count = 0

    def rewrite(part: str) -> str:
        nonlocal count
        changed, part_count = replace_word(part, old, new)
        count += part_count
        return changed

    return rewrite_rename_target(relative_path, text, rewrite), count


def find_files_named(root: Path, stem: str) -> list[Path]:
    """파일 줄기가 @p stem 인 소스 파일(형식 이름 = 파일 이름) — 테스트 파일 `Test<stem>.cpp` 도."""
    list_path: list[Path] = []
    for path in (root / "Source").rglob("*"):
        if path.is_file() and path.stem in (stem, f"Test{stem}") and SKIP_PARTS.isdisjoint(path.relative_to(root).parts):
            list_path.append(path)
    return list_path


def rename_all(root: Path, list_pair: Sequence[dict], dry_run: bool, reapply: bool = False) -> int:
    list_file = list_text_files(root)
    texts = {path: path.read_text(encoding="utf-8", errors="strict") for path in list_file}
    problems = 0
    relative_texts = {path.relative_to(root).as_posix(): text for path, text in texts.items()}
    properties = collect_reflected_properties({relative: text for relative, text in relative_texts.items() if relative.startswith("Source/") and relative.endswith(".h")})
    serialized_pairs: dict[str, tuple[str, str]] = {}   # 옛 멤버 이름 → 데이터에서 함께 바꿀 직렬화 이름 쌍
    for pair in list_pair:
        reason = check_pair(pair["old"], pair["new"], pair.get("scope"))
        if reason is not None:
            print(f"[거절] {pair['old']} → {pair['new']}: {reason}")
            problems += 1
            continue
        if pair.get("scope") is None and pair.get("serializedName", True):
            serialized, list_reason = plan_serialized_rename(properties, relative_texts, pair["old"], pair["new"])
            for reason in list_reason:
                print(f"[거절] {pair['old']} → {pair['new']}: {reason}. 데이터를 손으로 옮긴 뒤 \"serializedName\": false")
            problems += len(list_reason)
            if serialized is not None:
                serialized_pairs[pair["old"]] = serialized
        if reapply:
            continue
        clash = [path for path, text in texts.items() if is_clash_file(path, pair) and replace_word(text, pair["new"], pair["new"])[1] > 0]
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
            if not is_in_scope(path, pair):
                continue
            changed, count = replace_word_outside_marked_lines(texts[path], old, new, path.relative_to(root).as_posix())
            if old in serialized_pairs:
                serialized_count = 0

                def rewrite_serialized(part: str) -> str:
                    nonlocal serialized_count
                    rewritten, part_count = replace_serialized_name(part, *serialized_pairs[old])
                    serialized_count += part_count
                    return rewritten

                changed = rewrite_rename_target(path.relative_to(root).as_posix(), changed, rewrite_serialized)
                count += serialized_count
            if count:
                texts[path] = changed
                touched.append((path.relative_to(root).as_posix(), count))
        data = [entry for entry in touched if entry[0].startswith("Content/")]
        print(f"{old} → {new}: 파일 {len(touched)} · 바꿈 {sum(count for _, count in touched)}" + (f" · 데이터 {len(data)}" if data else "")
              + (" · 직렬화 이름 {} → {}".format(*serialized_pairs[old]) if old in serialized_pairs else ""))
        for path, count in data[:20]:
            print(f"    데이터 {path} ({count})")
        if dry_run or not touched:
            continue
        for path in list_file:
            relative = path.relative_to(root).as_posix()
            if any(relative == entry for entry, _ in touched):
                path.write_text(texts[path], encoding="utf-8", newline="\n")
        for path in find_files_named(root, old) if pair.get("scope") is None else []:
            target = path.with_name(path.name.replace(old, new, 1))
            run_git_or_raise(["mv", str(path), str(target)], cwd=root)
            list_file = [target if item == path else item for item in list_file]
            texts[target] = texts.pop(path, target.read_text(encoding="utf-8"))
            print(f"    파일 {path.relative_to(root).as_posix()} → {target.name}")
        if pair.get("ban", True) is False:
            continue
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
    check_serialized_rename()
    assert check_pair("swText", "text", CMAKE_SCOPE) is None, "CMake 낙타 지역 변수는 짧아도 받는다"
    assert check_pair("target", "goal", CMAKE_SCOPE) is not None, "CMake 소문자 한 낱말은 명령 · 키워드와 겹친다"
    assert is_cmake_file(Path("cmake/A.cmake")) and is_cmake_file(Path("Source/CMakeLists.txt")) and not is_cmake_file(Path("Source/A.cpp"))
    assert not is_in_scope(Path("Source/A.cpp"), {"scope": CMAKE_SCOPE}) and is_in_scope(Path("Source/A.cpp"), {})


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="식별자 이름을 코드 · 데이터 · 문서에서 한 번에 옮긴다")
    parser.add_argument("pairs", nargs="?", help="쌍 파일(JSON 목록: old · new · why)")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--reapply", action="store_true", help="병합 뒤 같은 쌍 표를 다시 돌린다(새 이름이 이미 있어도 옛 이름을 마저 바꾼다)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        self_check()
        print("[RenameIdentifier] self-check OK")
        return 0
    if not arguments.pairs:
        parser.error("쌍 파일을 준다")
    list_pair = json.loads(Path(arguments.pairs).read_text(encoding="utf-8"))
    return 1 if rename_all(get_project_root(), list_pair, arguments.dry_run, arguments.reapply) else 0


if __name__ == "__main__":
    sys.exit(main())
