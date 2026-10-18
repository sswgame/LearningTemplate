#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""게임 런타임 코드(GameFramework 기반 · 키트, Games)가 리소스를 디스크로 훑거나 디스크 경로로 푸는 곳을 잡는다.

리소스 폴더 나열은 `ResourceIO::collectResources`, 읽기는 `ResourceIO::read*` 하나다 — 둘 다 마운트된 팩과 낱개 파일 허용 정책을 본다.
디스크로 훑으면(`FileUtil::collectFiles` · `collectFolders` · `isDirectory`) 팩으로 쿠킹한 빌드에서 폴더가 조용히 비어 보이고, 디스크 경로로 풀면
(`ResourcePaths::findLooseFile` · `findLooseDomainFolder` · `makeAbsolutePath`, `LooseFilePath::getDiskPath`) 같은 결함이 읽기 쪽에서 난다.

형식이 먼저 막는다 — `findLoose*` 의 결과(`LooseFilePath`)는 `string` 으로 저절로 바뀌지 않는다. 그래도 남는 틈이 둘이라 이 게이트를 둔다.
① `FileUtil` 의 훑기 함수는 평범한 문자열을 받아야 한다(Saved · 설정 · 로그 · 쿠커 · 에디터의 정당한 디스크 폴더). 그래서 형식으로는 리소스 폴더만
골라 막을 수 없다. ② `getDiskPath()` 와 디스크 입구(`load*FromDiskFile` —소스 트리를 디스크로 훑은 경로를 팩 정책 없이
읽는다)는 쿠커 · 에디터 · 도구 · 테스트가 써야 하므로 있어야 한다. 런타임 게임 코드만 이것들을 쓰지 않게 여기서 막는다.
에디터 확장(`Source/GameFramework/PlugIn/`)은 Dev 전용 에디터 코드라 보지 않는다. 엔진(`Source/Engine`)은 쿠커와 런타임이 한 폴더에 섞여 있어
파일 단위로 가를 기준이 없다 — 엔진 쪽은 `getDiskPath()` 가 보이는 호출 자리로 남는 것이 형식의 몫이다.

예외는 `Scripts/lint/rules/CheckRuntimeResourceDiskAccess.toml` 의 `[exemption]` 한 곳이다.

  python Scripts/lint/gate/CheckRuntimeResourceDiskAccess.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate · gate

from common import findFast, kDirSourceGameFramework, kDirSourceGames  # noqa: E402
from LintGate import LintGate  # noqa: E402
from SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = (kDirSourceGameFramework, kDirSourceGames)
_kSuffixes = (".h", ".hpp", ".inl", ".cpp")
# 에디터 확장은 Dev 전용 에디터 코드다(디스크의 원본 에셋을 다룬다).
_kExcludedPrefix = f"{kDirSourceGameFramework}/PlugIn/"

_kDiskAccessRe = re.compile(
    r"(FileUtil::(?:collectFiles|collectFolders|isDirectory)"
    r"|ResourcePaths::(?:findLooseFile|findLooseDomainFolder|makeAbsolutePath)"
    r"|\.getDiskPath\s*\("
    r"|\bload\w*FromDiskFile\s*\()")


def findDiskAccess(source: SourceFile) -> list[str]:
    """예외 밖에서 리소스를 디스크로 훑거나 푸는 줄을 위반 문자열로 돌려줍니다."""
    gate = CheckRuntimeResourceDiskAccessGate
    relative = source.relative
    if relative.startswith(_kExcludedPrefix):
        return []
    bExempt = relative in gate.mapExemption
    listCodeMatch: list[tuple[int, str]] = []
    if findFast(_kDiskAccessRe, source.text) is not None:
        listCodeMatch = [(lineIndex, match.group(1))
                         for lineIndex, line in enumerate(source.listMaskedLine, start=1)
                         for match in _kDiskAccessRe.finditer(line)]
    if bExempt:
        gate.seeExemption(relative)
        if listCodeMatch:
            gate.useExemption(relative)
        return []
    return [f"{relative}:{lineIndex}: {call.rstrip('(').strip()}  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, call in listCodeMatch]


class CheckRuntimeResourceDiskAccessGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "게임 런타임 코드가 리소스를 디스크로 훑거나 디스크 경로로 풀지 않고 ResourceIO 를 쓰는지 검사"
    buildComment = "Checking that game runtime code lists and reads resources through ResourceIO..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 리소스 디스크 접근"
    violationHeader = "리소스 디스크 접근"
    hint = (
        "  리소스 폴더 나열은 ResourceIO::collectResources, 읽기는 ResourceIO::readTextResource · readBinaryResource 입니다\n"
        "  (팩과 낱개 파일 허용 정책을 한 곳에서 봅니다). 리소스가 아닌 디스크 폴더(설정 · Saved)를 정말 훑어야 하면\n"
        "  Scripts/lint/rules/CheckRuntimeResourceDiskAccess.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "기반이 리소스 폴더를 디스크로 훑는다",
            "expectedViolation": "Source/GameFramework/Base/Gameplay/Probe/Probe.cpp:3: FileUtil::collectFiles",
            "files": {
                "Source/GameFramework/Base/Gameplay/Probe/Probe.cpp": (
                    "#include \"pch.h\"\n"
                    "void probe( sw::vector<sw::string>& outListFile )\n"
                    "{ (void)sw::FileUtil::collectFiles( \"x\", \".xml\", outListFile, false ); }\n"
                ),
            },
        },
        {
            "name": "게임이 리소스 id 를 디스크 경로로 꺼낸다",
            "expectedViolation": "Source/Games/Probe/Probe.cpp:2: .getDiskPath",
            "files": {
                "Source/Games/Probe/Probe.cpp": (
                    "#include \"pch.h\"\n"
                    "sw::string probe() { return sw::ResourcePaths::findLooseFile( \"a.xml\" ).getDiskPath(); }\n"
                ),
            },
        },
        {
            "name": "디스크 훑기를 더는 하지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.cpp' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.cpp": "// FileUtil::collectFiles 는 주석 안이라 호출이 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 GameFramework · Games 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findDiskAccess(source))


main = CheckRuntimeResourceDiskAccessGate.run


if __name__ == "__main__":
    sys.exit(main())
