#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""X11 헤더는 X11 을 직접 쓰는 `.cpp` 에서만 include 하고, 그 뒤에는 X11 매크로를 지운다.

X11 은 `Convex` · `None` · `Success` · `KeyPress` · `Always` 같은 흔한 단어를 매크로로 정의한다. X11 헤더가 공용 헤더(그리고 PCH)에
들어가면 그 매크로가 엔진의 모든 TU 로 퍼져 서드파티 헤더까지 덮는다 — `PlatformOsHeaders.h` 가 X11 을 들고 있을 때 Jolt 의
`EShapeType::Convex` 가 리눅스 빌드 다섯 잡을 모두 세웠다. Windows 빌드는 X11 을 보지 않아 이 결함을 원리상 못 낸다.

  1) X11 계열 헤더(`<X11/...>` · `<GL/glx*.h>` · `<vulkan/vulkan_xlib*.h>` · `Core/Common/X11Headers.h`)는 `.cpp` 에서만 include 한다.
     예외는 기본 묶음 `Core/Common/X11Headers.h` 하나다. 헤더가 include 하면 그 헤더를 include 하는 모든 TU 로 매크로가 번진다.
  2) X11 시스템 헤더를 직접 include 한 파일은 **마지막 X11 include 뒤에** `Core/Common/X11MacroUndef.h` 를 다시 include 한다
     (`X11Headers.h` 는 스스로 그렇게 끝난다).
  3) X11 을 include 하는 파일은 감싼 서드파티 헤더(Jolt · Box2D · ACL · RTM · Recast/Detour · Tracy)를 include 하지 않는다 —
     남은 매크로가 그 라이브러리의 식별자를 덮는다.

유니티 빌드에서 X11 `.cpp` 의 매크로가 이웃 TU 로 새지 않게 하는 것은 CMake(`sw_skipUnityForX11Sources`)가 같은 include 줄을 보고 한다.

  python Scripts/lint/gate/CheckX11Isolation.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import normalizePath, readTextFiles  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

#: X11 매크로를 정의하는 시스템 헤더의 접두어입니다.
_kListX11SystemPrefix = ("X11/", "GL/glx", "vulkan/vulkan_xlib")
#: X11 기본 묶음 — X11 시스템 헤더를 include 해도 되는 유일한 헤더이고, 스스로 매크로를 지우고 끝난다.
_kX11Wrapper = "Core/Common/X11Headers.h"
_kX11MacroUndef = "Core/Common/X11MacroUndef.h"
#: X11 을 include 하는 TU 가 함께 include 하면 안 되는 서드파티 헤더 접두어입니다.
_kListThirdPartyPrefix = ("Jolt/", "box2d/", "acl/", "rtm/", "Recast", "Detour", "DetourCrowd/", "tracy/", "Tracy")

_kListSourceRoot = ("Source", "Test", "Tools")
_kSourceSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".ipp")
_kTranslationUnitSuffixes = (".c", ".cc", ".cpp", ".cxx")

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def isX11System(includePath: str) -> bool:
    return includePath.startswith(_kListX11SystemPrefix)


def findViolationsInFile(relative: str, text: str) -> list[str]:
    """파일 하나의 위반 줄입니다."""
    listViolation: list[str] = []
    bTranslationUnit = relative.endswith(_kTranslationUnitSuffixes)
    bWrapper = relative == f"Source/{_kX11Wrapper}"
    lastRawX11Line = 0
    lastUndefLine = 0
    firstX11Line = 0
    listThirdParty: list[tuple[int, str]] = []
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        match = _kIncludeRe.match(line)
        if match is None:
            continue
        includePath = normalizePath(match.group(1))
        bRaw = isX11System(includePath)
        if bRaw or includePath == _kX11Wrapper:
            firstX11Line = firstX11Line or lineNumber
            if bTranslationUnit is False and bWrapper is False:
                listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> X11 헤더는 .cpp 에서만 include 합니다(헤더면 그 헤더를 쓰는 모든 TU 로 매크로가 번집니다)")
        if bRaw:
            lastRawX11Line = lineNumber
        elif includePath in (_kX11MacroUndef, _kX11Wrapper):
            lastUndefLine = lineNumber
        if includePath.startswith(_kListThirdPartyPrefix):
            listThirdParty.append((lineNumber, includePath))
    if lastRawX11Line > 0 and lastUndefLine < lastRawX11Line:
        listViolation.append(f"{relative}:{lastRawX11Line}: X11 헤더 뒤에 \"{_kX11MacroUndef}\" 를 다시 include 하지 않았습니다")
    if firstX11Line > 0:
        for lineNumber, includePath in listThirdParty:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> X11 을 include 하는 파일(줄 {firstX11Line})은 서드파티 헤더를 함께 include 하지 않습니다")
    return listViolation


def findViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> list[str]:
    listPath = LintGate.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=_kListSourceRoot, suffixes=_kSourceSuffixes)
    listViolation: list[str] = []
    for path, text in readTextFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        listViolation.extend(findViolationsInFile(relative, text))
    return listViolation


class CheckX11IsolationGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "X11 헤더가 .cpp 밖(공용 헤더 · PCH)으로 새지 않고, include 뒤에 X11 매크로를 지우는지 검사"
    buildComment = "Checking that X11 headers stay inside the X11 implementation files..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*", "Test/*", "Tools/*")
    preCommitFileArgument = "--files"
    violationHeader = "X11 격리 위반"
    hint = (
        f"  X11 이 필요한 .cpp 에서 \"{_kX11Wrapper}\" 를 include 합니다(공용 헤더 · PlatformOsHeaders.h 에 넣지 않는다).\n"
        f"  GLX · XKB · Xlib-xcb 처럼 더 필요한 X11 헤더는 그 뒤에 include 하고, 마지막에 \"{_kX11MacroUndef}\" 를 다시 include 합니다.\n"
        "  헤더에 X11 타입이 필요하면 void* 로 두고 .cpp 에서 바꿔 씁니다(IRenderSurface · NativeWindowEvent 처럼)."
    )
    selfTestCases = [
        {
            "name": "공용 OS 헤더가 Xlib 을 include 한다",
            "files": {"Source/Core/Common/PlatformOsHeaders.h": "#pragma once\n#include <X11/Xlib.h>\n#include \"Core/Common/X11MacroUndef.h\"\n"},
        },
        {
            "name": "백엔드 내부 헤더가 X11 묶음을 include 한다",
            "files": {"Source/Engine/Graphics/RHI/Vulkan/ProbeInternal.h": "#pragma once\n#include \"Core/Common/X11Headers.h\"\n"},
        },
        {
            "name": "Xlib-xcb 뒤에 매크로를 지우지 않는다",
            "files": {"Source/Engine/Graphics/RHI/Vulkan/Probe.cpp": "#include \"Core/Common/X11Headers.h\"\n#include <X11/Xlib-xcb.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "X11 TU 가 Jolt 헤더를 함께 include 한다",
            "files": {"Source/Engine/Window/Linux/Probe.cpp": "#include \"Core/Common/X11Headers.h\"\n#include <Jolt/Jolt.h>\nint probe() { return 0; }\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        return GateResult(listViolation=findViolations(repositoryRoot, args.files), summary="X11 헤더는 X11 구현 .cpp 안에만 있다")


main = CheckX11IsolationGate.run

if __name__ == "__main__":
    sys.exit(main())
