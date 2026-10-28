#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`Source/` · `Test/` · `Tools/` 가 환경 변수를 `getenv` · `GetEnvironmentVariable` 로 직접 읽는 곳을 잡는다 — 읽기는 `Core/Process/ProcessEnvironment.h` 하나다.

환경 변수에는 없음 · 빈 값 · 값 있음 세 상태가 있는데, 직접 읽으면 빈 값을 없음으로 읽는 곳과 있음으로 읽는 곳이 갈린다(빈 `HOME` 으로 상대 경로를 만드는 따위).
대신 쓸 것(같은 헤더):

    ProcessEnvironment::tryRead( name, out )           정해져 있으면 true — 빈 값도 정해진 값이다
    ProcessEnvironment::tryReadNonEmpty( name, out )   값이 있을 때만 true — 빈 값은 없음과 같다

`getenv` · `secure_getenv` · `GetEnvironmentVariable(A|W)` 이름 자체를 막는다. 주석 · 문자열 안의 언급은 보지 않는다.
예외는 `Scripts/lint/rules/CheckProcessEnvironment.toml` 의 `[exemption]` 한 곳이다. 예외 파일이 더는 읽지 않으면 그 줄은 낡은 예외로 실패한다(`LintGate` 기반이 본다).

  python Scripts/lint/gate/CheckProcessEnvironment.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import findFast  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile  # noqa: E402

_kListScanRoot = kLintTargetRelDirs
_kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".tpl")

_kEnvironmentReadRe = re.compile(r"(?<![\w$])(getenv|secure_getenv|GetEnvironmentVariable[AW]?)(?![\w$])")


#: 엔진의 사용자 데이터 · 설정 폴더를 정하는 OS 변수 이름의 글. 이 폴더는 `UserDataPath` 한 곳이 계산한다(자동화가 `SW_USER_DATA_ROOT` 로 실행별 폴더로 가르는 자리이기도 하다 —
#: 다른 곳이 이 변수로 사용자 폴더를 직접 만들면 그 격리를 비껴 간다). 큰따옴표 안의 이름 그대로만 본다(`"HOME"`) - 주석 · 긴 글 속 언급은 오탐이라 보지 않는다.
_kUserFolderVariableRe = re.compile(r'"(LOCALAPPDATA|APPDATA|XDG_CONFIG_HOME|XDG_DATA_HOME|XDG_CACHE_HOME|USERPROFILE|HOME)"')
#: 사용자 폴더 변수 예외 키의 앞말 — `[exemption]` 에 `user-folder:<경로>` 로 적는다.
_kUserFolderKeyPrefix = "user-folder:"


def findUserFolderVariableUses(source: SourceFile) -> list[str]:
    """`UserDataPath` 밖에서 사용자 폴더 변수 이름을 글자로 쓰는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 쓰면 `use` 로 적습니다."""
    gate = CheckProcessEnvironmentGate
    key = _kUserFolderKeyPrefix + source.relative
    bExempt = key in gate.mapExemption
    listMatch: list[tuple[int, str]] = []
    for lineIndex, line in enumerate(source.listLine, start=1):
        code = line.split("//", 1)[0]
        if code.lstrip().startswith(("*", "/*")):
            continue
        for match in _kUserFolderVariableRe.finditer(code):
            listMatch.append((lineIndex, match.group(1)))
    if bExempt:
        gate.seeExemption(key)
        if listMatch:
            gate.useExemption(key)
        return []
    return [f"{source.relative}:{lineIndex}: 사용자 폴더 변수 \"{name}\"  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, name in listMatch]


def findEnvironmentReads(source: SourceFile) -> list[str]:
    """예외 밖에서 환경 변수를 직접 읽는 줄을 위반 문자열로 돌려줍니다. 예외 파일은 보면 `see`, 읽으면 `use` 로 적습니다."""
    gate = CheckProcessEnvironmentGate
    relative = source.relative
    bExempt = relative in gate.mapExemption
    listMatch: list[tuple[int, str]] = []
    if findFast(_kEnvironmentReadRe, source.text) is not None:
        listMatch = [(lineIndex, match.group(1))
                     for lineIndex, line in enumerate(source.listMaskedLine, start=1)
                     for match in _kEnvironmentReadRe.finditer(line)]
    if bExempt:
        gate.seeExemption(relative)
        if listMatch:
            gate.useExemption(relative)
        return []
    return [f"{relative}:{lineIndex}: {name}  | {source.listLine[lineIndex - 1].strip()}" for lineIndex, name in listMatch]


class CheckProcessEnvironmentGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있다."""

    description = "엔진 · 테스트 · 도구 코드가 환경 변수를 getenv 가 아니라 ProcessEnvironment 로 읽는지 검사"
    buildComment = "Checking that code reads environment variables through ProcessEnvironment..."
    timeoutSeconds = 30
    preCommitPattern = tuple(f"{root}/*" for root in _kListScanRoot)
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListScanRoot
    sourceSuffixes = _kSuffixes
    sourceSummary = f"{' · '.join(_kListScanRoot)} 의 환경 변수 직접 읽기"
    violationHeader = "환경 변수 직접 읽기"
    hint = (
        "  환경 변수 읽기는 Source/Core/Process/ProcessEnvironment.h 하나입니다:\n"
        "      빈 값도 정해진 값    ProcessEnvironment::tryRead( name, out )\n"
        "      빈 값은 없음과 같다  ProcessEnvironment::tryReadNonEmpty( name, out )\n"
        "  정말 직접 읽어야 하면 Scripts/lint/rules/CheckProcessEnvironment.toml 의 [exemption] 에 이유와 함께 적습니다."
    )
    selfTestCases = [
        {
            "name": "엔진 코드가 std::getenv 를 부른다",
            "expectedViolation": "Source/Probe/ProbeHome.cpp:2: getenv",
            "files": {
                "Source/Probe/ProbeHome.cpp": (
                    "#include <cstdlib>\n"
                    "const char* probe() { return std::getenv( \"HOME\" ); }\n"
                ),
            },
        },
        {
            "name": "테스트가 getenv 로 자식 역할 표식을 읽는다(Test 폴더)",
            "expectedViolation": "Test/Probe/ProbeChild.cpp:2: getenv",
            "files": {
                "Test/Probe/ProbeChild.cpp": (
                    "#include <cstdlib>\n"
                    "bool probe() { return std::getenv( \"SW_PROBE_CHILD\" ) != nullptr; }\n"
                ),
            },
        },
        {
            "name": "Windows 코드가 GetEnvironmentVariableW 를 부른다",
            "expectedViolation": "Source/Probe/ProbeWin.cpp:2: GetEnvironmentVariableW",
            "files": {
                "Source/Probe/ProbeWin.cpp": (
                    "// getenv 는 주석 안이라 읽기가 아니다\n"
                    "unsigned probe( wchar_t* p ) { return GetEnvironmentVariableW( L\"X\", p, 8 ); }\n"
                ),
            },
        },
        {
            "name": "엔진 코드가 UserDataPath 를 비껴 사용자 폴더 변수로 경로를 만든다",
            "expectedViolation": "Source/Probe/ProbeUserFolder.cpp:3: 사용자 폴더 변수 \"LOCALAPPDATA\"",
            "files": {
                "Source/Probe/ProbeUserFolder.cpp": (
                    "// \"HOME\" 은 주석 안이라 위반이 아니다\n"
                    "/** \"APPDATA\" 도 문서 주석 안이다 */\n"
                    "bool probe( string& out ) { return ProcessEnvironment::tryReadNonEmpty( \"LOCALAPPDATA\", out ); }\n"
                ),
            },
        },
        {
            "name": "환경 변수를 더는 읽지 않는 예외 파일(낡은 예외)",
            "expectedViolation": "[낡은 예외] 'Source/Core/Process/Posix/PosixProcessEnvironment.cpp' 의 대상은 그대로인데 규칙을 어기지 않습니다",
            "files": {
                "Source/Core/Process/Posix/PosixProcessEnvironment.cpp": "// getenv 는 주석 안이라 읽기가 아니다\nint probe() { return 0; }\n",
                "Source/Core/Process/Windows/WindowsProcessEnvironment.cpp": "int probe() { return GetEnvironmentVariableW( 0, 0, 0 ); }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 특정 파일 (생략 시 린트 대상 뿌리 전체)")

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=[*findEnvironmentReads(source), *findUserFolderVariableUses(source)])


main = CheckProcessEnvironmentGate.run


if __name__ == "__main__":
    sys.exit(main())
