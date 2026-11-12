#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FactCache.py

여러 파일의 관계를 보는 규칙(층 순환 · 참조 0 · 호출 그래프 · 생성 표 · 에셋 역참조)의 저장소 전체 색인 — 파일마다 뽑은 사실을 내용 해시를 키로
두고, 해시가 바뀐 파일만 다시 뽑는다. 판정은 늘 저장소 전체 사실로 내리므로 커밋 훅과 `--all` 의 결과가 같다. 언리얼 UBT 의 makefile 캐시(바뀐
것만 다시 훑는다)와 같은 방식이다.

저장은 `Saved/Lint/<이름>/` 한 폴더다 — 커밋 훅이 스테이지 파일 수에 비례해 읽게 나눈다:
- `manifest.json`: 판(뽑는 법) · 조각마다 (경로, 내용 키) 목록의 해시 · 요약(`contribute` 토큰의 수). 늘 읽는다 — 지금 키로 낸 조각 해시와 다른 조각만 연다.
- `shard-<xx>.json`: 사실 조각(`shardOf(경로)` 의 sha1 앞 두 자리). 바뀐 파일 · 지운 파일 · 물은 파일(`want`)의 조각만 읽는다.
- 요약: 판정에 저장소 전체 합이 필요한 규칙은 사실 대신 파일마다의 토큰(`contribute`)을 접은 수를 읽는다. 사실이 바뀐 파일만큼 옛 토큰을 빼고 새 토큰을
  더한다(옛 사실은 그 파일의 조각에 있다). 사실 전체를 읽어야만 하는 규칙(호출 그래프)은 `bReadAll=True` 로 부르고, 연 파일 수 검사
  (`PreCommit.checkReadBound`)는 그 저장소만 조각 수를 세지 않는다(`setReadAllStore`).

- 키는 git 이 이미 아는 해시다: 작업 트리가 인덱스와 같은 파일은 인덱스 블롭 id(`git ls-files -s`), 다른 파일 · 새 파일만 읽어 sha1 을 낸다.
- 규칙 모듈과 그것이 import 하는 모듈 · 뽑기가 읽는 표 · 이 파일이 바뀌면(뽑는 법이 바뀌었다) 캐시를 통째로 버린다 — 판 번호를 손으로 올리지 않는다.
- 캐시는 파생물이다. 없거나 깨지면(조각 해시가 manifest 와 다르다 — 쓰다 멈췄다) 처음부터 다시 뽑는다(정답은 늘 소스다).
"""

from __future__ import annotations

import collections
import functools
import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence

from lint.LintScope import readBytes, readText, runGitLines
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
def getDirtyFiles(projectRoot: Path) -> frozenset[str]:
    """작업 트리가 인덱스와 다른 파일과 새 파일 — 이 파일만 내용을 읽어 키를 낸다."""
    return frozenset(runGitLines(projectRoot, ["diff", "--name-only"])) | frozenset(runGitLines(projectRoot, ["ls-files", "--others", "--exclude-standard"]))


@functools.cache
def getWorkingTreeKey(projectRoot: Path, relative: str) -> str | None:
    """작업 트리 파일의 내용 키(없으면 None). 인덱스와 같은 파일은 블롭 id, 다르거나 새 파일은 읽어서 낸 sha1 — 물은 파일만 읽는다(새 파일이 많아도 모두 읽지 않는다)."""
    if relative not in getDirtyFiles(projectRoot):
        return getIndexKeys(projectRoot).get(relative)
    try:
        data = readBytes(projectRoot / relative)
    except OSError:
        return None
    return None if data is None else "w" + hashlib.sha1(data).hexdigest()


def readWorkingTree(projectRoot: Path, listRelative: list[str], errors: str) -> dict[str, str | None]:
    """못 읽는 파일은 None — 규칙이 저장소를 걸을 때처럼 건너뛴다."""
    return {relative: readText(projectRoot / relative, errors) for relative in listRelative}


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


#: `bReadAll=True` 로 사실 전체를 읽은 저장소 이름 — 연 파일 수 검사가 이 저장소의 조각 수를 세지 않는다.
#: 남은 쓰는 곳은 tick-scene-walk 하나다(shader-kill-order 는 요약 + 증분 판정으로 옮겼다) — 그 규칙이 지워지면 이 장치(bReadAll · setReadAllStore)도 지운다.
setReadAllStore: set[str] = set()


def getPathShard(relative: str) -> str:
    return relative


@dataclass
class Facts:
    mapFact: dict[str, Any]                    # 물은 경로(`want`)의 사실, `listRelative` 순
    summary: collections.Counter[str]          # `contribute` 토큰의 수(0 은 없다)


class CacheBroken(Exception):
    """조각과 manifest 가 어긋났다 — 처음부터 다시 뽑는다."""


def getShardName(key: str) -> str:
    return "shard-" + hashlib.sha1(key.encode("utf-8")).hexdigest()[:2] + ".json"


def writeJson(path: Path, value: Any) -> None:
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
    os.replace(temporary, path)   # 커밋 훅 둘이 겹쳐도 반쯤 쓴 파일을 읽지 않는다


def openFacts(projectRoot: Path, name: str, listVersionFile: Sequence[str], listRelative: Iterable[str], extract: Callable[[str, str], Any], *,
              errors: str = "strict", mapKey: dict[str, str] | None = None,
              readTexts: Callable[[list[str]], dict[str, str | None]] | None = None,
              want: Iterable[str] | None = (), bReadAll: bool = False,
              contribute: Callable[[str, Any], Iterable[str]] | None = None,
              shardOf: Callable[[str], str] = getPathShard) -> Facts:
    """
    `listRelative` 의 파일마다 `extract(경로, 글)` 의 결과(JSON 으로 쓸 수 있는 값)를 색인하고, `want` 의 사실과 요약을 돌려준다. 못 읽은 파일은 빠진다.

    - `listVersionFile`: 뽑는 법을 정하는 파일(부르는 규칙 모듈 `__file__`, 뽑기가 읽는 표) — 하나라도 바뀌면 다시 뽑는다.
    - 기본은 작업 트리 내용이고, 인덱스 내용을 보려면 `mapKey` · `readTexts` 를 함께 넘긴다.
    - `want`: 사실을 돌려받을 경로(그 조각만 읽는다). 모두가 필요하면 `want=None` 과 `bReadAll=True`(호출 그래프처럼 접을 수 없는 판정).
    - `contribute(경로, 사실)`: 요약에 더할 토큰 — 판정이 이 수만으로 나면 사실 조각을 읽지 않는다.
    - `shardOf(경로)`: 조각을 고르는 열쇠 — 함께 묻는 파일(같은 폴더 · 같은 이름 짝)이 한 조각에 들게 한다.
    """
    if want is None and not bReadAll:
        raise ValueError(f"[FactCache] {name}: 사실 전체(want=None)는 bReadAll=True 로만 읽는다 — 요약(contribute)이나 물은 경로로 줄인다")
    try:
        return openFactsInternal(projectRoot, name, listVersionFile, list(listRelative), extract, errors, mapKey, readTexts, want, bReadAll, contribute, shardOf, False)
    except CacheBroken:
        return openFactsInternal(projectRoot, name, listVersionFile, list(listRelative), extract, errors, mapKey, readTexts, want, bReadAll, contribute, shardOf, True)


def digestShard(listPathKey: Iterable[tuple[str, str]]) -> str:
    """조각 하나의 (경로, 내용 키) 목록의 해시 — manifest 는 조각마다 이것만 두어, 바뀐 조각만 열어 본다."""
    return hashlib.sha1("\n".join(f"{path}\t{key}" for path, key in sorted(listPathKey)).encode("utf-8")).hexdigest()


def openFactsInternal(projectRoot: Path, name: str, listVersionFile: Sequence[str], listRelative: list[str], extract: Callable[[str, str], Any],
                      errors: str, mapKey: dict[str, str] | None, readTexts: Callable[[list[str]], dict[str, str | None]] | None,
                      want: Iterable[str] | None, bReadAll: bool, contribute: Callable[[str, Any], Iterable[str]] | None,
                      shardOf: Callable[[str], str], bReset: bool) -> Facts:
    if bReadAll:
        setReadAllStore.add(name)
    keys = mapKey if mapKey is not None else {relative: key for relative in listRelative if (key := getWorkingTreeKey(projectRoot, relative)) is not None}
    read = readTexts if readTexts is not None else functools.partial(readWorkingTree, projectRoot, errors=errors)
    folder = projectRoot / kCacheFolder / name
    manifestPath = folder / "manifest.json"
    version = computeVersion(listVersionFile)
    mapOldDigest: dict[str, str] = {}
    summary: collections.Counter[str] = collections.Counter()
    bFresh = True   # 쓸 수 있는 manifest 가 없다(처음 · 판이 바뀜 · 깨짐) — 모두 다시 뽑고 옛 조각은 버린다
    if not bReset:
        try:
            document = json.loads(manifestPath.read_text(encoding="utf-8"))
            if document.get("version") == version:
                mapOldDigest, summary, bFresh = document["shards"], collections.Counter(document["summary"]), False
        except (OSError, ValueError, KeyError):
            pass

    # 지금 조각마다의 경로 — 키를 모르는 파일(git 밖)이 든 조각은 늘 바뀐 것으로 본다(매번 다시 뽑는다).
    mapCurrentPath: dict[str, list[str]] = {}
    for relative in listRelative:
        mapCurrentPath.setdefault(getShardName(shardOf(relative)), []).append(relative)
    mapCurrentDigest = {shardName: digestShard((path, keys[path]) for path in listPath) if all(path in keys for path in listPath) else ""
                        for shardName, listPath in mapCurrentPath.items()}
    setChanged = {shardName for shardName in set(mapCurrentDigest) | set(mapOldDigest)
                  if not mapCurrentDigest.get(shardName) or mapCurrentDigest.get(shardName) != mapOldDigest.get(shardName)}
    setWant = set(listRelative) if want is None else set(listRelative) & set(want)
    mapShard: dict[str, dict[str, list[Any]]] = {}

    def getShard(shardName: str) -> dict[str, list[Any]]:
        if shardName not in mapShard:
            shard: dict[str, list[Any]] = {}
            if not bFresh and shardName in mapOldDigest:
                try:
                    shard = json.loads((folder / shardName).read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    raise CacheBroken(name) from None
                if digestShard((path, entry[0]) for path, entry in shard.items()) != mapOldDigest[shardName]:
                    raise CacheBroken(name)   # 조각과 manifest 가 어긋났다(쓰다 멈췄다)
            mapShard[shardName] = shard
        return mapShard[shardName]

    def addTokens(relative: str, fact: Any, sign: int) -> None:
        for token in contribute(relative, fact) if contribute is not None else ():
            summary[token] += sign
            if summary[token] < 0:
                raise CacheBroken(name)
            if summary[token] == 0:
                del summary[token]

    listStale: list[str] = []
    for shardName in sorted(setChanged):
        shard = getShard(shardName)
        setCurrent = set(mapCurrentPath.get(shardName, ()))
        for path in [path for path in shard if path not in setCurrent]:
            addTokens(path, shard.pop(path)[1], -1)   # 지운 파일
        listStale += [path for path in mapCurrentPath.get(shardName, ()) if path not in keys or shard.get(path, [None])[0] != keys[path]]
    if listStale:
        for relative, text in read(listStale).items():
            shard = getShard(getShardName(shardOf(relative)))
            if relative in shard:
                addTokens(relative, shard.pop(relative)[1], -1)
            if text is None:
                continue   # 못 읽은 파일은 빠진다
            fact = extract(relative, text)
            shard[relative] = [keys.get(relative, ""), fact]
            addTokens(relative, fact, 1)
    mapFact = {relative: entry[1] for relative in listRelative if relative in setWant
               and (entry := getShard(getShardName(shardOf(relative))).get(relative)) is not None}

    if setChanged or bFresh:
        folder.mkdir(parents=True, exist_ok=True)
        if bFresh:
            for stale in folder.glob("shard-*.json"):
                stale.unlink()   # 판이 바뀌었다 — 옛 조각은 쓰지 않는다
        mapNewDigest = {shardName: digest for shardName, digest in mapOldDigest.items() if shardName not in setChanged} if not bFresh else {}
        for shardName in setChanged:
            shard = mapShard[shardName]
            if shard:
                writeJson(folder / shardName, shard)
                mapNewDigest[shardName] = digestShard((path, entry[0]) for path, entry in shard.items())
            else:
                (folder / shardName).unlink(missing_ok=True)
        writeJson(manifestPath, {"version": version, "shards": dict(sorted(mapNewDigest.items())), "summary": dict(summary)})
    return Facts(mapFact, summary)


def loadFacts(projectRoot: Path, name: str, listVersionFile: Sequence[str], listRelative: Iterable[str], extract: Callable[[str, str], Any], **options: Any) -> dict[str, Any]:
    """`openFacts` 의 사실만(`want` 의 경로)."""
    return openFacts(projectRoot, name, listVersionFile, listRelative, extract, **options).mapFact
