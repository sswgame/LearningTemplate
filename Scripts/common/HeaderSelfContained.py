#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
헤더 하나를 **혼자** 컴파일해 보는 자리 — 혼자 서지 못하는 헤더를 찾는다.

헤더가 쓰는 이름의 선언을 직접 include 하지 않아도, 그것을 먼저 include 해 준 다른 헤더가 있으면 빌드는 통과한다.
그 "다른 헤더" 가 정리되는 날 내 코드를 안 고쳤는데 엉뚱한 파일에서 빌드가 깨진다. 그래서 헤더마다 `#include "그 헤더"` 한 줄짜리
TU 를 `-fsyntax-only` 로 컴파일한다.

쓰는 곳은 셋이다(같은 판정을 따로 만들지 말 것):

| 쓰는 곳 | 범위 | 실패하면 |
| --- | --- | --- |
| `report/RunHeaderSelfContained.py` | 트리 전체 · 폴더 | 보고만(`--fail-on-violation` 이면 1 — CI 정기 잡) |
| `gate/CheckHeaderSelfContained.py` | 커밋 훅의 staged 헤더 | 커밋을 막는다(빌드 폴더가 없으면 건너뛴다) |
| `report/RunForwardDeclarationCandidates.py --verify-unused` | include 한 줄을 뺀 사본 | 후보 분류 |

**플래그는 진짜 빌드에서 빌려 온다.** 컴파일 DB 에서 그 헤더와 경로가 가장 길게 겹치는 TU 의 명령줄을 쓴다 — Editor 헤더는 Editor TU 의
플래그로 본다. PCH(MSVC `/Yu` · `/Fp` · `/FI…cmake_pch`, clang `-Xclang -include-pch` · `-include …cmake_pch`)는 뗀다 — 두면 pch 가 미리
넣어 준 것이 누락을 가린다. gcc 식 의존 파일 플래그(`-MD` · `-MF` …)도 뗀다 — 단독 컴파일이 진짜 TU 의 `.d` 를 덮어쓰지 않게.

**빌드가 한 번은 돌았어야 한다.** 구성(configure)만 한 폴더의 `FlagOps.gen.h` 는 자리 표시자라 강제 include 된 플래그 연산이 없어 가짜
오류가 수십 건 난다(`findHeaderProbeProblem`).
"""

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
from pathlib import Path
from typing import Sequence

from .Parallel import getProcessWorkerCount, mapConcurrent
from .Paths import normalizePath
from .TranslationUnits import kCompileDatabaseFileName

__all__ = [
    "kDefaultHeaderProbeBuildDir",
    "kHeaderScanRoot",
    "collectCheckedHeaders",
    "findHeaderProbeProblem",
    "findHeadersNotSelfContained",
    "findSeedEntry",
    "isCheckedHeader",
    "loadCompileDatabase",
    "makeIncludeSpelling",
    "makeSeedIndex",
    "makeSyntaxOnlyCommand",
    "runSyntaxOnly",
]

#: 따로 주지 않으면 보는 빌드 폴더(저장소 기준). `.clangd` 도 이 폴더를 본다.
kDefaultHeaderProbeBuildDir = Path("build") / "Ninja-Debug"
#: 헤더를 찾는 자리이자 include 철자의 기준(`Source/` 아래 상대 경로가 저장소의 include 철자다).
kHeaderScanRoot = "Source"

# 생성 헤더와 PCH 는 혼자 서는 것이 목적이 아니다.
_kSkipSuffixes = (".gen.h",)
_kSkipNames = ("pch.h",)
#: 플래그 열거형이 반드시 있는 타깃의 강제 include 헤더 — 구성 때 쓴 자리 표시자 그대로면 코드젠이 아직 돌지 않았다
#: (`cmake/Engine/ReflectionCodeGen.cmake` 가 자리 표시자에 이 표식을 쓴다).
_kCodegenProbeHeader = Path("generated") / "Engine" / "FlagOps.gen.h"
_kCodegenPlaceholderMarker = "AUTO-GENERATED placeholder"
_kErrorRe = re.compile(r"error: (.+)")
#: 값 하나를 뒤에 받는 gcc 식 의존 파일 플래그.
_kDependencyFlagWithValue = ("-MT", "-MF", "-MQ")
_kDependencyFlag = ("-MD", "-MMD")
#: CMake 의 PCH 더미 TU — 플래그를 빌려 올 자리가 아니다(`/Yc` 로 PCH 를 만든다).
_kPchSourceSuffixes = ("cmake_pch.cxx", "cmake_pch.c")


def loadCompileDatabase(buildDir: Path) -> list[dict]:
    """`compile_commands.json` 을 읽는다. 없거나 읽지 못하면 빈 목록(이유는 `findHeaderProbeProblem` 이 말한다)."""
    try:
        return json.loads((buildDir / kCompileDatabaseFileName).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return []


def findHeaderProbeProblem(buildDir: Path) -> str:
    """이 빌드 폴더로 단독 컴파일을 할 수 없는 이유. 할 수 있으면 빈 문자열."""
    if not (buildDir / kCompileDatabaseFileName).is_file():
        return f"컴파일 DB 가 없습니다: {normalizePath(buildDir / kCompileDatabaseFileName)} — 그 프리셋을 configure 하세요"
    flagOps = buildDir / _kCodegenProbeHeader
    try:
        if _kCodegenPlaceholderMarker in flagOps.read_text(encoding="utf-8", errors="replace"):
            return f"리플렉션 코드젠이 아직 돌지 않았습니다({normalizePath(flagOps)} 가 자리 표시자) — 그 프리셋을 한 번 빌드하세요"
    except OSError:
        return f"{normalizePath(flagOps)} 가 없습니다 — 그 프리셋을 한 번 빌드하세요"
    return ""


def makeSeedIndex(database: Sequence[dict], buildDir: Path | None = None, repositoryRoot: Path | None = None) -> list[tuple[str, dict]]:
    """
    TU 를 자기 폴더(끝에 `/`)와 함께 늘어놓는다 — 헤더와 가장 많이 겹치는 것을 고르기 위한 표다.

    유니티 빌드(CI 프리셋)의 TU 는 빌드 폴더 안(`<빌드>/Source/Engine/CMakeFiles/Engine.dir/Unity/unity_0_cxx.cxx`)에 있어 소스 쪽 헤더와
    겹치는 길이가 저장소 루트까지뿐이다 — 모든 헤더가 첫 TU 의 플래그를 받는다. 빌드 폴더는 소스 폴더를 그대로 비추므로 `buildDir` ·
    `repositoryRoot` 를 주면 `CMakeFiles` 앞까지를 소스 쪽 경로로 옮겨 놓는다.
    """
    buildPrefix = normalizePath(buildDir.resolve()).lower() + "/" if buildDir is not None else ""
    rootPrefix = normalizePath(repositoryRoot.resolve()) + "/" if repositoryRoot is not None else ""
    listSeed: list[tuple[str, dict]] = []
    for entry in database:
        filePath = entry.get("file", "").replace("\\", "/")
        if not filePath or filePath.endswith(_kPchSourceSuffixes):
            continue
        seedDir = filePath.rsplit("/", 1)[0] + "/"
        if buildPrefix and rootPrefix and seedDir.lower().startswith(buildPrefix) and "/CMakeFiles/" in seedDir:
            seedDir = rootPrefix + seedDir[len(buildPrefix):].split("/CMakeFiles/")[0] + "/"
        listSeed.append((seedDir, entry))
    return listSeed


def findSeedEntry(headerPath: str, listSeed: Sequence[tuple[str, dict]]) -> dict | None:
    """헤더와 폴더 경로가 가장 길게 겹치는 TU 를 고른다."""
    bestEntry: dict | None = None
    bestLength = -1
    for seedDir, entry in listSeed:
        length = len(os.path.commonprefix([seedDir, headerPath]))
        if length > bestLength:
            bestLength = length
            bestEntry = entry
    return bestEntry


def isPchTokenInternal(token: str) -> bool:
    return "cmake_pch" in token


def makeSyntaxOnlyCommand(entry: dict, probeSource: Path) -> list[str]:
    """TU 의 명령줄에서 PCH · 출력 · 의존 파일 · 소스 파일을 떼고 `-fsyntax-only` 로 바꾼다."""
    rawCommand = entry.get("command") or " ".join(entry.get("arguments", []))
    listToken = shlex.split(rawCommand, posix=False)
    command: list[str] = []
    index = 0
    while index < len(listToken):
        token = listToken[index]
        nextToken = listToken[index + 1] if index + 1 < len(listToken) else ""
        # clang: -Xclang -include-pch -Xclang <pch> · -Xclang -include -Xclang <cmake_pch.hxx>
        if token == "-Xclang" and nextToken in ("-include-pch", "-include") and index + 3 < len(listToken) \
                and listToken[index + 2] == "-Xclang" and isPchTokenInternal(listToken[index + 3]):
            index += 4
            continue
        # gcc · clang: -include <cmake_pch.hxx>
        if token == "-include" and isPchTokenInternal(nextToken):
            index += 2
            continue
        if token in _kDependencyFlagWithValue or token == "-o":
            index += 2
            continue
        if token in _kDependencyFlag or token in ("-c", "--"):
            index += 1
            continue
        if token.startswith(("/Yc", "/Yu", "/Fp", "/Fo", "/Fd")) or (token.startswith("/FI") and isPchTokenInternal(token)):
            index += 1
            continue
        if token.endswith((".cpp", ".cc", ".cxx")):
            index += 1
            continue
        command.append(token)
        index += 1

    command.extend(["-fsyntax-only", "-Wno-unused-command-line-argument", str(probeSource)])
    return command


def runSyntaxOnly(entry: dict, probeSource: Path, buildDir: Path) -> str | None:
    """`probeSource` 를 `entry` 의 플래그로 단독 컴파일한다. 서면 None, 못 서면 첫 오류(없으면 "컴파일 실패")."""
    completed = subprocess.run(" ".join(makeSyntaxOnlyCommand(entry, probeSource)), shell=True, capture_output=True,
                               text=True, errors="replace", cwd=str(buildDir))
    if completed.returncode == 0:
        return None
    listReason = _kErrorRe.findall((completed.stdout or "") + (completed.stderr or ""))
    return listReason[0].strip() if listReason else "컴파일 실패 (오류 메시지 없음)"


def isCheckedHeader(headerPath: Path) -> bool:
    """혼자 서야 하는 헤더인가(생성 헤더 · PCH 가 아니다)."""
    return headerPath.suffix == ".h" and headerPath.name not in _kSkipNames and not headerPath.name.endswith(_kSkipSuffixes)


def collectCheckedHeaders(repositoryRoot: Path, pathFilter: str = "") -> list[Path]:
    """`Source/` 의 검사 대상 헤더. `pathFilter` 가 있으면 경로에 그 문자열이 든 것만."""
    scanRoot = repositoryRoot / kHeaderScanRoot
    if not scanRoot.is_dir():
        return []
    listHeader = [path for path in sorted(scanRoot.rglob("*.h")) if isCheckedHeader(path)]
    if pathFilter:
        needle = pathFilter.replace("\\", "/")
        listHeader = [path for path in listHeader if needle in normalizePath(path)]
    return listHeader


def makeIncludeSpelling(repositoryRoot: Path, headerPath: Path) -> str | None:
    """`Source/` 기준 상대 경로 — 저장소가 실제로 쓰는 include 철자. `Source/` 밖이면 None."""
    try:
        return headerPath.resolve().relative_to((repositoryRoot / kHeaderScanRoot).resolve()).as_posix()
    except ValueError:
        return None


def findHeadersNotSelfContained(repositoryRoot: Path, buildDir: Path, listHeader: Sequence[Path], probeDir: Path,
                                workerCount: int | None = None) -> list[tuple[str, str]]:
    """
    헤더마다 단독 컴파일해 서지 못하는 것의 (include 철자, 첫 오류) 를 철자 순으로 돌려준다.

    `probeDir` 은 부르는 쪽이 만든 임시 폴더다. 빌드 폴더가 쓸 수 없는 상태인지는 먼저 `findHeaderProbeProblem` 으로 볼 것.
    """
    listSeed = makeSeedIndex(loadCompileDatabase(buildDir), buildDir, repositoryRoot)
    if not listSeed:
        return []

    def checkOneHeader(headerPath: Path) -> tuple[str, str] | None:
        includeSpelling = makeIncludeSpelling(repositoryRoot, headerPath)
        if includeSpelling is None:
            return None
        entry = findSeedEntry(normalizePath(headerPath.resolve()), listSeed)
        if entry is None:
            return None
        probeSource = probeDir / (includeSpelling.replace("/", "_")[:-2] + "_probe.cpp")
        probeSource.write_text('#include "%s"\n' % includeSpelling, encoding="utf-8")
        reason = runSyntaxOnly(entry, probeSource, buildDir)
        return None if reason is None else (includeSpelling, reason)

    workers = workerCount if workerCount else getProcessWorkerCount(len(listHeader))
    return sorted(result for result in mapConcurrent(checkOneHeader, list(listHeader), workerCount=workers) if result is not None)
