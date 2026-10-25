#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
작업자가 **바꾼 파일만** 단일 파일 문법 검사(`-fsyntax-only`)하는 자리 — `py -3 -m Scripts syntax-check` 의 판정 부분.

빌드 폴더가 없는 작업자 워크트리에서도 문법 오류를 통합 전에 잡는 것이 목적이다. 플래그는 메인 저장소 빌드 폴더의 컴파일 DB 에서 빌려 오되
**소스 쪽 경로(`Source` · `Test` · `Resource`)만 워크트리로 옮긴다** — 생성 헤더가 든 빌드 폴더 · `Tools` · `ThirdParty` ·
vcpkg 는 메인의 것을 그대로 쓴다(워크트리에는 링크뿐이다). 헤더 탐침 · 명령줄 떼기 · 이웃 TU 고르기는 `HeaderSelfContained` 를 쓴다.

못 잡는 것: 템플릿 인스턴스화(`-fdelayed-template-parsing` 이라 본문이 쓰이는 곳에서야 검사된다), 링크 오류, 바꾼 헤더가 파급되는 **그 밖의**
TU, 리플렉션 코드젠(빌드 폴더의 `.gen.h` 는 메인 기준이라 어노테이션을 바꾼 헤더는 낡은 산출물을 본다).
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from .HeaderSelfContained import compileSyntaxOnly, findSeedEntry, makeIncludeSpelling, makeSeedIndex
from .Host import runGit
from .Parallel import getProcessWorkerCount, mapConcurrent
from .Paths import normalizePath

__all__ = [
    "SyntaxJob",
    "listChangedSourceFiles",
    "parseDiagnostics",
    "planSyntaxJobs",
    "rehomeDatabase",
    "runSyntaxJob",
    "runSyntaxJobs",
]

#: 바꾼 파일을 찾는 폴더 — 이 밖의 `.h` 는 C++ 이 아닐 수 있다(셰이더 헤더).
_kSyntaxScanRoots = ("Source", "Test")
_kSourceSuffixes = (".cpp", ".cc", ".cxx")
_kHeaderSuffixes = (".h", ".hpp")
#: 컴파일 DB 의 경로 가운데 워크트리로 옮기는 메인 쪽 폴더. 나머지(빌드 폴더 · Tools · ThirdParty)는 워크트리에 없거나 링크다.
_kRehomeFolders = ("Source", "Test", "Resource", "Config")
#: 헤더 하나를 검사하려고 그 헤더를 include 하는 TU 를 몇 개까지 컴파일하는가(헤더 단독 탐침과 별개로).
_kMaxIncluderPerHeader = 2

_kFileLineRe = re.compile(r"^(?P<file>.+?)(?:\((?P<msvcLine>\d+)(?:,\d+)?\)|:(?P<gccLine>\d+)(?::\d+)?): (?:fatal )?error: (?P<message>.+)$")
_kBareErrorRe = re.compile(r"(?:fatal )?error: (.+)")


@dataclass(frozen=True)
class SyntaxJob:
    """컴파일 한 번 — 어느 파일을(source) 어느 TU 의 플래그로(entry) 보는가. `label` 은 보고에 쓰는 이름이다."""

    label: str
    source: Path
    entry: dict


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


def listChangedSourceFiles(root: Path, base: str) -> list[Path]:
    """`base...HEAD` 로 커밋한 것 + 작업 트리(staged · unstaged · untracked)에서 바뀐 C++ 소스 · 헤더. 지금 디스크에 있는 것만."""
    listGitArgument = [["diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD"],
                       ["diff", "--name-only", "--diff-filter=ACMR", "HEAD"],
                       ["ls-files", "--others", "--exclude-standard"]]
    setPath: set[Path] = set()
    for gitArgument in listGitArgument:
        result = runGit(gitArgument, cwd=root)
        if not result.bSucceeded:
            # 목록을 못 얻은 것을 "바뀐 파일 없음" 으로 읽으면 검사 없이 통과한다.
            raise RuntimeError(f"git {' '.join(gitArgument)} 실패: {result.stderr.strip()}")
        for line in result.stdout.splitlines():
            relPath = line.strip()
            path = root / relPath
            if relPath.split("/")[0] in _kSyntaxScanRoots and path.suffix in (*_kSourceSuffixes, *_kHeaderSuffixes) and path.is_file():
                setPath.add(path)
    return sorted(setPath)


def findIncluders(root: Path, headerPath: Path, mapEntryByFile: dict[str, dict]) -> list[Path]:
    """`headerPath` 를 `#include "…"` 하는 소스 가운데 컴파일 DB 에 있는 것을 같은 폴더 먼저, 최대 `_kMaxIncluderPerHeader` 개."""
    pattern = rf'#[[:space:]]*include[[:space:]]+"([^"]*/)?{re.escape(headerPath.name)}"'
    result = runGit(["grep", "-l", "-E", "-e", pattern, "--", *_kSyntaxScanRoots], cwd=root)
    if result.returnCode not in (0, 1):
        raise RuntimeError(f"git grep 실패: {result.stderr.strip()}")
    listCandidate = [root / line.strip() for line in result.stdout.splitlines() if line.strip().endswith(_kSourceSuffixes)]
    listCandidate = [path for path in listCandidate if normalizePath(path.resolve()).lower() in mapEntryByFile]
    listCandidate.sort(key=lambda path: (path.parent != headerPath.parent, normalizePath(path)))
    return listCandidate[:_kMaxIncluderPerHeader]


def makeRelativeLabel(root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return normalizePath(path)


def planSyntaxJobs(root: Path, buildDir: Path, listFile: Sequence[Path], database: Sequence[dict], probeDir: Path
                   ) -> tuple[list[SyntaxJob], list[str]]:
    """
    검사 목록을 컴파일 작업으로 바꾼다 — (작업들, 참고 줄들).

    - 소스: 컴파일 DB 에 항목이 있으면 그 명령으로, 없으면(새 파일) 폴더가 가장 길게 겹치는 이웃 TU 의 명령으로 본다.
    - 헤더: `probeDir` 에 `#include "<헤더>"` 한 줄짜리 탐침을 만들어 혼자 서는지 보고(이웃 TU 플래그), 그 헤더를 include 하는 소스도 본다.
    같은 파일이 두 번 오르면 먼저 오른 작업만 남긴다.
    """
    listSeed = makeSeedIndex(database, buildDir, root)
    mapEntryByFile = {normalizePath((Path(str(entry["directory"])) / str(entry["file"])).resolve()).lower(): entry for entry in database}
    mapJob: dict[str, SyntaxJob] = {}
    listNote: list[str] = []

    def addJobInternal(label: str, source: Path, entry: dict | None) -> None:
        if entry is not None:
            mapJob.setdefault(normalizePath(source).lower(), SyntaxJob(label, source, entry))

    def addSourceInternal(label: str, path: Path) -> None:
        pathKey = normalizePath(path.resolve())
        entry = mapEntryByFile.get(pathKey.lower())
        if entry is None:
            entry = findSeedEntry(pathKey, listSeed)
            if entry is not None:
                listNote.append(f"{label}: 컴파일 DB 에 없어 이웃 TU 의 플래그를 썼다 — {makeRelativeLabel(root, Path(str(entry['file'])))}")
        addJobInternal(label, path, entry)

    for path in listFile:
        label = makeRelativeLabel(root, path)
        if path.suffix in _kSourceSuffixes:
            addSourceInternal(label, path)
            continue
        spelling = makeIncludeSpelling(root, path)
        if spelling is not None:
            probe = probeDir / (spelling.replace("/", "_")[:-2] + "_probe.cpp")
            probe.write_text('#include "%s"\n' % spelling, encoding="utf-8")
            addJobInternal(f"{label} (헤더 단독)", probe, findSeedEntry(normalizePath(path.resolve()), listSeed))
        for includer in findIncluders(root, path, mapEntryByFile):
            addSourceInternal(f"{makeRelativeLabel(root, includer)} ({label} 을 include)", includer)
    return list(mapJob.values()), listNote


def parseDiagnostics(output: str, root: Path | None = None) -> list[str]:
    """컴파일러 출력의 오류 줄 → `파일:줄: 메시지`(위치 없는 오류는 메시지만). 파일은 `root` 아래면 상대 경로로."""
    listDiagnostic: list[str] = []
    for rawLine in output.splitlines():
        line = rawLine.strip()
        matched = _kFileLineRe.match(line)
        if matched:
            fileText = normalizePath(matched["file"])
            if root is not None:
                fileText = makeRelativeLabel(root, Path(matched["file"]))
            listDiagnostic.append(f"{fileText}:{matched['msvcLine'] or matched['gccLine']}: {matched['message'].strip()}")
            continue
        bare = _kBareErrorRe.search(line)
        if bare:
            listDiagnostic.append(bare.group(1).strip())
    return listDiagnostic


def runSyntaxJob(job: SyntaxJob, buildDir: Path, root: Path | None = None) -> list[str]:
    """작업 하나를 컴파일한다. 서면 빈 목록, 못 서면 오류 줄들(오류 줄이 하나도 안 읽히면 출력 끝을 그대로)."""
    bSucceeded, output = compileSyntaxOnly(job.entry, job.source, buildDir)
    if bSucceeded:
        return []
    return parseDiagnostics(output, root) or [f"{job.label}: 컴파일 실패, 오류 줄을 읽지 못했다 — " + " | ".join(output.strip().splitlines()[-3:])]


def runSyntaxJobs(listJob: Sequence[SyntaxJob], buildDir: Path, root: Path | None = None) -> list[tuple[SyntaxJob, list[str]]]:
    """작업들을 동시에 돌려 (작업, 오류 줄들) 을 작업 순서대로 돌려준다."""
    mapDiagnostic = dict(mapConcurrent(lambda pair: (pair[0], runSyntaxJob(pair[1], buildDir, root)), list(enumerate(listJob)),
                                       workerCount=getProcessWorkerCount(len(listJob))))
    return [(job, mapDiagnostic[index]) for index, job in enumerate(listJob)]
