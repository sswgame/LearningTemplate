#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`Source/Core/Memory` 밖에서 `Memory::allocate` · `Memory::allocateAligned` 로 맨 블록을 잡는 곳을 잡는다.

맨 블록(`void*` + 수동 `Memory::free`)은 일찍 돌아가는 길 · 예외에서 새는 모양이다. 형식이 이미 있다 — 거친 바이트 블록은
`MemoryBlock`(`Core/Memory/MemoryBlock.h`), 객체는 `make_unique<T>`, 원소 배열은 `vector<T>` · `make_unique<T[]>` 다.
이 게이트는 그 형식으로 못 바꾸는 틈만 목록으로 남긴다: 외부 라이브러리가 할당 · 해제 콜백을 요구하는 자리(`pugi::set_memory_management_functions` 등),
락 없이 읽는 표에 올리는 블록, 리플렉션이 만든 형식 소거 인스턴스. 이 자리는 소유 형식으로 감쌀 수 없어서(콜백은 `void*` 를 돌려준다) 형식이 막지 못한다.

맨 `sw_new` 는 `Style/RawNew`(`CheckCodeConventions`) 가 같은 이유로 막는다.

`Source/Core/Memory/` 는 할당자 자신이라 보지 않는다. 주석 · 문자열 안의 언급과 테스트(`Test/`)도 보지 않는다.
예외는 `Scripts/lint/rules/CheckRawAllocation.toml` 의 `[exemption]` 한 곳이다. 예외 파일이 더는 맨 할당을 쓰지 않으면 그 줄은 낡은 예외로 실패한다(`LintGate` 기반이 본다).

  python Scripts/lint/gate/CheckRawAllocation.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import findFast  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = ("Source",)
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")
_kAllocatorFolder = "Source/Core/Memory/"

# `&Memory::allocate`(주소를 넘기는 등록)도 잡도록 뒤에 `(` 를 요구하지 않는다. `MemoryBlock::allocate` 는 `Memory::` 로 시작하지 않아 빠진다.
_kRawAllocationRe = re.compile(r"(?<![\w$])Memory::allocate(?:Aligned)?(?![\w$])")


def findRawAllocations(source: SourceFile) -> list[str]:
    """예외 밖에서 맨 블록을 잡는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckRawAllocationGate
    relative = source.relative
    if relative.startswith("Source/") == False or relative.startswith(_kAllocatorFolder):
        return []
    bExempt = relative in gate.mapExemption
    listCodeMatch: list[tuple[int, str]] = []
    if findFast(_kRawAllocationRe, source.text) is not None:
        listCodeMatch = [(lineIndex, match.group(0))
                         for lineIndex, line in enumerate(source.listMaskedLine, start=1)
                         for match in _kRawAllocationRe.finditer(line)]
    if bExempt:
        gate.seeExemption(relative)
        if listCodeMatch:
            gate.useExemption(relative)
        return []
    return [f"{relative}:{lineIndex}: {name}  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, name in listCodeMatch]


class CheckRawAllocationGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "Core/Memory 밖의 맨 Memory::allocate 가 소유 형식(MemoryBlock · make_unique · vector)을 쓰는지 검사"
    buildComment = "Checking that engine code holds raw blocks through MemoryBlock, not Memory::allocate + Memory::free..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 Memory::allocate 직접 사용"
    violationHeader = "Memory::allocate 직접 사용"
    hint = (
        "  거친 바이트 블록은 소유 형식으로 듭니다 — 일찍 돌아가도 새지 않고, 블록 목록은 vector<MemoryBlock> 을 비우면 풀립니다:\n"
        "      #include \"Core/Memory/MemoryBlock.h\"      MemoryBlock block = MemoryBlock::allocate( size );   if ( block.isValid() == false ) return false;\n"
        "  객체는 make_unique<T>( ... ), 원소 배열은 vector<T> · make_unique<T[]>( n ) 입니다.\n"
        "  외부 라이브러리가 `void*` 할당 · 해제 콜백을 요구하는 자리처럼 정말 형식으로 못 감싸면\n"
        "  Scripts/lint/rules/CheckRawAllocation.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "블록을 잡고 수동으로 푼다",
            "expectedViolation": "Source/Probe/ProbeBuffer.cpp:3: Memory::allocate",
            "files": {
                "Source/Probe/ProbeBuffer.cpp": (
                    "void probe( size_t size )\n"
                    "{\n"
                    "    void* pBlock = Memory::allocate( size );\n"
                    "    Memory::free( pBlock );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "정렬 할당 함수의 주소를 등록한다",
            "expectedViolation": "Source/Probe/ProbeHook.cpp:3: Memory::allocateAligned",
            "files": {
                "Source/Probe/ProbeHook.cpp": (
                    "void probe()\n"
                    "{\n"
                    "    registerHooks( &Memory::allocateAligned );\n"
                    "}\n"
                ),
            },
        },
        {
            "name": "맨 할당을 더는 쓰지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/Engine/UI/Document/UIDocumentLoader.cpp' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Engine/UI/Document/UIDocumentLoader.cpp": "// Memory::allocate 는 주석 안이라 쓰임이 아니다\nint probe() { return 0; }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findRawAllocations(source))


main = CheckRawAllocationGate.run


if __name__ == "__main__":
    sys.exit(main())
