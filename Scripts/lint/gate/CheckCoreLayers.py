#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core 폴더 층 검사 — `Source/Core/` 바로 아래 폴더가 곧 층이고, 아래 층은 위 층을 include 하지 못한다.

  0 Common · Predefined → 1 Concurrency · Math → 2 Memory → 3 Container → 4 Delegate · Uuid → 5 Log
  → 6 CommandLine · Compression · Process · String · Time → 7 GlobalVariable · Task → 8 File · Network → 9 Module
  → 10 Diagnostics · Event → 11 LogSink

강제 규칙:
  1) `Source/Core/<폴더>/` 의 파일은 **자기 폴더이거나 티어가 더 낮은 폴더**의 `Core/…` 헤더만 include 한다(같은 티어의 다른 폴더도 안 된다).
     `.cpp` 도 센다 — Core 를 여러 라이브러리로 나눌 때 링크 간선이 되기 때문이다.
  2) 폴더 파일은 루트 모음 헤더(`Core/CoreMinimal.h` · `Core/pch.h`)를 include 하지 않는다(폴더 하나가 Core 전체를 끌어온다).
  3) 표(_kCoreTier)에 없는 폴더는 실패다 — 새 폴더는 티어를 정하고 넣는다(`RunCoreLayerGraph.py` 가 계산해 준다).
  4) `Log` 보다 낮은 티어의 파일은 로그 매크로(`SW_LOG_*`)를 쓰지 않는다 — `.cpp` 는 pch 로 `Logger.h` 를 include 없이 받으므로
     include 간선에는 보이지 않는 거꾸로 가는 의존이다. 이미 있는 것은 `_kHiddenLogUse` 에 이유와 함께 적는다(줄일 대상).
  `Core/Network` 내부 방향은 `CheckCoreNetworkLayers.py` 가 따로 본다.

  python Scripts/lint/gate/CheckCoreLayers.py [--root <repo>] [--files a.h b.cpp]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kCorePrefix = "Core/"
_kSourceCorePrefix = "Source/Core/"

#: 폴더 → 티어. 숫자가 큰 쪽이 위다. `RunCoreLayerGraph.py` 의 Kahn 티어와 같아야 한다.
_kCoreTier: dict[str, int] = {
    "Common": 0,
    "Predefined": 0,      # 인자 · 이름 표(.xxx) — 아무것도 include 하지 않는다
    "Concurrency": 1,     # 원시 동기화(atomic · mutex · SpinLock · Futex)와 경합 검출 훅
    "Math": 1,
    "Memory": 2,          # 할당기 · 메모리 태그 · 할당 기록기 인터페이스
    "Container": 3,       # 컨테이너 · 문자열 타입과 StringUtil · formatString · 동시 큐
    "Delegate": 4,
    "Uuid": 4,
    "Log": 5,             # 로그 매크로와 전역 창구(Logger) — 기본 싱크는 LogSink
    "CommandLine": 6,
    "Compression": 6,
    "Process": 6,         # 프로세스 · 종료 신호 · 스레드 크래시 스택
    "String": 6,          # 이름(hashed_string · TagID) · 고정 문자열 — 로그 매크로를 쓴다
    "Time": 6,
    "GlobalVariable": 7,
    "Task": 7,
    "File": 8,
    "Network": 8,         # 다른 Core 폴더는 Network 를 include 하지 않는다
    "Module": 9,
    "Diagnostics": 10,    # 호출 스택 · 크래시 보고 · 메모리 프로파일러 · 교착 검출기 · 경합 보고기
    "Event": 10,
    "LogSink": 11,        # 기본 로그 싱크(AsyncLogSink)와 출력 장치
}

_kRootAggregate = ("Core/CoreMinimal.h", "Core/pch.h")

#: Log 보다 아래 티어에서 pch 로 로그 매크로를 쓰는 파일 → 이유. 새로 늘리지 않는다.
_kHiddenLogUse: dict[str, str] = {
    "Source/Core/Math/VectorMath.cpp": "영 벡터 단언(SW_LOG_ASSERT) — Math 가 Log 아래라 링크 간선이 거꾸로 간다",
    "Source/Core/Memory/LinearAllocator.cpp": "블록 표 소진 · 블록 할당 실패 Error 로그",
    "Source/Core/Container/FrameArenaAllocator.cpp": "프레임 아레나 청크 할당 실패 Error 로그",
    "Source/Core/Container/DynamicBitset.cpp": "비트 위치 · 크기 단언과 잘못된 글자 Error 로그",
    "Source/Core/Container/StringUtil.cpp": "UTF-8 검증 경고 · 단언",
}

_kSourceSuffixes = (".h", ".hpp", ".inl", ".cpp", ".xxx")
_kIncludeRe = re.compile(r'^\s*#\s*include\s*"(Core/[^"]+)"')
_kLogUseRe = re.compile(r"\bSW_LOG_(?:ERROR|WARNING|INFO|TRACE|ASSERT|CALLER)\b|\bLogger::")


def coreLayerOf(pathAfterCore: str) -> str:
    """`Core/` 뒤의 경로 → 폴더 이름. 루트 파일이면 빈 이름."""
    listPart = pathAfterCore.split("/")
    return listPart[0] if len(listPart) > 1 else ""


def stripComments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda match: "\n" * match.group(0).count("\n"), text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def findViolationsInFile(relative: str, text: str) -> list[str]:
    folder = coreLayerOf(relative[len(_kSourceCorePrefix):])
    if folder == "":
        return []
    if folder not in _kCoreTier:
        return [f"{relative}: 'Core/{folder}/' 는 티어 표(_kCoreTier)에 없는 폴더입니다"]
    tier = _kCoreTier[folder]
    listViolation: list[str] = []
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        match = _kIncludeRe.match(line)
        if match is None:
            continue
        includePath = normalizePath(match.group(1))
        if includePath in _kRootAggregate:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 폴더 파일이 루트 모음 헤더를 include 합니다")
            continue
        includeFolder = coreLayerOf(includePath[len(_kCorePrefix):])
        if includeFolder == folder or includeFolder == "":
            continue
        includeTier = _kCoreTier.get(includeFolder)
        if includeTier is None:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 티어 표에 없는 폴더입니다")
        elif includeTier >= tier:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 티어 {tier}('{folder}')가 티어 {includeTier}('{includeFolder}')를 include 합니다")
    if tier < _kCoreTier["Log"] and relative not in _kHiddenLogUse and _kLogUseRe.search(stripComments(text)):
        listViolation.append(f"{relative}: 티어 {tier}('{folder}')가 로그 매크로를 씁니다 — Log(티어 {_kCoreTier['Log']}) 아래 층은 로그를 쓰지 않습니다")
    return listViolation


def findViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> tuple[list[str], int]:
    listPath = LintGate.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=("Source/Core",), suffixes=_kSourceSuffixes)
    listViolation: list[str] = []
    fileCount = 0
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative.startswith(_kSourceCorePrefix) is False:
            continue
        fileCount += 1
        listViolation.extend(findViolationsInFile(relative, text))
    for relative in _kHiddenLogUse:
        if listFileArgument is None and (repositoryRoot / relative).is_file() is False:
            listViolation.append(f"{relative}: _kHiddenLogUse 에 있지만 파일이 없습니다 — 표에서 지우세요")
    return listViolation, fileCount


class CheckCoreLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "Core 폴더 티어(Common → … → LogSink)를 거꾸로 include 하지 않는지 검사"
    buildComment = "Checking the Core folder layers..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/Core/*",)
    preCommitFileArgument = "--files"
    violationHeader = "Core 층 위반"
    hint = (
        "  아래 층이 위 층을 알아야 하면 타입을 아래로 내리거나, 위 층이 인터페이스 · 함수 포인터를 건넨다(ILockObserver · IAllocationTracker 처럼).\n"
        "  새 폴더는 Scripts/lint/gate/CheckCoreLayers.py 의 _kCoreTier 에 티어를 정해 넣는다(Scripts/lint/report/RunCoreLayerGraph.py 가 계산한다)."
    )
    selfTestCases = [
        {
            "name": "컨테이너가 로그 헤더를 include 한다",
            "files": {"Source/Core/Container/Probe.h": "#pragma once\n#include \"Core/Log/Logger.h\"\n"},
        },
        {
            "name": "같은 티어의 다른 폴더를 include 한다",
            "files": {"Source/Core/String/Probe.cpp": "#include \"pch.h\"\n#include \"Core/Time/MonotonicClock.h\"\n"},
        },
        {
            "name": "폴더 파일이 루트 모음 헤더를 include 한다",
            "files": {"Source/Core/Memory/Probe.cpp": "#include \"pch.h\"\n#include \"Core/CoreMinimal.h\"\n"},
        },
        {
            "name": "표에 없는 폴더",
            "files": {"Source/Core/Scratch/Probe.h": "#pragma once\n"},
        },
        {
            "name": "Log 아래 층이 pch 로 로그 매크로를 쓴다",
            "files": {"Source/Core/Memory/Probe.cpp": "#include \"pch.h\"\nvoid probe() { SW_LOG_ERROR( \"x\" ); }\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation, fileCount = findViolations(repositoryRoot, args.files)
        return GateResult(listViolation=listViolation, summary=f"Core 파일 {fileCount} 개가 폴더 티어를 지킨다")


main = CheckCoreLayersGate.run

if __name__ == "__main__":
    sys.exit(main())
