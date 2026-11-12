#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/RenameIdentifier.py

식별자 이름을 저장소 전체에서 한 번에 옮긴다 — 코드(Source · Test · Scripts) · 데이터(Content — 리플렉션 칸 · 감시 값 · 개발 명령 이름이 글로 든다) ·
설정(Config) · 문서(docs). 형식 이름이 파일 줄기와 같으면 파일도 옮긴다(`git mv` — include 경로는 같은 치환이 따라간다).
옛 이름은 `Config/Rules/Lint/NamingWords.json` 의 `_bannedNames` 에 올려 다른 갈래 · 붙여 넣은 코드가 되살리지 못하게 하고, 이름 기준선을 줄인다.

**왜 도구인가.** 이름 하나를 손으로 옮기면 데이터 XML 의 칸 이름 · 시나리오의 감시 값 이름 · 문서가 남아, 빌드는 지나는데 데이터 읽기가 조용히 빈 값이 된다.
언리얼은 CoreRedirects 로 옛 이름을 읽어 주지만 이 저장소는 옛 이름을 남기지 않고 데이터까지 한 번에 옮긴다(되돌리는 리다이렉트 표를 쌓지 않는다).

안전: 단어 단위로만 바꾸고, 흔한 이름(단어 하나 · 6 글자 미만)과 새 이름이 이미 있는 경우는 거절한다. `--dry-run` 으로 파일별 횟수를 먼저 본다.

사용: `py -3 Scripts/dev/RenameIdentifier.py <쌍 파일.json> [--dry-run]`
쌍 파일: `[{"old": "shouldShowCursor", "new": "shouldShowCursor", "why": "실제 값은 UI 가 커서를 보이길 바란다"}, …]`
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

from common.Git import runGitOrRaise  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kRoots = ("Source", "Content", "Config", "docs", "Scripts", "Test", ".claude/skills")
kTextSuffixes = {".h", ".cpp", ".inl", ".hlsl", ".hlsli", ".py", ".md", ".json", ".xml", ".txt", ".cmake", ".material", ".ini", ".toml", ".yaml", ".yml"}
kSkipParts = {"ThirdParty", "build", ".git", "graphify-out", "__pycache__", "node_modules"}
kNamingPath = "Config/Rules/Lint/NamingWords.json"
kMinLength = 6
kWordSplit = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z0-9]+")


def countWords(name: str) -> int:
    return len([part for chunk in name.strip("_").split("_") for part in kWordSplit.findall(chunk)])


def checkPair(old: str, new: str) -> str | None:
    """옮길 수 없는 쌍이면 그 까닭입니다."""
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", old) is None or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new) is None:
        return "식별자가 아니다"
    if old == new:
        return "같은 이름이다"
    bMember = old.startswith("_")   # 멤버(`_bNot`)는 머리 `_` 가 있어 짧아도 다른 식별자와 겹치지 않는다
    bCommon = len(old) < 4 if bMember else ( len(old) < kMinLength or countWords(old) < 2 )
    if bCommon:
        return f"'{old}' 는 흔한 이름이라 단어 단위 치환이 엉뚱한 식별자를 바꾼다 — 손으로 옮긴다"
    return None


def listTextFiles(root: Path) -> list[Path]:
    listPath: list[Path] = []
    for base in kRoots:
        folder = root / base
        if folder.exists() is False:
            continue
        for path in folder.rglob("*"):
            if path.is_file() and path.suffix.lower() in kTextSuffixes and kSkipParts.isdisjoint(path.relative_to(root).parts):
                listPath.append(path)
    return listPath


def replaceWord(text: str, old: str, new: str) -> tuple[str, int]:
    pattern = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])")
    return pattern.subn(new, text)


def findFilesNamed(root: Path, stem: str) -> list[Path]:
    """파일 줄기가 @p stem 인 소스 파일(형식 이름 = 파일 이름) — 테스트 파일 `Test<stem>.cpp` 도."""
    listPath: list[Path] = []
    for path in (root / "Source").rglob("*"):
        if path.is_file() and path.stem in (stem, f"Test{stem}") and kSkipParts.isdisjoint(path.relative_to(root).parts):
            listPath.append(path)
    return listPath


def renameAll(root: Path, listPair: Sequence[dict], bDryRun: bool) -> int:
    listFile = listTextFiles(root)
    texts = {path: path.read_text(encoding="utf-8", errors="strict") for path in listFile}
    problems = 0
    for pair in listPair:
        reason = checkPair(pair["old"], pair["new"])
        if reason is not None:
            print(f"[거절] {pair['old']} → {pair['new']}: {reason}")
            problems += 1
            continue
        clash = [path for path, text in texts.items() if path.suffix in (".h", ".cpp", ".inl") and replaceWord(text, pair["new"], pair["new"])[1] > 0]
        if clash and pair.get("allowExisting") is not True:
            print(f"[거절] {pair['new']} 가 이미 있다({clash[0].relative_to(root).as_posix()} …) — 겹치는 이름이면 다른 이름을, 일부러 합치는 것이면 \"allowExisting\": true")
            problems += 1
            continue
    if problems:
        return problems

    stamp = datetime.date.today().isoformat()
    for pair in listPair:
        old, new = pair["old"], pair["new"]
        touched: list[tuple[str, int]] = []
        for path in listFile:
            changed, count = replaceWord(texts[path], old, new)
            if count:
                texts[path] = changed
                touched.append((path.relative_to(root).as_posix(), count))
        data = [entry for entry in touched if entry[0].startswith("Content/")]
        print(f"{old} → {new}: 파일 {len(touched)} · 바꿈 {sum(count for _, count in touched)}" + (f" · 데이터 {len(data)}" if data else ""))
        for path, count in data[:20]:
            print(f"    데이터 {path} ({count})")
        if bDryRun:
            continue
        for path in listFile:
            relative = path.relative_to(root).as_posix()
            if any(relative == entry for entry, _ in touched):
                path.write_text(texts[path], encoding="utf-8", newline="\n")
        for path in findFilesNamed(root, old):
            target = path.with_name(path.name.replace(old, new, 1))
            runGitOrRaise(["mv", str(path), str(target)], cwd=root)
            listFile = [target if item == path else item for item in listFile]
            texts[target] = texts.pop(path, target.read_text(encoding="utf-8"))
            print(f"    파일 {path.relative_to(root).as_posix()} → {target.name}")
        table = json.loads((root / kNamingPath).read_text(encoding="utf-8"))
        table["_bannedNames"][old] = f"{new} — {pair.get('why', '')}({stamp})"
        (root / kNamingPath).write_text(json.dumps(table, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
        texts[root / kNamingPath] = (root / kNamingPath).read_text(encoding="utf-8")
    if bDryRun is False:
        subprocess.run([sys.executable, "Scripts/lint/NamingWords.py", "--lower-baseline"], cwd=root, check=True)
    return 0


def selfCheck() -> None:
    assert checkPair("shouldShowCursor", "shouldShowCursor") is None
    assert checkPair("reads", "uses") is not None, "단어 하나는 거절"
    assert checkPair("When", "Condition") is not None, "흔한 이름은 거절"
    assert checkPair("_bNot", "_bNegate") is None, "멤버 머리 이름은 단어 하나여도 받는다"
    text, count = replaceWord("shouldShowCursor(); bool isCursorCapturedLater; x.shouldShowCursor", "shouldShowCursor", "shouldShowCursor")
    assert count == 2 and "isCursorCapturedLater" in text, (text, count)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="식별자 이름을 코드 · 데이터 · 문서에서 한 번에 옮긴다")
    parser.add_argument("pairs", nargs="?", help="쌍 파일(JSON 목록: old · new · why)")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        selfCheck()
        print("[RenameIdentifier] self-check OK")
        return 0
    if not arguments.pairs:
        parser.error("쌍 파일을 준다")
    listPair = json.loads(Path(arguments.pairs).read_text(encoding="utf-8"))
    return 1 if renameAll(getProjectRoot(), listPair, arguments.dry_run) else 0


if __name__ == "__main__":
    sys.exit(main())
