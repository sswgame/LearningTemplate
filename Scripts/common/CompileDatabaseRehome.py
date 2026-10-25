#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
컴파일 DB 의 소스 쪽 경로를 메인 저장소에서 워크트리로 옮기는 자리.

작업자 워크트리에는 빌드 폴더가 없다 — 메인 저장소 빌드 폴더의 컴파일 DB 에서 플래그를 빌려 오되 **소스 쪽 경로(`Source` · `Test` ·
`Resource` · `Config`)만 워크트리로 옮긴다**. 생성 헤더가 든 빌드 폴더 · `Tools` · `ThirdParty` · vcpkg 는 메인의 것을 그대로 쓴다
(워크트리에는 링크뿐이다). 쓰는 곳: `SyntaxCheck`(바꾼 파일 검사) · `HeaderSelfContained`(헤더 단독 검사 — 커밋 훅 게이트).
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Sequence

from .Paths import normalizePath

__all__ = ["makeRehomePattern", "rehomeDatabase"]

#: 컴파일 DB 의 경로 가운데 워크트리로 옮기는 메인 쪽 폴더. 나머지(빌드 폴더 · Tools · ThirdParty)는 워크트리에 없거나 링크다.
_kRehomeFolders = ("Source", "Test", "Resource", "Config")


def makeRehomePattern(mainRoot: Path) -> re.Pattern[str]:
    """컴파일 DB 문자열 속 `<메인>/Source` · `Test` · … 의 `<메인>` 부분에 맞는다(`\\` · `/` 철자 둘 다, 대소문자 무시)."""
    listPart = [re.escape(part) for part in normalizePath(mainRoot.resolve()).split("/")]
    folders = "|".join(_kRehomeFolders)
    return re.compile(r"[\\/]".join(listPart) + rf"(?=[\\/](?:{folders})(?:[\\/\s\"']|$))", re.IGNORECASE)


def rehomeDatabase(database: Sequence[dict], mainRoot: Path, worktreeRoot: Path) -> list[dict]:
    """
    컴파일 DB 항목의 소스 쪽 경로를 메인 저장소에서 워크트리로 옮긴 사본. 같은 저장소이면 그대로다.

    `command` · `arguments` · `file` 만 고친다(`directory` 는 메인의 빌드 폴더 그대로 — 컴파일러의 작업 폴더가 된다).
    """
    if mainRoot.resolve() == worktreeRoot.resolve():
        return list(database)
    pattern = makeRehomePattern(mainRoot)
    worktreePosix = normalizePath(worktreeRoot.resolve())

    def rehomeTextInternal(text: str) -> str:
        return pattern.sub(lambda match: worktreePosix.replace("/", "\\") if "\\" in match.group(0) else worktreePosix, text)

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
