#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
컴파일 DB 의 소스 쪽 경로를 메인 저장소에서 워크트리로 옮기는 자리.

작업자 워크트리에는 빌드 폴더가 없다 — 메인 저장소 빌드 폴더의 컴파일 DB 에서 플래그를 빌려 오되, **워크트리가 제 것으로 가진 경로만 워크트리로 옮긴다**.
"제 것"은 폴더 이름 목록이 아니라 워크트리의 실제 모양으로 정한다: 같은 상대 경로가 워크트리에 있고, 그 경로 위에 링크(심볼릭 링크 · 정션)가 없으면 옮긴다.
생성 헤더가 든 빌드 폴더 · vcpkg · `Tools/LLVM` 같은 링크와 워크트리에 없는 경로(생성물)는 메인의 것을 그대로 쓴다. 저장소에 폴더가 늘어도 목록을 고칠 일이 없다
(`Tools` 가 목록에 없어 `Tools/OnlineLoadBot` 항목이 안 맞던 결함을 이 모양으로 없앴다). 쓰는 곳: `SyntaxCheck`(바꾼 파일 검사) ·
`HeaderSelfContained`(헤더 단독 검사 — 커밋 훅 게이트 · CI 워크플로).
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Sequence

from .Paths import normalizePath

__all__ = ["makeRehomePattern", "rehomeDatabase"]


def makeRehomePattern(mainRoot: Path) -> re.Pattern[str]:
    """
    컴파일 DB 문자열 속 `<메인>/<상대 경로>` 에 맞는다(`\\` · `/` 철자 둘 다, 대소문자 무시). 그룹 `root` 는 `<메인>`, `rel` 은 이어지는 경로다
    (구분자로 시작하고 공백 · 따옴표 · 콜론에서 끝난다). 메인 이름이 더 긴 다른 경로의 접두(`LearningTemplate2`)일 때는 맞지 않는다.
    """
    listPart = [re.escape(part) for part in normalizePath(mainRoot.resolve()).split("/")]
    return re.compile(r"(?P<root>" + r"[\\/]".join(listPart) + r")(?=[\\/\s\"']|$)(?P<rel>(?:[\\/][^\\/\s\"':*?<>|]+)*)", re.IGNORECASE)


def isLinkInternal(path: Path) -> bool:
    """심볼릭 링크 또는 정션이다(워크트리가 메인의 빌드 · 도구 폴더를 거는 모양)."""
    isJunction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(isJunction and isJunction())


def isOwnedByWorktreeInternal(worktreeRoot: Path, relative: str) -> bool:
    """`<워크트리>/<relative>` 가 있고 그 경로의 어느 단계도 링크가 아니다."""
    current = worktreeRoot
    for part in re.split(r"[\\/]", relative):
        if not part:
            continue
        current = current / part
        if isLinkInternal(current):
            return False
    return current != worktreeRoot and current.exists()


def rehomeDatabase(database: Sequence[dict], mainRoot: Path, worktreeRoot: Path) -> list[dict]:
    """
    컴파일 DB 항목의 경로를 메인 저장소에서 워크트리로 옮긴 사본. 같은 저장소이면 그대로다.

    `command` · `arguments` · `file` 만 고친다(`directory` 는 메인의 빌드 폴더 그대로 — 컴파일러의 작업 폴더가 된다).
    경로마다 워크트리에 제 것으로 있는지 한 번만 본다(같은 경로는 수천 번 나온다).
    """
    if mainRoot.resolve() == worktreeRoot.resolve():
        return list(database)
    pattern = makeRehomePattern(mainRoot)
    worktreeResolved = worktreeRoot.resolve()
    worktreePosix = normalizePath(worktreeResolved)
    mapOwned: dict[str, bool] = {}

    def replaceInternal(match: re.Match[str]) -> str:
        relative = match.group("rel")
        if relative not in mapOwned:
            mapOwned[relative] = isOwnedByWorktreeInternal(worktreeResolved, relative)
        if not mapOwned[relative]:
            return match.group(0)
        newRoot = worktreePosix.replace("/", "\\") if "\\" in match.group("root") else worktreePosix
        return newRoot + relative

    def rehomeTextInternal(text: str) -> str:
        return pattern.sub(replaceInternal, text)

    listRehomed: list[dict] = []
    for entry in database:
        mapEntry = dict(entry)
        for key in ("command", "file"):
            if key in mapEntry:
                mapEntry[key] = rehomeTextInternal(str(mapEntry[key]))
        if "arguments" in mapEntry:
            mapEntry["arguments"] = [rehomeTextInternal(str(argument)) for argument in mapEntry["arguments"]]
        listRehomed.append(mapEntry)
    return listRehomed
