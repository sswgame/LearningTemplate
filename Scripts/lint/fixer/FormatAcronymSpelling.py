#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatAcronymSpelling.py

약어 철자 코드모드 — Pascal 낱말로 쓴 약어를 대문자로 고칩니다(AGENTS.md "Function names", 규칙 정본은 `Scripts/lint/AcronymRegistry.py`).

`UiSystem` → `UISystem`, `updateUi` → `updateUI`, `_pGpuScene` → `_pGPUScene`, `entityIds` → `entityIDs`. 이름 맨 앞의 소문자 약어(`uiSystem`)는 그대로다.
문자열 리터럴 · 주석 · `#include` 줄 · `gv_` 전역 변수 · `SW_*` 매크로 · 서드파티 이름 · 제품 이름(`ImGui`)은 건드리지 않는다.
서드파티 이름은 등록부의 앞머리 · 이름공간 · 이름 표(`kExternalName`)로만 가린다 — 코드모드가 외부 헤더를 직접 읽으면 기계마다 깔린 SDK 에 따라
같은 트리를 다르게 고친다. 사전 실행(`--report`)이 외부 헤더(`ThirdParty/` · vcpkg 설치 헤더 · Windows SDK · Vulkan SDK)를 훑어 거기에도 있는
이름을 "의심" 으로 내면, 사람이 남의 이름(`GetCurrentProcessId` · DXGI `VendorId`)만 골라 `kExternalName` 에 올린다(`SocketId` 처럼 우리 이름이 겹친 것은 둔다).

모드:
  py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --all [--acronym Ui]        # 식별자를 고쳐 쓴다(--check 면 걸린 파일만 찍는다)
  py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --report [--acronym Ui] [--out <파일>]
        # 사전 실행 — 약어별 바뀔 이름 · 사용 · 파일 수, 파일 이름 변경, 충돌 쌍(UiX 와 UIX 가 둘 다 있는 이름), 외부 이름 의심,
        #   문자열 · 셰이더 · 데이터에도 나오는 이름, 이어 붙은 대문자 약어(규칙 4)
  py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --apply-files [--acronym Ui] [--check]
        # 파일 이름 바꾸기(대소문자만 바뀌면 임시 이름을 거치는 두 단계 git mv) · 파일 이름을 적은 곳(include · CMake · 문서) 치환 ·
        #   데이터 파일(Resource · Config · Test 의 XML · JSON …)의 리플렉션 이름 치환. --check 면 하지 않고 목록만.
  py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --apply-text [--acronym Ui] [--strings] [--check]
        # 코드모드 뒤에 — 주석 · 문서(.md) · CMake · 스크립트의 옛 이름을 새 철자로(새 철자가 코드에 있는 낱말만).
        #   코드의 문자열 리터럴은 목록만 내고 --strings 일 때 고친다. 새 철자가 코드에 없는 낱말은 "남은 철자" 로 알린다.
  py -3 Scripts/lint/fixer/FormatAcronymSpelling.py --rename-folders [--acronym Rpg]
        # 폴더 · 모듈 이름 변경 표(사전 목록) — 옮기지 않는다

한 약어씩 커밋하는 순서는 계획 2절이다: `--apply-files` → `--all` → reconfigure → 컴파일. `--apply-files` 는 데이터 치환에 코드의 이름 목록을 쓰고,
코드가 이미 바뀐 뒤에 돌려도 새 철자가 코드에 있는 이름을 찾아 같은 치환을 한다.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintFixer · AcronymRegistry

import AcronymRegistry as registry  # noqa: E402
from common import (  # noqa: E402
    blankComments,
    collectRepositoryFiles,
    getProjectRoot,
    kLintTargetRelDirs,
    kNotOurCodeDirNames,
    kNotOurDirNames,
    mapConcurrent,
    runProcess,
)
from LintFixer import FixerFileKind, FixPass, LintFixer, addFileArguments, selectFixerTargetFiles  # noqa: E402

#: 이번 실행이 고르는 약어(`--acronym`) — 변환(FixPass)이 읽는다. 조각 시험은 기본값(전체)으로 돈다.
_kState: dict[str, tuple[str, ...]] = {"acronyms": registry.kSelectable}

#: 셰이더 — 고치지 않지만, C++ 과 같은 이름이 셰이더에도 있으면 사전 실행이 알린다(`bindingslots.hlsli` 는 양쪽이 include 한다).
_kShaderSuffix: tuple[str, ...] = (".hlsl", ".hlsli")

#: 파일 이름을 글로 적는 파일 — `--apply-files` 가 옛 파일 이름을 새 이름으로 바꾼다.
_kPathTextSuffix: tuple[str, ...] = (*registry.kCodeSuffix, ".cmake", ".md", ".json", ".py", ".txt", ".natvis", ".xml", ".yml", ".in")

#: 외부 헤더 훑기 결과를 담아 두는 파일(빌드 폴더 — 커밋하지 않는다).
_kExternalCacheRel = "build/AcronymSpelling/ExternalNames.txt"


def transformTypeNamesInternal(text: str) -> tuple[str, bool]:
    """대문자로 시작하는 이름(타입 · 이름공간 · 열거형)."""
    newText, mapRename = registry.respellCode(text, _kState["acronyms"], bValueNames=False)
    return newText, bool(mapRename)


def transformValueNamesInternal(text: str) -> tuple[str, bool]:
    """소문자 · `_` 로 시작하는 이름(함수 · 변수 · 멤버)."""
    newText, mapRename = registry.respellCode(text, _kState["acronyms"], bTypeNames=False)
    return newText, bool(mapRename)


# --- 외부 이름 ------------------------------------------------------------------


def findExternalHeaderRoots(repositoryRoot: Path) -> list[Path]:
    """남의 헤더가 있는 폴더 — 저장소 `ThirdParty/` · vcpkg 설치 헤더 · Windows SDK(um · shared · ucrt) · Vulkan SDK."""
    listRoot = [repositoryRoot / "ThirdParty"]
    listRoot += sorted(repositoryRoot.glob("build/vcpkg_installed/*/include"))   # vcpkg 설치 헤더(트리플릿마다)
    sdkInclude = Path(os.environ.get("WindowsSdkDir", r"C:\Program Files (x86)\Windows Kits\10")) / "Include"
    if sdkInclude.is_dir():
        listVersion = sorted(path for path in sdkInclude.iterdir() if path.is_dir() and path.name[:1].isdigit())
        if listVersion:
            listRoot += [listVersion[-1] / name for name in ("um", "shared", "ucrt")]
    if os.environ.get("VULKAN_SDK"):
        listRoot.append(Path(os.environ["VULKAN_SDK"]) / "Include")
    return [root for root in listRoot if root.is_dir()]


def scanExternalNamesInternal(path: Path) -> set[str]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return set()
    return {site.name for site in registry.iterateNameSites(text, registry.kAcronym, masked=text)}


def loadExternalNames(repositoryRoot: Path, bRefresh: bool = False) -> frozenset[str]:
    """
    외부 헤더에 나오는 이름 가운데 약어 규칙에 걸리는 것(사전 실행의 의심 목록 — 고치는 판정에는 쓰지 않는다).
    훑기는 수십 초라 빌드 폴더에 담아 두고, 뿌리 목록 · 파일 수 · 최신 수정 시각이 같으면 다시 쓴다.
    """
    listRoot = findExternalHeaderRoots(repositoryRoot)
    listFile = [path for root in listRoot for path in root.rglob("*")
                if path.suffix.lower() in (".h", ".hpp", ".hh", ".inl", ".hxx") and path.is_file()]
    stamp = f"{'|'.join(root.as_posix() for root in listRoot)}|{len(listFile)}|{max((p.stat().st_mtime_ns for p in listFile), default=0)}"
    cachePath = repositoryRoot / _kExternalCacheRel
    if not bRefresh and cachePath.is_file():
        lines = cachePath.read_text(encoding="utf-8").splitlines()
        if lines and lines[0] == stamp:
            return frozenset(lines[1:])
    setName: set[str] = set()
    for found in mapConcurrent(scanExternalNamesInternal, listFile):
        setName |= found
    cachePath.parent.mkdir(parents=True, exist_ok=True)
    cachePath.write_text("\n".join([stamp, *sorted(setName)]) + "\n", encoding="utf-8")
    print(f"[AcronymSpelling] 외부 헤더 {len(listFile)}개에서 약어 모양 이름 {len(setName)}개 — {cachePath}", file=sys.stderr)
    return frozenset(setName)


# --- 코드 훑기 ------------------------------------------------------------------


def collectCodeFiles(repositoryRoot: Path) -> list[Path]:
    """코드모드가 보는 C++ 파일 — `kLintTargetRelDirs` 아래, 서드파티 · 빌드 산출물 밖."""
    return collectRepositoryFiles(repositoryRoot, kLintTargetRelDirs, suffixes=registry.kCodeSuffix, excludedDirNames=kNotOurCodeDirNames)


@dataclass
class FileScan:
    """파일 하나를 훑은 결과."""

    relPath: str
    counterRename: Counter = field(default_factory=Counter)      # 옛 이름 → 이 파일에서 바뀌는 횟수
    setIdentifier: set[str] = field(default_factory=set)         # 이 파일의 모든 이름(충돌 판정)
    setLiteralToken: set[str] = field(default_factory=set)       # 문자열 리터럴 안의 이름 모양 낱말
    setExternalSkip: set[str] = field(default_factory=set)       # 남의 이름이라 비켜 간 것
    mapRename: dict[str, str] = field(default_factory=dict)


_kStringLiteralRe = re.compile(r'"(?:\\.|[^"\\\n])*"')
_kWordRe = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def scanCodeFile(path: Path, repositoryRoot: Path, acronyms: tuple[str, ...]) -> FileScan:
    text = path.read_text(encoding="utf-8", errors="replace")
    result = FileScan(path.relative_to(repositoryRoot).as_posix())
    masked = registry.maskCode(text)
    result.setIdentifier = set(_kWordRe.findall(masked))
    for literal in _kStringLiteralRe.findall(blankComments(text)):
        result.setLiteralToken.update(_kWordRe.findall(literal))
    for site in registry.iterateNameSites(text, acronyms, masked=masked):
        newName = registry.respellName(site.name, acronyms)
        if newName == site.name:
            continue
        if registry.isExternalName(site.name, site.qualifier):
            result.setExternalSkip.add(site.name)
            continue
        result.counterRename[site.name] += 1
        result.mapRename[site.name] = newName
    return result


def scanTree(repositoryRoot: Path, acronyms: tuple[str, ...]) -> list[FileScan]:
    listFile = collectCodeFiles(repositoryRoot)
    return list(mapConcurrent(lambda path: scanCodeFile(path, repositoryRoot, acronyms), listFile))


def collectFileRenames(repositoryRoot: Path, acronyms: tuple[str, ...]) -> list[tuple[str, str]]:
    """이름이 바뀌는 코드 파일(저장소 기준 옛 경로, 새 경로). 폴더는 바꾸지 않는다(`--rename-folders`)."""
    listRename: list[tuple[str, str]] = []
    for path in collectCodeFiles(repositoryRoot):
        newName = registry.respellPathPart(path.name, acronyms)
        if newName != path.name:
            relPath = path.relative_to(repositoryRoot).as_posix()
            listRename.append((relPath, relPath.rpartition("/")[0] + "/" + newName))
    return listRename


def collectDataFiles(repositoryRoot: Path) -> list[Path]:
    return collectRepositoryFiles(repositoryRoot, registry.kDataRelDir, suffixes=registry.kDataSuffix, excludedDirNames=kNotOurCodeDirNames)


def buildDataRenameMap(listScan: list[FileScan], acronyms: tuple[str, ...]) -> tuple[dict[str, str], set[str]]:
    """(코드에서 바뀌는 이름 표, 코드의 모든 이름). 데이터 치환은 이 둘로 판정한다(`dataTokenRename`)."""
    mapRename: dict[str, str] = {}
    setIdentifier: set[str] = set()
    for scan in listScan:
        mapRename.update(scan.mapRename)
        setIdentifier |= scan.setIdentifier
    return mapRename, setIdentifier


def dataTokenRename(token: str, mapRename: dict[str, str], setIdentifier: set[str], acronyms: tuple[str, ...]) -> str:
    """데이터 낱말 하나의 새 철자 — 코드에서 바뀌는 이름이거나, 새 철자가 이미 코드에 있는 이름(코드를 먼저 바꾼 경우)일 때만."""
    if token in mapRename:
        return mapRename[token]
    newToken = registry.respellName(token, acronyms)
    if newToken != token and newToken in setIdentifier:
        return newToken
    return token


# --- 보고 ----------------------------------------------------------------------


def buildReport(repositoryRoot: Path, acronyms: tuple[str, ...], externalNames: frozenset[str]) -> tuple[list[str], list[str]]:
    """(요약 줄들, 상세 줄들)."""
    listScan = scanTree(repositoryRoot, acronyms)
    mapRename, setIdentifier = buildDataRenameMap(listScan, acronyms)
    counterUse: Counter = Counter()
    mapFiles: dict[str, set[str]] = defaultdict(set)
    setLiteral: set[str] = set()
    setExternal: set[str] = set()
    for scan in listScan:
        counterUse.update(scan.counterRename)
        for name in scan.counterRename:
            mapFiles[name].add(scan.relPath)
        setLiteral |= scan.setLiteralToken
        setExternal |= scan.setExternalSkip

    # 충돌: 새 철자가 이미 다른 이름으로 있거나, 옛 이름 둘이 같은 새 철자가 된다.
    mapByNew: dict[str, list[str]] = defaultdict(list)
    for old, new in mapRename.items():
        mapByNew[new].append(old)
    listConflict = sorted({f"{old} → {new} (이미 있음)" for old, new in mapRename.items() if new in setIdentifier}
                          | {f"{' · '.join(sorted(olds))} → {new} (둘이 합쳐짐)" for new, olds in mapByNew.items() if len(olds) > 1})
    listAdjacent = sorted({f"{new} ({' '.join(registry.findAdjacentAcronyms(new))})" for new in mapRename.values()
                           if registry.findAdjacentAcronyms(new)})
    listLiteral = sorted(name for name in mapRename if name in setLiteral)
    listExternalSuspect = sorted(name for name in mapRename if name in externalNames)
    listFileRename = collectFileRenames(repositoryRoot, acronyms)

    # 셰이더 · 데이터
    counterShader: Counter = Counter()
    for path in collectRepositoryFiles(repositoryRoot, ("Source", "Resource", "Test"), suffixes=_kShaderSuffix, excludedDirNames=kNotOurCodeDirNames):
        text = path.read_text(encoding="utf-8", errors="replace")
        for site in registry.iterateNameSites(text, acronyms):
            if registry.respellName(site.name, acronyms) != site.name:
                counterShader[site.name] += 1
    counterData: Counter = Counter()
    mapDataFiles: dict[str, set[str]] = defaultdict(set)
    for path in collectDataFiles(repositoryRoot):
        text = path.read_text(encoding="utf-8", errors="replace")
        for token in _kWordRe.findall(text):
            if dataTokenRename(token, mapRename, setIdentifier, acronyms) != token:
                counterData[token] += 1
                mapDataFiles[token].add(path.relative_to(repositoryRoot).as_posix())

    listSummary = ["| 약어 | 바뀔 이름 | 사용 | 파일 | 파일 이름 | 데이터 사용 · 파일 | 충돌 |", "|---|---|---|---|---|---|---|"]
    for acronym in acronyms:
        listName = [name for name in mapRename if acronym in registry.changedAcronyms(name, acronyms)]
        if not listName:
            continue
        uses = sum(counterUse[name] for name in listName)
        files = set().union(*(mapFiles[name] for name in listName))
        renames = sum(1 for old, new in listFileRename
                      if registry.respellPathPart(old.rpartition("/")[2], (acronym,)) != old.rpartition("/")[2])
        dataUses = sum(counterData[name] for name in listName)
        dataFiles = set().union(*(mapDataFiles[name] for name in listName)) if listName else set()
        conflicts = sum(1 for line in listConflict if any(line.startswith(name + " ") or f" {name} " in f" {line} " for name in listName))
        listSummary.append(f"| {registry.toPascal(acronym)} | {len(listName):,} | {uses:,} | {len(files):,} | {renames} | "
                           f"{dataUses:,} · {len(dataFiles)} | {conflicts} |")
    allFiles = set().union(*mapFiles.values()) if mapFiles else set()
    listSummary.append(f"| 합계(이름 하나에 약어 둘이면 한 번) | {len(mapRename):,} | {sum(counterUse.values()):,} | {len(allFiles):,} | "
                       f"{len(listFileRename)} | {sum(counterData.values()):,} · {len(set().union(*mapDataFiles.values())) if mapDataFiles else 0} | "
                       f"{len(listConflict)} |")
    listSummary += ["", f"충돌 쌍 {len(listConflict)} · 외부 이름이라 비켜 간 것 {len(setExternal)} · 외부 헤더에도 있는 이름(의심) {len(listExternalSuspect)} · 문자열 리터럴에도 나오는 이름 {len(listLiteral)} · "
                    f"셰이더에도 나오는 이름 {len(counterShader)}(셰이더는 고치지 않는다) · 이어 붙은 대문자 약어 {len(listAdjacent)}"]

    listDetail = ["# 충돌 쌍", *listConflict, "", "# 외부 이름이라 비켜 간 것(등록부 앞머리 · 이름공간 · 이름 표)", *sorted(setExternal), "",
                  "# 외부 헤더에도 있는 이름(의심 — 남의 이름이면 kExternalName 에 올린다, 우리 이름이 겹친 것이면 둔다)", *listExternalSuspect, "",
                  "# 문자열 리터럴에도 나오는 이름(문자열로 찾는 이름 — 내보내기 심볼 · 리플렉션 · 로그)", *listLiteral, "",
                  "# 셰이더에도 나오는 이름", *(f"{name} {count}" for name, count in sorted(counterShader.items())), "",
                  "# 이어 붙은 대문자 약어(규칙 4)", *listAdjacent, "",
                  "# 파일 이름", *(f"{old} → {new}" for old, new in listFileRename), "",
                  "# 데이터 파일 치환", *(f"{name} → {dataTokenRename(name, mapRename, setIdentifier, acronyms)} {count} ({len(mapDataFiles[name])} 파일)"
                                       for name, count in sorted(counterData.items())), "",
                  "# 이름 바꾸기(옛 이름 → 새 이름 · 사용 · 파일)",
                  *(f"{old} → {new} {counterUse[old]} {len(mapFiles[old])}" for old, new in sorted(mapRename.items()))]
    return listSummary, listDetail


# --- 파일 · 데이터 적용 ----------------------------------------------------------


def runGitInternal(repositoryRoot: Path, *arguments: str) -> None:
    result = runProcess(["git", *arguments], cwd=repositoryRoot)
    if result.returnCode != 0:
        raise RuntimeError(f"git {' '.join(arguments)} 실패: {result.stderr.strip() or result.stdout.strip()}")


def moveFileInternal(repositoryRoot: Path, oldRel: str, newRel: str) -> None:
    """`git mv` — 대소문자만 바뀌면 Windows 가 같은 파일로 보므로 임시 이름을 거친다."""
    if oldRel.lower() == newRel.lower():
        temporaryRel = newRel + ".acronym-tmp"
        runGitInternal(repositoryRoot, "mv", oldRel, temporaryRel)
        runGitInternal(repositoryRoot, "mv", temporaryRel, newRel)
    else:
        runGitInternal(repositoryRoot, "mv", oldRel, newRel)


def replaceWordsInternal(text: str, mapReplace: dict[str, str], wordRe: re.Pattern[str]) -> tuple[str, int]:
    count = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal count
        found = match.group(0)
        if found in mapReplace:
            count += 1
            return mapReplace[found]
        return found

    return wordRe.sub(replace, text), count


def readTextInternal(path: Path) -> str | None:
    try:
        with path.open("r", encoding="utf-8", errors="strict", newline="") as file:
            return file.read()
    except (OSError, UnicodeDecodeError):
        return None


def writeTextInternal(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="") as file:
        file.write(text)


def applyFiles(repositoryRoot: Path, acronyms: tuple[str, ...], bDryRun: bool) -> list[str]:
    """파일 이름 바꾸기 · 파일 이름을 적은 곳 치환 · 데이터 파일 치환. 한 일(또는 할 일)의 줄들을 돌려준다."""
    listLine: list[str] = []
    listScan = scanTree(repositoryRoot, acronyms)
    mapRename, setIdentifier = buildDataRenameMap(listScan, acronyms)
    listFileRename = collectFileRenames(repositoryRoot, acronyms)

    # 1) 파일 이름을 적은 곳 — include · CMake · 문서. 옛 파일 이름(확장자 포함) 낱말을 새 이름으로.
    mapBaseName = {old.rpartition("/")[2]: new.rpartition("/")[2] for old, new in listFileRename}
    if mapBaseName:
        baseNameRe = re.compile(r"(?<![A-Za-z0-9_.])(?:" + "|".join(re.escape(name) for name in sorted(mapBaseName, key=len, reverse=True))
                                + r")(?![A-Za-z0-9_])")
        listTextFile = collectRepositoryFiles(repositoryRoot, ("",), suffixes=_kPathTextSuffix, fileNames=("CMakeLists.txt",),
                                              excludedDirNames=kNotOurCodeDirNames)
        for path in listTextFile:
            text = readTextInternal(path)
            if text is None:
                continue
            newText, count = replaceWordsInternal(text, mapBaseName, baseNameRe)
            if count:
                listLine.append(f"파일 이름 치환 {count}곳: {path.relative_to(repositoryRoot).as_posix()}")
                if not bDryRun:
                    writeTextInternal(path, newText)

    # 2) 데이터 — 리플렉션 타입 · 속성 이름.
    for path in collectDataFiles(repositoryRoot):
        text = readTextInternal(path)
        if text is None:
            continue
        mapToken = {token: dataTokenRename(token, mapRename, setIdentifier, acronyms) for token in set(_kWordRe.findall(text))}
        mapToken = {old: new for old, new in mapToken.items() if old != new}
        if not mapToken:
            continue
        newText, count = replaceWordsInternal(text, mapToken, _kWordRe)
        listLine.append(f"데이터 치환 {count}곳: {path.relative_to(repositoryRoot).as_posix()} ({', '.join(sorted(mapToken))})")
        if not bDryRun:
            writeTextInternal(path, newText)

    # 3) 파일 이름 — 글을 고친 뒤에 옮긴다(옮긴 파일도 1) 에서 이미 고쳐졌다).
    for oldRel, newRel in listFileRename:
        listLine.append(f"git mv {oldRel} → {newRel}" + (" (대소문자만 — 두 단계)" if oldRel.lower() == newRel.lower() else ""))
        if not bDryRun:
            moveFileInternal(repositoryRoot, oldRel, newRel)
    return listLine


# --- 글 적용(주석 · 문자열 · 문서) ---------------------------------------------------

#: `--apply-text` 가 고치는 글 파일(코드 파일은 주석 · 문자열만). 셰이더는 이름을 문자열로 묶어 쓰므로 고치지 않는다(`--report` 가 알린다).
_kProseSuffix: tuple[str, ...] = (".md", ".cmake", ".py", ".txt", ".natvis", ".yml", ".yaml", ".in", ".json", ".xml", ".ps1", ".toml")

#: 옛 철자를 일부러 적는 파일 — 규칙 · 도구 · 시험 · 계획 문서.
_kTextExcludedRel: frozenset[str] = frozenset({
    "Scripts/lint/AcronymRegistry.py", "Scripts/lint/fixer/FormatAcronymSpelling.py", "Scripts/lint/gate/CheckAcronymSpelling.py",
    "Test/PythonTest/TestAcronymSpelling.py", "docs/04_CodingGuidelines.md",
    "Scripts/lint/gate/CheckFunctionVocabulary.py",
    "Scripts/lint/rules/AcronymRegistry.toml", "Scripts/lint/rules/CheckFunctionVocabulary.toml", "Scripts/lint/rules/CheckProductNames.toml",
})

_kIncludeLineRe = re.compile(r"^[ \t]*#[ \t]*include\b")


@dataclass
class TextEdit:
    """글 한 자리 — `kind` 는 `comment` · `string` · `prose` · `leftover`(새 철자가 코드에 없어 그대로 둔 것)."""

    relPath: str
    line: int
    kind: str
    old: str
    new: str


def rewriteText(relPath: str, text: str, mapRename: dict[str, str], setIdentifier: set[str], acronyms: tuple[str, ...],
                bCode: bool, bStrings: bool) -> tuple[str, list[TextEdit]]:
    """
    글 하나의 옛 이름을 새 철자로 — (새 글, 자리 목록). 판정은 데이터와 같다(`dataTokenRename` — 코드에서 바뀌는 이름이거나 새 철자가 코드에 있는 이름).
    코드 파일은 식별자(코드모드 몫)와 `#include` 줄(`--apply-files` 몫)을 건너뛰고 주석 · 문자열만 본다. 문자열은 `bStrings` 일 때만 고친다.
    """
    masked = registry.maskCode(text) if bCode else ""
    commentBlanked = blankComments(text) if bCode else ""
    listEdit: list[TextEdit] = []
    pieces: list[str] = []
    cursor = 0
    for match in _kWordRe.finditer(text):
        token = match.group(0)
        if registry.respellName(token, acronyms) == token or registry.isExternalName(token):
            continue
        start = match.start()
        kind = "prose"
        if bCode:
            if masked[start] != " " or _kIncludeLineRe.match(text, text.rfind("\n", 0, start) + 1):
                continue
            kind = "comment" if commentBlanked[start] == " " else "string"
        newToken = dataTokenRename(token, mapRename, setIdentifier, acronyms)
        line = text.count("\n", 0, start) + 1
        if newToken == token:
            listEdit.append(TextEdit(relPath, line, "leftover", token, registry.respellName(token, acronyms)))
            continue
        listEdit.append(TextEdit(relPath, line, kind, token, newToken))
        if kind != "string" or bStrings:
            pieces += [text[cursor:start], newToken]
            cursor = match.end()
    pieces.append(text[cursor:])
    return "".join(pieces), listEdit


def applyText(repositoryRoot: Path, acronyms: tuple[str, ...], bDryRun: bool, bStrings: bool) -> list[TextEdit]:
    """주석 · 문서 · 스크립트의 옛 이름을 새 철자로. 바꾼(또는 남긴) 자리를 돌려준다."""
    listScan = scanTree(repositoryRoot, acronyms)
    mapRename, setIdentifier = buildDataRenameMap(listScan, acronyms)
    listPath = collectRepositoryFiles(repositoryRoot, ("",), suffixes=(*registry.kCodeSuffix, *_kProseSuffix), fileNames=("CMakeLists.txt",),
                                      excludedDirNames=kNotOurCodeDirNames)
    listAll: list[TextEdit] = []
    for path in listPath:
        relPath = path.relative_to(repositoryRoot).as_posix()
        if relPath in _kTextExcludedRel:
            continue
        text = readTextInternal(path)
        if text is None:
            continue
        newText, listEdit = rewriteText(relPath, text, mapRename, setIdentifier, acronyms, path.suffix.lower() in registry.kCodeSuffix, bStrings)
        listAll += listEdit
        if not bDryRun and newText != text:
            writeTextInternal(path, newText)
    return listAll


def formatTextEdits(listEdit: list[TextEdit], bStrings: bool) -> list[str]:
    """요약 줄 + (고치지 않은) 문자열 · 남은 철자 목록."""
    counter = Counter(edit.kind for edit in listEdit)
    listLine = [f"주석 {counter['comment']} · 문서 {counter['prose']} · 문자열 {counter['string']}"
                f"({'고침' if bStrings else '고치지 않음 — 아래 목록, --strings 로 고친다'}) · 남은 철자 {counter['leftover']}"]
    for kind in (() if bStrings else ("string",)) + ("leftover",):
        listLine += [f"{kind} {edit.relPath}:{edit.line}: {edit.old} → {edit.new}" for edit in listEdit if edit.kind == kind]
    return listLine


# --- 폴더 표 ---------------------------------------------------------------------

#: 폴더 이름 표에서 빼는 뿌리 — `Resource/` 는 경로가 에셋 id 라 소문자이고, `Scripts/` 는 파이썬 패키지 이름 규칙이 따로 있다.
_kFolderScanRelDir: tuple[str, ...] = (*kLintTargetRelDirs, "Config", "cmake")


def buildFolderRenameTable(repositoryRoot: Path, acronyms: tuple[str, ...]) -> list[str]:
    """폴더 · 모듈 이름 변경 표(옮기지 않는다)."""
    listRow: list[str] = ["| 지금 | 바뀐 이름 | 따라 바뀌는 모듈(GF_*) · 타깃 |", "|---|---|---|"]
    setModule: set[str] = set()
    for cmakePath in collectRepositoryFiles(repositoryRoot, ("Source", "Test", "cmake"), fileNames=("CMakeLists.txt",), suffixes=(".cmake",),
                                            excludedDirNames=kNotOurCodeDirNames):
        text = readTextInternal(cmakePath) or ""
        setModule.update(re.findall(r"\bGF_\w+", text))
    for rootRel in _kFolderScanRelDir:
        root = repositoryRoot / rootRel
        if not root.is_dir():
            continue
        for directory in sorted(path for path in root.rglob("*") if path.is_dir()):
            relParts = directory.relative_to(repositoryRoot).parts
            if any(part in kNotOurCodeDirNames or part in kNotOurDirNames for part in relParts):
                continue
            newName = registry.respellPathPart(directory.name, acronyms)
            if newName == directory.name:
                continue
            listModule = sorted({module for module in setModule
                                 if module.split("_")[-1] == directory.name and registry.respellName(module, acronyms) != module})
            moduleText = " · ".join(f"`{module}` → `{registry.respellName(module, acronyms)}`" for module in listModule)
            listRow.append(f"| `{directory.relative_to(repositoryRoot).as_posix()}` | `{newName}` | {moduleText} |")
    return listRow


# --- 픽서 ------------------------------------------------------------------------


class FormatAcronymSpellingFixer(LintFixer):
    description = "약어 철자를 대문자로 고칩니다(UiSystem → UISystem, updateUi → updateUI) — 사전 실행 · 파일 · 폴더 모드"
    tag = "AcronymSpelling"
    listScopeRelDir = kLintTargetRelDirs
    fileKind = FixerFileKind(suffixes=registry.kCodeSuffix, excludedDirNames=kNotOurCodeDirNames)
    listPass = (
        FixPass(transformTypeNamesInternal,
                problem="타입 · 이름공간 · 열거형 이름의 약어가 Pascal 철자입니다(UiSystem → UISystem)",
                done="타입 이름의 약어를 대문자로 고쳤습니다",
                badSample="class UiSystem;\nenum class EGpuMode { Cpu, Gpu };\n",
                goodSample='class UISystem;\nclass ImGuiLayer;\nconst char* kName = "UiSystem"; // UiSystem\n#include "UI/UiSystem.h"\n'
                           "#define SW_UI_ENABLED 1\nVkPhysicalDeviceIDProperties props;\nstd::Idle x;\nclass Guid;\n"),
        FixPass(transformValueNamesInternal,
                problem="함수 · 변수 · 멤버 이름의 약어가 Pascal 철자입니다(updateUi → updateUI)",
                done="함수 · 변수 이름의 약어를 대문자로 고쳤습니다",
                badSample="void updateUi();\nint _pGpuScene = getOwnerId();\n",
                goodSample='void updateUI();\nint uiScale = 0;\nint gv_uiScale = 0;\nint _gpuScene = 0;\nauto s = "updateUi"; // updateUi\n'
                           "void drawImGuiPanel();\nint idx = 0;\n"),
    )

    def main(self, argv: Sequence[str] | None = None) -> int:
        parser = argparse.ArgumentParser(description=self.description)
        addFileArguments(parser)
        parser.add_argument("--check", action="store_true", help="고치지 않고 걸린 것만 찍는다(--apply-files 에서는 할 일만 찍는다)")
        parser.add_argument("--acronym", action="append", default=None, help="이 약어만(Ui · Gpu …, 쉼표로 여럿) — 생략하면 등록부 전체")
        parser.add_argument("--report", action="store_true", help="사전 실행 — 바뀔 이름 · 충돌 · 의심을 보고한다(고치지 않는다)")
        parser.add_argument("--out", type=Path, default=None, help="--report 의 상세 목록을 쓸 파일")
        parser.add_argument("--apply-files", action="store_true", help="파일 이름 · 파일 이름을 적은 곳 · 데이터 파일을 바꾼다")
        parser.add_argument("--rename-folders", action="store_true", help="폴더 · 모듈 이름 변경 표를 찍는다(옮기지 않는다)")
        parser.add_argument("--apply-text", action="store_true",
                            help="주석 · 문서 · 스크립트의 옛 이름을 새 철자로(코드모드 뒤에 돌린다). 문자열과 남은 철자는 목록만 — --check 면 고치지 않는다")
        parser.add_argument("--strings", action="store_true", help="--apply-text 에서 코드의 문자열 리터럴도 고친다(목록을 먼저 본 뒤)")
        parser.add_argument("--no-external-scan", action="store_true", help="--report 에서 외부 헤더를 훑지 않는다(의심 목록을 내지 않는다)")
        parser.add_argument("--refresh-external", action="store_true", help="--report 의 외부 헤더 훑기를 담아 둔 파일을 다시 만든다")
        args = parser.parse_args(argv)

        listProblem = registry.checkRegistry()
        if listProblem:
            print("\n".join(f"[{self.tag}] 등록부: {problem}" for problem in listProblem), file=sys.stderr)
            return 2
        try:
            acronyms = registry.resolveAcronyms(args.acronym)
        except ValueError as error:
            print(f"[{self.tag}] {error}", file=sys.stderr)
            return 2
        repositoryRoot = getProjectRoot()

        if args.rename_folders:
            print("\n".join(buildFolderRenameTable(repositoryRoot, acronyms)))
            print(f"[{self.tag}] 표만 찍었습니다 — 폴더는 옮기지 않았습니다")
            return 0

        _kState["acronyms"] = acronyms

        if args.report:
            externalNames = frozenset() if args.no_external_scan else loadExternalNames(repositoryRoot, args.refresh_external)
            listSummary, listDetail = buildReport(repositoryRoot, acronyms, externalNames)
            print("\n".join(listSummary))
            if args.out:
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text("\n".join(listDetail) + "\n", encoding="utf-8")
                print(f"[{self.tag}] 상세 목록: {args.out}")
            return 0

        if args.apply_files:
            try:
                listLine = applyFiles(repositoryRoot, acronyms, args.check)
            except RuntimeError as error:
                print(f"[{self.tag}] {error}", file=sys.stderr)
                return 2
            print("\n".join(f"[{self.tag}] {line}" for line in listLine) or f"[{self.tag}] 바꿀 파일이 없습니다")
            return 0

        if args.apply_text:
            listEdit = applyText(repositoryRoot, acronyms, args.check, args.strings)
            print("\n".join(f"[{self.tag}] {line}" for line in formatTextEdits(listEdit, args.strings)))
            return 0

        listFile = selectFixerTargetFiles(args, repositoryRoot, self.tag, self.fileKind)
        if not listFile:
            print(f"[{self.tag}] 대상 파일이 없습니다.", file=sys.stderr)
            return 0
        listMessage = self.processFiles(listFile, checkOnly=args.check)
        for message in listMessage:
            print(message)
        return 1 if (args.check and listMessage) else 0


main = FormatAcronymSpellingFixer.run


if __name__ == "__main__":
    sys.exit(main())
