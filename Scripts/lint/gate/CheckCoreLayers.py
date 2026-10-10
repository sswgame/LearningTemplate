#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core 폴더 층 검사 — `Source/Core/` 바로 아래 폴더가 곧 층이고, 아래 층은 위 층을 include 하지 못한다.

  0 Common · Predefined → 1 Concurrency · Math → 2 Memory → 3 Container → 4 Delegate · UUID → 5 Log
  → 6 CommandLine · Compression · Process · String · Time → 7 GlobalVariable · Task → 8 File · Network → 9 Module
  → 10 Diagnostics · Event → 11 LogSink

강제 규칙:
  1) `Source/Core/<폴더>/` 의 파일은 **자기 폴더이거나 티어가 더 낮은 폴더**의 `Core/…` 헤더만 include 한다(같은 티어의 다른 폴더도 안 된다).
     `.cpp` 도 센다 — Core 를 여러 라이브러리로 나눌 때 링크 간선이 되기 때문이다.
  2) 폴더 파일은 루트 모음 헤더(`Core/CoreMinimal.h` · `Core/pch.h`)를 include 하지 않는다(폴더 하나가 Core 전체를 끌어온다).
  3) 티어 표에 없는 폴더는 실패다 — 새 폴더는 티어를 정하고 넣는다(`RunCoreLayerGraph.py` 가 계산해 준다).
  4) `Log` 보다 낮은 티어의 파일은 로그 매크로(`SW_LOG_*`)를 쓰지 않는다 — `.cpp` 는 pch 로 `Logger.h` 를 include 없이 받으므로
     include 간선에는 보이지 않는 거꾸로 가는 의존이다. 이미 있는 것은 예외 표에 이유와 함께 적는다(줄일 대상).
  `Core/Network` 내부 방향은 `CheckCoreNetworkLayers.py` 가 따로 본다.

티어 표(`[tier]`)와 예외 표(`[exemption]`)는 `Scripts/lint/rules/CheckCoreLayers.toml` 에 있다.

  python Scripts/lint/gate/CheckCoreLayers.py [--root <repo>] [--files a.h b.cpp]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments, firstFolderAfter, iterIncludes, normalizePath  # noqa: E402
from common.RuleData import kKindInteger  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kCorePrefix = "Core/"
_kSourceCorePrefix = "Source/Core/"

#: `rules/CheckCoreLayers.toml` 에서 예외 표 말고 읽는 것 — 폴더 → 티어(숫자가 큰 쪽이 위).
_kRuleSchema = {"tier": kKindInteger}
_kCoreTier: dict[str, int] = LintGate.readRules("CheckCoreLayers", _kRuleSchema, requiredKeys=("tier",))["tier"]

_kRootAggregate = ("Core/CoreMinimal.h", "Core/pch.h")

_kSourceSuffixes = (".h", ".hpp", ".inl", ".cpp", ".xxx")
_kLogUseRe = re.compile(r"\bSW_LOG_(?:ERROR|WARNING|INFO|TRACE|ASSERT|CALLER)\b|\bLogger::")


def findViolationsInFile(relative: str, text: str) -> list[str]:
    folder = firstFolderAfter(relative, _kSourceCorePrefix)
    if folder == "":
        return []
    if folder not in _kCoreTier:
        return [f"{relative}: 'Core/{folder}/' 는 티어 표(rules/CheckCoreLayers.toml 의 [tier])에 없는 폴더입니다"]
    tier = _kCoreTier[folder]
    listViolation: list[str] = []
    for lineNumber, rawInclude in iterIncludes(text, bQuotedOnly=True):
        if rawInclude.startswith(_kCorePrefix) is False:
            continue
        includePath = normalizePath(rawInclude)
        if includePath in _kRootAggregate:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 폴더 파일이 루트 모음 헤더를 include 합니다")
            continue
        includeFolder = firstFolderAfter(includePath, _kCorePrefix)
        if includeFolder == folder or includeFolder == "":
            continue
        includeTier = _kCoreTier.get(includeFolder)
        if includeTier is None:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 티어 표에 없는 폴더입니다")
        elif includeTier >= tier:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 티어 {tier}('{folder}')가 티어 {includeTier}('{includeFolder}')를 include 합니다")
    if tier < _kCoreTier["Log"]:
        bExempt = relative in CheckCoreLayersGate.mapExemption
        if bExempt:
            CheckCoreLayersGate.seeExemption(relative)
        if _kLogUseRe.search(blankComments(text)):
            if bExempt:
                CheckCoreLayersGate.useExemption(relative)
            else:
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
    return listViolation, fileCount


class CheckCoreLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    ruleSchema = _kRuleSchema
    description = "Core 폴더 티어(Common → … → LogSink)를 거꾸로 include 하지 않는지 검사"
    buildComment = "Checking the Core folder layers..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/Core/*",)
    preCommitFileArgument = "--files"
    violationHeader = "Core 층 위반"
    hint = (
        "  아래 층이 위 층을 알아야 하면 타입을 아래로 내리거나, 위 층이 인터페이스 · 함수 포인터를 건넨다(ILockObserver · IAllocationTracker 처럼).\n"
        "  새 폴더는 Scripts/lint/rules/CheckCoreLayers.toml 의 [tier] 에 티어를 정해 넣는다(Scripts/lint/report/RunCoreLayerGraph.py 가 계산한다)."
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
