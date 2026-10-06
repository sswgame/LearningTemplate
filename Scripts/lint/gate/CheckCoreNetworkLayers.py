#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Core 네트워크 층 검사 — `Source/Core/Network/` 의 폴더가 곧 층이다.

  뿌리(NetTypes · BitStream) ← Transport · Security ← Connection ← Message ← Replication

강제 규칙:
  1) `Core/Network/` 아래 파일은 **자기 층 이하**의 `Core/Network/` 헤더만 include 한다(같은 폴더끼리는 된다).
     전송은 연결을 모르고, 연결은 메시지 라우터를 모르고, 메시지는 복제 부품을 모른다.
     보안(암호 창구 · 재전송 방지 창 · 세션 키 유도)은 뿌리만 본다 — 연결(UDP AEAD) · 메시지(TLS 끝점)가 쓴다.
  2) 표(_kNetworkTier)에 없는 하위 폴더는 실패다 — 새 폴더는 층을 정하고 넣는다.

  python Scripts/lint/gate/CheckCoreNetworkLayers.py [--root <repo>] [--files a.h b.cpp]
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

_kNetworkPrefix = "Core/Network/"
_kSourceNetworkPrefix = "Source/Core/Network/"

#: 폴더 → 층. 숫자가 큰 쪽이 위다. 빈 이름은 `Core/Network/` 바로 아래 파일(뿌리)이다.
_kNetworkTier: dict[str, int] = {
    "": 0,
    "Transport": 1,
    "Security": 1,  # 암호 창구 — 뿌리만 본다. 같은 층의 Transport 와는 서로 include 할 일이 없다
    "Connection": 2,
    "Message": 3,
    "Replication": 4,
}

_kSourceSuffixes = (".h", ".hpp", ".inl", ".cpp")
_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def findFolderOf(pathAfterNetwork: str) -> str:
    """`Core/Network/` 뒤의 경로 → 폴더 이름(뿌리 파일이면 빈 이름)입니다."""
    listPart = pathAfterNetwork.split("/")
    return listPart[0] if len(listPart) > 1 else ""


def findViolationsInFile(relative: str, text: str) -> list[str]:
    folder = findFolderOf(relative[len(_kSourceNetworkPrefix):])
    if folder not in _kNetworkTier:
        return [f"{relative}: 'Core/Network/{folder}/' 는 층 표에 없는 폴더입니다"]
    tier = _kNetworkTier[folder]
    listViolation: list[str] = []
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        match = _kIncludeRe.match(line)
        if match is None:
            continue
        includePath = normalizePath(match.group(1))
        if includePath.startswith(_kNetworkPrefix) is False:
            continue
        includeFolder = findFolderOf(includePath[len(_kNetworkPrefix):])
        includeTier = _kNetworkTier.get(includeFolder)
        if includeTier is None:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 층 표에 없는 폴더입니다")
        elif includeTier > tier:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 층 {tier}('{folder or '뿌리'}')가 위 층 {includeTier}('{includeFolder}')를 include 합니다")
    return listViolation


def findViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> tuple[list[str], int]:
    listPath = LintGate.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=("Source/Core/Network",), suffixes=_kSourceSuffixes)
    listViolation: list[str] = []
    fileCount = 0
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative.startswith(_kSourceNetworkPrefix) is False:
            continue
        fileCount += 1
        listViolation.extend(findViolationsInFile(relative, text))
    return listViolation, fileCount


class CheckCoreNetworkLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "Core/Network 의 폴더 층(뿌리 ← Transport · Security ← Connection ← Message ← Replication)을 거꾸로 include 하지 않는지 검사"
    buildComment = "Checking the Core/Network folder layers..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/Core/Network/*",)
    preCommitFileArgument = "--files"
    violationHeader = "Core/Network 층 위반"
    hint = (
        "  아래 층이 위 층을 알아야 하면 그 타입을 아래 층으로 내리거나(공용 타입은 NetTypes.h), 위 층이 아래 층에 콜백 · 인터페이스를 건넨다.\n"
        "  새 폴더는 Scripts/lint/gate/CheckCoreNetworkLayers.py 의 _kNetworkTier 에 층을 정해 넣는다."
    )
    selfTestCases = [
        {
            "name": "전송이 연결 헤더를 include 한다",
            "files": {"Source/Core/Network/Transport/Probe.h": "#pragma once\n#include \"Core/Network/Connection/NetHost.h\"\n"},
        },
        {
            "name": "연결이 복제 부품을 include 한다",
            "files": {"Source/Core/Network/Connection/Probe.cpp": "#include \"pch.h\"\n#include \"Core/Network/Replication/NetPrioritizer.h\"\n"},
        },
        {
            "name": "보안이 연결 헤더를 include 한다",
            "files": {"Source/Core/Network/Security/Probe.h": "#pragma once\n#include \"Core/Network/Connection/NetHost.h\"\n"},
        },
        {
            "name": "표에 없는 폴더",
            "files": {"Source/Core/Network/Session/Probe.h": "#pragma once\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation, fileCount = findViolations(repositoryRoot, args.files)
        return GateResult(listViolation=listViolation, summary=f"Core/Network 파일 {fileCount} 개가 층을 지킨다")


main = CheckCoreNetworkLayersGate.run

if __name__ == "__main__":
    sys.exit(main())
