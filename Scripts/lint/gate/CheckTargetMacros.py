#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""컴파일러 내장 매크로로 플랫폼 · 아키텍처 · 컴파일러를 묻는 곳을 잡는다 — 코드는 CMake 가 정한 SW_ 매크로만 읽는다.

CMake 가 판정해 정의한다(`cmake/Modules/Platform` · `Architecture` · `Compiler`):

    SW_PLATFORM_WINDOWS · SW_PLATFORM_LINUX(macOS 는 지원하지 않는다)
    SW_X64 · SW_ARM64
    SW_COMPILER_CLANG(clang-cl 포함) · SW_COMPILER_MSVC(cl.exe) · SW_COMPILER_GCC

내장 매크로(`_MSC_VER` · `__clang__` · `__x86_64__` · `_WIN32` …)는 컴파일러마다 이름이 달라 같은 질문을 두 벌씩 적게 되고,
clang-cl 은 `__clang__` 과 `_MSC_VER` 를 둘 다 정의해서 `_MSC_VER` 가 "cl.exe 인가" 인지 "MSVC 확장을 쓸 수 있는가" 인지
읽어서는 알 수 없다. 내장 매크로를 읽는 곳은 CMake 판정과 실제 컴파일러를 대조하는 `Source/Core/Common/TargetMacroCheck.h`
하나뿐이다. 주석 · 문자열 안의 언급은 보지 않는다.

지원하지 않는 플랫폼의 SW_ 매크로(`SW_PLATFORM_MACOS`)도 막는다. CMake 가 정의하지 않으므로 그 갈래는 어느 구성에서도 컴파일되지 않는
죽은 코드다 — 검사 헤더도 예외가 아니다.

규칙 데이터(예외 표 · 목록)는 `Scripts/lint/rules/CheckTargetMacros.toml` 에 있다.

  python Scripts/lint/gate/CheckTargetMacros.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankCommentsAndLiterals, kLintTargetRelDirs, normalizePath  # noqa: E402
from common.RuleData import kKindText  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 내장 매크로를 읽어도 되는 유일한 파일(CMake 판정과 실제 컴파일러를 대조한다).
_kCheckHeader = "Source/Core/Common/TargetMacroCheck.h"

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

#: 내장 매크로 → 대신 쓸 것(`rules/CheckTargetMacros.toml`).
_kRuleSchema = {"builtin": kKindText}
_kMapBuiltinToReplacement: dict[str, str] = LintGate.readRules("CheckTargetMacros", _kRuleSchema, requiredKeys=("builtin",))["builtin"]

_kBuiltinRe = re.compile(r"(?<![\w$])(" + "|".join(re.escape(name) for name in _kMapBuiltinToReplacement) + r")(?![\w$])")

#: 지원하지 않는 플랫폼의 SW_ 매크로 → 이유. 검사 헤더를 포함해 어디서도 읽지 않는다.
_kMapUnsupportedMacroToReason = {
    "SW_PLATFORM_MACOS": "지원하지 않는 플랫폼(Windows · Linux 만 짓는다) — CMake 가 정의하지 않아 그 갈래는 컴파일되지 않는다",
}

_kUnsupportedRe = re.compile(r"(?<![\w$])(" + "|".join(re.escape(name) for name in _kMapUnsupportedMacroToReason) + r")(?![\w$])")

def findBuiltinMacroUses(repositoryRoot: Path, listTargetFile: list[str] | None) -> list[str]:
    """검사 헤더 밖에서 내장 매크로를 읽는 줄과, 어디서든 지원하지 않는 플랫폼 매크로를 읽는 줄을 위반 문자열로 돌려줍니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listTargetFile, listScanRoot=_kListScanRoot, suffixes=_kSuffixes)
    listViolation: list[str] = []
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        bCheckBuiltin = relative != _kCheckHeader and _kBuiltinRe.search(text) is not None
        bCheckUnsupported = _kUnsupportedRe.search(text) is not None
        if bCheckBuiltin is False and bCheckUnsupported is False:
            continue
        listOriginalLine = text.splitlines()
        for lineIndex, line in enumerate(blankCommentsAndLiterals(text).splitlines(), start=1):
            originalLine = listOriginalLine[lineIndex - 1].strip()
            if bCheckBuiltin:
                for match in _kBuiltinRe.finditer(line):
                    builtin = match.group(1)
                    listViolation.append(f"{relative}:{lineIndex}: {builtin} -> {_kMapBuiltinToReplacement[builtin]}  | {originalLine}")
            if bCheckUnsupported:
                for match in _kUnsupportedRe.finditer(line):
                    macro = match.group(1)
                    listViolation.append(f"{relative}:{lineIndex}: {macro} -> {_kMapUnsupportedMacroToReason[macro]}  | {originalLine}")
    return listViolation


class CheckTargetMacrosGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    ruleSchema = _kRuleSchema

    description = "플랫폼 · 아키텍처 · 컴파일러를 내장 매크로가 아니라 SW_ 매크로로 묻는지 검사"
    buildComment = "Checking that platform/architecture/compiler checks use SW_ macros..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    violationHeader = "컴파일러 내장 매크로 사용"
    hint = (
        "  플랫폼 · 아키텍처 · 컴파일러는 CMake 가 정의하는 SW_ 매크로로 묻습니다:\n"
        "      SW_PLATFORM_WINDOWS · SW_PLATFORM_LINUX · SW_X64 · SW_ARM64\n"
        "      SW_COMPILER_CLANG(clang-cl 포함) · SW_COMPILER_MSVC(cl.exe) · SW_COMPILER_GCC\n"
        f"  내장 매크로를 읽는 곳은 {_kCheckHeader} 하나뿐입니다(CMake 판정과 실제 컴파일러를 대조한다).\n"
        "  SW_PLATFORM_MACOS 는 지원하지 않는 플랫폼이라 CMake 가 정의하지 않습니다 — 그 갈래는 지웁니다."
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
            "name": "지원하지 않는 SW_PLATFORM_MACOS 갈래(Test 폴더)",
            "files": {
                _kCheckHeader: "#pragma once\n",
                "Test/Probe/ProbeWindow.cpp": (
                    "#if defined( SW_PLATFORM_WINDOWS )\n"
                    "int probe() { return 1; }\n"
                    "#elif defined( SW_PLATFORM_MACOS )\n"
                    "int probe() { return 2; }\n"
                    "#endif\n"
                ),
            },
        },
        {
            "name": "검사 헤더 안의 SW_PLATFORM_MACOS 도 예외가 아니다",
            "files": {
                _kCheckHeader: "#pragma once\n#if defined( SW_PLATFORM_MACOS )\n    #error unsupported\n#endif\n",
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
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 린트 대상 뿌리 전체)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 예외 파일이 사라지면(이름이 바뀌면) 예외도 함께 낡는다 — 조용히 넘기지 않는다.
        if not (repositoryRoot / _kCheckHeader).is_file():
            raise GateError(f"{_kCheckHeader} 가 없습니다 — 내장 매크로를 읽어도 되는 유일한 파일입니다. 옮겼다면 이 게이트의 _kCheckHeader 를 고치십시오.")
        violations = findBuiltinMacroUses(repositoryRoot, args.files)
        return GateResult(listViolation=violations, summary=f"{' · '.join(_kListScanRoot)} 의 내장 매크로 · 지원하지 않는 플랫폼 매크로")


main = CheckTargetMacrosGate.run


if __name__ == "__main__":
    sys.exit(main())
