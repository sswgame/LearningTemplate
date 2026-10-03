#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""컴파일러 내장 매크로로 플랫폼 · 아키텍처 · 컴파일러를 묻는 곳을 잡는다 — 코드는 CMake 가 정한 SW_ 매크로만 읽는다.

CMake 가 판정해 정의한다(`cmake/Modules/Platform` · `Architecture` · `Compiler`):

    SW_PLATFORM_WINDOWS · SW_PLATFORM_LINUX · SW_PLATFORM_MACOS
    SW_X64 · SW_ARM64
    SW_COMPILER_CLANG(clang-cl 포함) · SW_COMPILER_MSVC(cl.exe) · SW_COMPILER_GCC

내장 매크로(`_MSC_VER` · `__clang__` · `__x86_64__` · `_WIN32` …)는 컴파일러마다 이름이 달라 같은 질문을 두 벌씩 적게 되고,
clang-cl 은 `__clang__` 과 `_MSC_VER` 를 둘 다 정의해서 `_MSC_VER` 가 "cl.exe 인가" 인지 "MSVC 확장을 쓸 수 있는가" 인지
읽어서는 알 수 없다. 내장 매크로를 읽는 곳은 CMake 판정과 실제 컴파일러를 대조하는 `Source/Core/Common/TargetMacroCheck.h`
하나뿐이다. 주석 · 문자열 안의 언급은 보지 않는다.

  python Scripts/lint/gate/CheckTargetMacros.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import normalizePath, readTextFiles  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 내장 매크로를 읽어도 되는 유일한 파일(CMake 판정과 실제 컴파일러를 대조한다).
_kCheckHeader = "Source/Core/Common/TargetMacroCheck.h"

_kListScanRoot = ("Source", "Test", "Tools/ReflectionParser")
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

#: 내장 매크로 → 대신 쓸 것.
_kMapBuiltinToReplacement = {
    "_WIN32": "SW_PLATFORM_WINDOWS",
    "_WIN64": "SW_PLATFORM_WINDOWS",
    "__linux__": "SW_PLATFORM_LINUX",
    "__linux": "SW_PLATFORM_LINUX",
    "__APPLE__": "SW_PLATFORM_MACOS",
    "__MINGW32__": "지원하지 않는 툴체인(Windows 는 MS ABI 만 짓는다)",
    "__MINGW64__": "지원하지 않는 툴체인(Windows 는 MS ABI 만 짓는다)",
    "_M_X64": "SW_X64",
    "_M_AMD64": "SW_X64",
    "__x86_64__": "SW_X64",
    "__amd64__": "SW_X64",
    "_M_ARM64": "SW_ARM64",
    "_M_ARM64EC": "SW_ARM64",
    "__aarch64__": "SW_ARM64",
    "__arm64__": "SW_ARM64",
    "_M_IX86": "지원하지 않는 아키텍처(엔진은 64 비트만 짓는다)",
    "__i386__": "지원하지 않는 아키텍처(엔진은 64 비트만 짓는다)",
    "_M_ARM": "지원하지 않는 아키텍처(엔진은 64 비트만 짓는다)",
    "__arm__": "지원하지 않는 아키텍처(엔진은 64 비트만 짓는다)",
    "__clang__": "SW_COMPILER_CLANG",
    "__GNUC__": "SW_COMPILER_GCC (Clang 도 정의한다 — 둘 다면 SW_COMPILER_CLANG || SW_COMPILER_GCC)",
    "__GNUG__": "SW_COMPILER_GCC (Clang 도 정의한다 — 둘 다면 SW_COMPILER_CLANG || SW_COMPILER_GCC)",
    "_MSC_VER": "MSVC 확장(intrinsic · __declspec · __FUNCSIG__)이면 SW_PLATFORM_WINDOWS, cl.exe 만이면 SW_COMPILER_MSVC",
}

_kBuiltinRe = re.compile(r"(?<![\w$])(" + "|".join(re.escape(name) for name in _kMapBuiltinToReplacement) + r")(?![\w$])")

#: 주석 · 문자열 · 문자 리터럴 — 같은 길이의 공백으로 지워 줄 번호를 지킨다.
_kCommentOrLiteralRe = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.DOTALL)


def blankCommentsAndLiterals(text: str) -> str:
    """주석 · 문자열 · 문자 리터럴을 같은 길이의 공백으로 바꿉니다(줄바꿈은 남긴다)."""
    return _kCommentOrLiteralRe.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def findBuiltinMacroUses(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """검사 헤더 밖에서 내장 매크로를 읽는 줄을 위반 문자열로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    for path, text in readTextFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative == _kCheckHeader:
            continue
        if _kBuiltinRe.search(text) is None:
            continue
        listOriginalLine = text.splitlines()
        for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
            for match in _kBuiltinRe.finditer(line):
                builtin = match.group(1)
                listViolation.append(f"{relative}:{lineIndex}: {builtin} -> {_kMapBuiltinToReplacement[builtin]}"
                                     f"  | {listOriginalLine[lineIndex - 1].strip()}")
    return listViolation


class CheckTargetMacrosGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "플랫폼 · 아키텍처 · 컴파일러를 내장 매크로가 아니라 SW_ 매크로로 묻는지 검사"
    buildComment = "Checking that platform/architecture/compiler checks use SW_ macros..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "컴파일러 내장 매크로 사용"
    hint = (
        "  플랫폼 · 아키텍처 · 컴파일러는 CMake 가 정의하는 SW_ 매크로로 묻습니다:\n"
        "      SW_PLATFORM_WINDOWS · SW_PLATFORM_LINUX · SW_PLATFORM_MACOS · SW_X64 · SW_ARM64\n"
        "      SW_COMPILER_CLANG(clang-cl 포함) · SW_COMPILER_MSVC(cl.exe) · SW_COMPILER_GCC\n"
        f"  내장 매크로를 읽는 곳은 {_kCheckHeader} 하나뿐입니다(CMake 판정과 실제 컴파일러를 대조한다)."
    )
    selfTestCases = [
        {
            "name": "_MSC_VER 로 컴파일러를 묻는다",
            "files": {
                _kCheckHeader: "#pragma once\n",
                "Source/Probe/ProbeCompiler.h": (
                    "#pragma once\n"
                    "#if defined( _MSC_VER )\n"
                    "    #define PROBE_INLINE __forceinline\n"
                    "#endif\n"
                ),
            },
        },
        {
            "name": "__x86_64__ 로 아키텍처를 묻는다(Test 폴더)",
            "files": {
                _kCheckHeader: "#pragma once\n",
                "Test/Probe/ProbeArch.cpp": (
                    "#ifdef __x86_64__\n"
                    "int probe() { return 1; }\n"
                    "#endif\n"
                ),
            },
        },
        {
            "name": "_WIN32 로 플랫폼을 묻는다(ReflectionParser)",
            "files": {
                _kCheckHeader: "#pragma once\n",
                "Tools/ReflectionParser/ProbePlatform.cpp": "#if !defined(_WIN32) && defined(__clang__)\n#endif\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 Source · Test · Tools/ReflectionParser 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 예외 파일이 사라지면(이름이 바뀌면) 예외도 함께 낡는다 — 조용히 넘기지 않는다.
        if not (repositoryRoot / _kCheckHeader).is_file():
            raise GateError(f"{_kCheckHeader} 가 없습니다 — 내장 매크로를 읽어도 되는 유일한 파일입니다. 옮겼다면 이 게이트의 _kCheckHeader 를 고치십시오.")
        violations = findBuiltinMacroUses(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary="Source · Test · Tools/ReflectionParser 의 내장 매크로")


main = CheckTargetMacrosGate.run


if __name__ == "__main__":
    sys.exit(main())
