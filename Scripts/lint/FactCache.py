#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FactCache.py

여러 파일의 관계를 보는 규칙(층 순환 · 참조 0 · 호출 그래프 · 생성 표 · 에셋 역참조)의 저장소 전체 색인 — 파일마다 뽑은 사실을 내용 해시를 키로
`Saved/Lint/<이름>.facts.json` 에 두고, 해시가 바뀐 파일만 다시 뽑는다. 판정은 늘 저장소 전체 사실로 내리므로 커밋 훅과 `--all` 의 결과가 같다.
언리얼 UBT 의 makefile 캐시(바뀐 것만 다시 훑는다)와 같은 방식이다.

- 키는 git 이 이미 아는 해시다: 작업 트리가 인덱스와 같은 파일은 인덱스 블롭 id(`git ls-files -s`), 다른 파일 · 새 파일만 읽어 sha1 을 낸다.
- 규칙 모듈과 그것이 import 하는 모듈 · 뽑기가 읽는 표 · 이 파일이 바뀌면(뽑는 법이 바뀌었다) 캐시를 통째로 버린다 — 판 번호를 손으로 올리지 않는다.
- 캐시는 파생물이다. 없거나 깨지면 처음부터 다시 뽑는다(정답은 늘 소스다).
"""

from __future__ import annotations

import functools
import hashlib
import json
import os
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence

from lint.LintScope import runGitLines
from lint.ModuleImports import kScriptsDir, listImportedModuleFiles

kCacheFolder = "Saved/Lint"


@functools.cache
def getIndexKeys(projectRoot: Path) -> dict[str, str]:
    """인덱스(스테이지)의 경로 → 블롭 id."""
    mapKey: dict[str, str] = {}
    for entry in runGitLines(projectRoot, ["ls-files", "-s"]):
        head, _, path = entry.partition("\t")
        mapKey[path] = head.split()[1]
    return mapKey


@functools.cache
def getWorkingTreeKeys(projectRoot: Path) -> dict[str, str]:
    """작업 트리의 경로 → 내용 키. 인덱스와 같은 파일은 블롭 id, 다르거나 새 파일은 읽어서 낸 sha1(작업 트리에서 지운 파일은 없다)."""
    mapKey = dict(getIndexKeys(projectRoot))
    setDirty = set(runGitLines(projectRoot, ["diff", "--name-only"])) | set(runGitLines(projectRoot, ["ls-files", "--others", "--exclude-standard"]))
    for path in setDirty:
        try:
            mapKey[path] = "w" + hashlib.sha1((projectRoot / path).read_bytes()).hexdigest()
        except OSError:
            mapKey.pop(path, None)
    return mapKey


def readWorkingTree(projectRoot: Path, listRelative: list[str], errors: str) -> dict[str, str | None]:
    mapText: dict[str, str | None] = {}
    for relative in listRelative:
        try:
            mapText[relative] = (projectRoot / relative).read_text(encoding="utf-8", errors=errors)
        except (UnicodeDecodeError, OSError):
            mapText[relative] = None   # 규칙이 저장소를 걸을 때처럼 건너뛴다
    return mapText


def computeVersion(listVersionFile: Iterable[str]) -> str:
    """뽑는 법을 정하는 파일들의 해시 — Scripts 의 모듈은 그것이 import 하는 모듈까지 넣는다(도우미 모듈이 바뀌어도 다시 뽑는다)."""
    setPath: set[Path] = {Path(__file__).resolve()}
    for file in listVersionFile:
        path = Path(file).resolve()
        if path.suffix == ".py" and kScriptsDir in path.parents:
            moduleName = path.relative_to(kScriptsDir).with_suffix("").as_posix().replace("/", ".")
            setPath.update(kScriptsDir.parent / relative for relative in listImportedModuleFiles(moduleName))
        else:
            setPath.add(path)
    digest = hashlib.sha1()
    for path in sorted(setPath):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def loadFacts(projectRoot: Path, name: str, listVersionFile: Sequence[str], listRelative: Iterable[str], extract: Callable[[str, str], Any], *,
              errors: str = "strict", mapKey: dict[str, str] | None = None,
              readTexts: Callable[[list[str]], dict[str, str | None]] | None = None) -> dict[str, Any]:
    """
    `listRelative` 의 파일마다 `extract(경로, 글)` 의 결과(JSON 으로 쓸 수 있는 값)를 돌려준다. 못 읽은 파일은 빠진다.

    `listVersionFile` 은 뽑는 법을 정하는 파일(부르는 규칙 모듈 `__file__`, 뽑기가 읽는 표) — 하나라도 바뀌면 다시 뽑는다. 기본은 작업 트리 내용이고, 인덱스 내용을 보려면 `mapKey` · `readTexts` 를 함께 넘긴다.
    """
    keys = mapKey if mapKey is not None else getWorkingTreeKeys(projectRoot)
    read = readTexts if readTexts is not None else functools.partial(readWorkingTree, projectRoot, errors=errors)
    cachePath = projectRoot / kCacheFolder / f"{name}.facts.json"
    version = computeVersion(listVersionFile)
    cached: dict[str, list[Any]] = {}
    try:
        document = json.loads(cachePath.read_text(encoding="utf-8"))
        if document.get("version") == version:
            cached = document["files"]
    except (OSError, ValueError, KeyError):
        cached = {}

    listRelative = list(listRelative)
    listStale = [relative for relative in listRelative if relative not in keys or cached.get(relative, [None])[0] != keys[relative]]
    stored: dict[str, list[Any]] = {relative: cached[relative] for relative in listRelative if relative not in listStale}
    if listStale:
        for relative, text in read(listStale).items():
            if text is not None and relative in keys:
                stored[relative] = [keys[relative], extract(relative, text)]
            elif text is not None:
                stored[relative] = ["", extract(relative, text)]   # 키를 모르는 파일(git 밖)은 매번 다시 뽑는다
    if listStale or set(cached) != set(stored):
        cachePath.parent.mkdir(parents=True, exist_ok=True)
        temporary = cachePath.with_suffix(f".{os.getpid()}.tmp")
        temporary.write_text(json.dumps({"version": version, "files": stored}, ensure_ascii=False), encoding="utf-8")
        os.replace(temporary, cachePath)   # 커밋 훅 둘이 겹쳐도 반쯤 쓴 파일을 읽지 않는다
    return {relative: stored[relative][1] for relative in listRelative if relative in stored}
