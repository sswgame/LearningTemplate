#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`std::filesystem` 은 `Source/Core/File/Std/` 안에서만 쓴다 — 엔진의 파일 시스템 경계는 `FileUtil` 이다.

`std::filesystem` 은 sw 할당자를 따르지 않고(경로 하나마다 표준 할당), 예외 판 함수와 좁은 문자 경로 생성자가 오류를 예외로 던진다(Windows 는
잘못된 UTF-8 바이트에서). 그래서 엔진은 `FileUtil` 로만 파일 시스템을 보고, `std::filesystem` 은 그 구현 TU(`Core/File/Std/FileUtilStdFileSystem.cpp`)
한 곳에만 둔다 — 한 함수를 플랫폼 API 로 바꿀 때 그 정의만 옮기면 된다.

  1) `#include <filesystem>` 은 `Source/Core/File/Std/` 안에서만.
  2) `std::filesystem` · `std::experimental::filesystem` 토큰도 그 밖에서는 쓰지 않는다(다른 표준 헤더가 끌어와 include 없이 쓸 수 있다).
주석 안의 낱말은 보지 않는다.

  python Scripts/lint/gate/CheckStdFilesystemIsolation.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kLintTargetRelDirs, normalizePath  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

#: `std::filesystem` 을 써도 되는 유일한 폴더입니다.
_kStdFilesystemHome = "Source/Core/File/Std/"

_kListSourceRoot = kLintTargetRelDirs
_kSourceSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".ipp")

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"](experimental/)?filesystem[>"]')
_kTokenRe = re.compile(r"\bstd\s*::\s*(experimental\s*::\s*)?filesystem\b")
_kBlockCommentRe = re.compile(r"/\*.*?\*/", re.DOTALL)
_kLineCommentRe = re.compile(r"//[^\n]*")


def stripComments(text: str) -> str:
    """블록 · 줄 주석을 지웁니다. 줄 번호가 그대로이도록 블록 주석은 같은 수의 줄바꿈으로 바꿉니다."""
    text = _kBlockCommentRe.sub(lambda match: "\n" * match.group(0).count("\n"), text)
    return _kLineCommentRe.sub("", text)


def findViolationsInFile(relative: str, text: str) -> list[str]:
    """파일 하나의 위반 줄입니다."""
    if relative.startswith(_kStdFilesystemHome):
        return []
    listViolation: list[str] = []
    for lineNumber, line in enumerate(stripComments(text).splitlines(), start=1):
        if _kIncludeRe.match(line) is not None:
            listViolation.append(f"{relative}:{lineNumber}: <filesystem> -> {_kStdFilesystemHome} 안에서만 include 합니다")
        elif _kTokenRe.search(line) is not None:
            listViolation.append(f"{relative}:{lineNumber}: std::filesystem -> FileUtil 로 묻습니다({_kStdFilesystemHome} 밖에서는 쓰지 않습니다)")
    return listViolation


def findViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> list[str]:
    listPath = LintGate.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=_kListSourceRoot, suffixes=_kSourceSuffixes)
    listViolation: list[str] = []
    for path, text in LintGate.readFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        listViolation.extend(findViolationsInFile(relative, text))
    return listViolation


class CheckStdFilesystemIsolationGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "std::filesystem 이 Source/Core/File/Std 밖에서 쓰이지 않는지 검사"
    buildComment = "Checking that std::filesystem stays inside Core/File/Std..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*", "Test/*", "Tools/*")
    preCommitFileArgument = "--files"
    violationHeader = "std::filesystem 경계 위반"
    hint = (
        "  파일 시스템은 FileUtil 로 묻습니다 — 존재 · 크기 · 시각(getFileWriteTime · setFileWriteTime) · 순회(forEachDirectoryEntry) ·\n"
        "  만들기 · 지우기 · 복사 · 권한(setWritable) · 경로(makeAbsolutePath · makeCanonicalPath · getTempDirectory).\n"
        f"  없는 기능이 필요하면 FileUtil 에 선언하고 {_kStdFilesystemHome}FileUtilStdFileSystem.cpp 에 구현합니다."
    )
    selfTestCases = [
        {
            "name": "엔진 코드가 <filesystem> 을 include 한다",
            "files": {"Source/Engine/Resource/Probe.cpp": "#include <filesystem>\nint probe() { return 0; }\n"},
        },
        {
            "name": "시험이 include 없이 std::filesystem 으로 시각을 바꾼다",
            "files": {"Test/EngineTest/TestProbe.cpp": "void probe() { std::filesystem::last_write_time( \"a\", {} ); }\n"},
        },
        {
            "name": "도구가 이름공간 별칭으로 쓴다",
            "files": {"Tools/ReflectionParser/Probe.cpp": "namespace fs = std::filesystem;\nint probe() { return 0; }\n"},
        },
        {
            "name": "Core 의 다른 File 폴더가 쓴다",
            "files": {"Source/Core/File/Linux/Probe.cpp": "#include <experimental/filesystem>\nint probe() { return 0; }\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        return GateResult(listViolation=findViolations(repositoryRoot, args.files), summary="std::filesystem 은 Core/File/Std 안에만 있다")


main = CheckStdFilesystemIsolationGate.run

if __name__ == "__main__":
    sys.exit(main())
