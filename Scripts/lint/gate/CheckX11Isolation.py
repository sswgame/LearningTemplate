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

  4) 서버 빌드에도 들어가는 폴더(목록이 없다 — 모듈 매니페스트 `_listTarget` 과 `Source/<X>` · `Test/<X>` CMakeLists 의 서버 가드에서 읽는다, `common/ServerBuild.py`.
     소속을 알 수 없는 `Source/<X>` 폴더는 게이트가 실패한다. RHI 백엔드 폴더 `Graphics/RHI` 는 제외)에서 X11 · Wayland · xkbcommon · xcb 헤더를 include 하는 `.cpp` 는
     `SW_WITH_CLIENT_CODE` 로 감싼다. 전용 서버(`SW_TARGET_TYPE=Server`)는 X11 을 링크하지 않아(`Linux.cmake`) 감싸지 않은 .cpp 는 서버 빌드의
     컴파일이나 링크를 세운다. Windows 빌드는 이 결함을 낼 수 없어 소스 모양으로 막는다(계획 DedicatedServerNoRenderer S3).

유니티 빌드에서 X11 `.cpp` 의 매크로가 이웃 TU 로 새지 않게 하는 것은 CMake(`sw_skipUnityForX11Sources`)가 같은 include 줄을 보고 한다.

  python Scripts/lint/gate/CheckX11Isolation.py [--root <repo>] [--files a.cpp b.h]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import iterIncludes  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from common.Paths import normalizePath  # noqa: E402
from common.ServerBuild import ServerBuildError, ServerBuildMap, readServerBuildMap  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult  # noqa: E402

#: X11 매크로를 정의하는 시스템 헤더의 접두어입니다.
_kListX11SystemPrefix = ("X11/", "GL/glx", "vulkan/vulkan_xlib")
#: X11 기본 묶음 — X11 시스템 헤더를 include 해도 되는 유일한 헤더이고, 스스로 매크로를 지우고 끝난다.
_kX11Wrapper = "Core/Common/X11Headers.h"
_kX11MacroUndef = "Core/Common/X11MacroUndef.h"
#: X11 을 include 하는 TU 가 함께 include 하면 안 되는 서드파티 헤더 접두어입니다.
_kListThirdPartyPrefix = ("Jolt/", "box2d/", "acl/", "rtm/", "Recast", "Detour", "DetourCrowd/", "tracy/", "Tracy")

#: 서버에 없는 창 계층의 시스템 헤더 접두어입니다(4번 규칙). X11 매크로 규칙(1~3)은 `_kListX11SystemPrefix` 만 본다.
_kListWindowingSystemPrefix = _kListX11SystemPrefix + ("wayland-", "xkbcommon/", "xcb/", "xdg-shell")
_kClientGuardMacro = "SW_WITH_CLIENT_CODE"
#: 서버 빌드가 짓는 폴더는 목록으로 적지 않고 매니페스트 `_listTarget` 과 폴더 CMakeLists 가드에서 읽는다(`common/ServerBuild.py`).
#: 소속을 알 수 없는 `Source/<X>/` 폴더가 생기면 4번 규칙이 그 폴더를 못 보게 되므로 게이트가 스스로 실패한다.
#: RHI 백엔드는 모듈(Dev) 또는 서버에서 빠지는 폴더(`Source/Engine/CMakeLists.txt` 의 `sw_getAllRhiBackendSources`)라 4번 규칙에서 뺀다.
_kEngineBackendDir = "Source/Engine/Graphics/RHI/"

_kListSourceRoot = kLintTargetRelDirs
_kSourceSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".ipp")
_kTranslationUnitSuffixes = (".c", ".cc", ".cpp", ".cxx")

# 자체 테스트용 CMakeLists · 매니페스트 조각 — 서버 빌드 소속은 이 파일들에서 읽힌다.
_kStubCMake = "add_library(Probe probe.cpp)\n"
_kServerGuardedCMake = 'if(SW_TARGET_TYPE STREQUAL "Server")\n\tsw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")\n\treturn()\nendif()\n'


def _manifest(*targets: str) -> str:
    return '{ "_name": "Probe", "_kind": "Module", "_listTarget": [' + ", ".join(f'"{target}"' for target in targets) + "] }\n"


def _withBuild(files: dict[str, str]) -> dict[str, str]:
    """`Source/<X>` · `Test/<X>` 마다 가드 없는 CMakeLists(= 서버도 짓는다)를 더합니다."""
    stub = {"/".join(path.split("/")[:2]) + "/CMakeLists.txt": _kStubCMake for path in files}
    return {**stub, **files}


def isX11System(includePath: str) -> bool:
    return includePath.startswith(_kListX11SystemPrefix)


def findViolationsInFile(relative: str, text: str, bServerBuilt: bool) -> list[str]:
    """파일 하나의 위반 줄입니다. `bServerBuilt` 는 이 파일의 폴더를 전용 서버 빌드가 짓는지(4번 규칙)입니다."""
    listViolation: list[str] = []
    bTranslationUnit = relative.endswith(_kTranslationUnitSuffixes)
    bWrapper = relative == f"Source/{_kX11Wrapper}"
    lastRawX11Line = 0
    lastUndefLine = 0
    firstX11Line = 0
    listThirdParty: list[tuple[int, str]] = []
    for lineNumber, rawInclude in iterIncludes(text):
        includePath = normalizePath(rawInclude)
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
    if bTranslationUnit and bServerBuilt and relative.startswith(_kEngineBackendDir) is False and _kClientGuardMacro not in text:
        for lineNumber, rawInclude in iterIncludes(text):
            includePath = normalizePath(rawInclude)
            if includePath.startswith(_kListWindowingSystemPrefix):
                listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 창 계층 헤더를 include 하는 서버 빌드 .cpp 는 {_kClientGuardMacro} 로 감쌉니다(전용 서버는 X11 을 링크하지 않습니다)")
                break
    if lastRawX11Line > 0 and lastUndefLine < lastRawX11Line:
        listViolation.append(f"{relative}:{lastRawX11Line}: X11 헤더 뒤에 \"{_kX11MacroUndef}\" 를 다시 include 하지 않았습니다")
    if firstX11Line > 0:
        for lineNumber, includePath in listThirdParty:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> X11 을 include 하는 파일(줄 {firstX11Line})은 서드파티 헤더를 함께 include 하지 않습니다")
    return listViolation


class CheckX11IsolationGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "X11 헤더가 .cpp 밖(공용 헤더 · PCH)으로 새지 않고, include 뒤에 X11 매크로를 지우는지 검사"
    buildComment = "Checking that X11 headers stay inside the X11 implementation files..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*", "Test/*", "Tools/*")
    preCommitFileArgument = "--files"
    sourceScanRoots = _kListSourceRoot
    sourceSuffixes = _kSourceSuffixes
    sourceSummary = "X11 헤더는 X11 구현 .cpp 안에만 있다"
    serverBuildMap: ServerBuildMap
    violationHeader = "X11 격리 위반"
    hint = (
        f"  X11 이 필요한 .cpp 에서 \"{_kX11Wrapper}\" 를 include 합니다(공용 헤더 · PlatformOsHeaders.h 에 넣지 않는다).\n"
        f"  GLX · XKB · Xlib-xcb 처럼 더 필요한 X11 헤더는 그 뒤에 include 하고, 마지막에 \"{_kX11MacroUndef}\" 를 다시 include 합니다.\n"
        "  헤더에 X11 타입이 필요하면 void* 로 두고 .cpp 에서 바꿔 씁니다(IRenderSurface · NativeWindowEvent 처럼)."
    )
    selfTestCases = [
        {
            "name": "공용 OS 헤더가 Xlib 을 include 한다",
            "expectedViolation": "Source/Core/Common/PlatformOsHeaders.h:2: <X11/Xlib.h> -> X11 헤더는 .cpp 에서만 include 합니다(헤더면 그 헤더를 쓰는 모든 TU 로 매크로가 번집니다)",
            "files": _withBuild({"Source/Core/Common/PlatformOsHeaders.h": "#pragma once\n#include <X11/Xlib.h>\n#include \"Core/Common/X11MacroUndef.h\"\n"}),
        },
        {
            "name": "백엔드 내부 헤더가 X11 묶음을 include 한다",
            "expectedViolation": "Source/Engine/Graphics/RHI/Vulkan/ProbeInternal.h:2: <Core/Common/X11Headers.h> -> X11 헤더는 .cpp 에서만 include 합니다(헤더면 그 헤더를 쓰는 모든 TU 로 매크로가 번집",
            "files": _withBuild({"Source/Engine/Graphics/RHI/Vulkan/ProbeInternal.h": "#pragma once\n#include \"Core/Common/X11Headers.h\"\n"}),
        },
        {
            "name": "Xlib-xcb 뒤에 매크로를 지우지 않는다",
            "expectedViolation": "Source/Engine/Graphics/RHI/Vulkan/Probe.cpp:2: X11 헤더 뒤에 \"Core/Common/X11MacroUndef.h\" 를 다시 include 하지 않았습니다",
            "files": _withBuild({"Source/Engine/Graphics/RHI/Vulkan/Probe.cpp": "#include \"Core/Common/X11Headers.h\"\n#include <X11/Xlib-xcb.h>\nint probe() { return 0; }\n"}),
        },
        {
            "name": "X11 TU 가 Jolt 헤더를 함께 include 한다",
            "expectedViolation": "Source/Engine/Window/Linux/Probe.cpp:2: <Jolt/Jolt.h> -> X11 을 include 하는 파일(줄 1)은 서드파티 헤더를 함께 include 하지 않습니다",
            "files": _withBuild({"Source/Engine/Window/Linux/Probe.cpp": "#include \"Core/Common/X11Headers.h\"\n#include <Jolt/Jolt.h>\nint probe() { return 0; }\n"}),
        },
        {
            "name": "서버에도 들어가는 엔진 테스트 .cpp 가 xcb 헤더를 감싸지 않고 include 한다",
            "expectedViolation": "Test/EngineTest/Window/Probe.cpp:1: <xcb/xcb.h> -> 창 계층 헤더를 include 하는 서버 빌드 .cpp 는 SW_WITH_CLIENT_CODE 로 감쌉니다",
            "files": _withBuild({"Test/EngineTest/Window/Probe.cpp": "#include <xcb/xcb.h>\nint probe() { return 0; }\n"}),
        },
        {
            "name": "서버에도 들어가는 GameFramework .cpp 가 xkbcommon 헤더를 감싸지 않고 include 한다",
            "expectedViolation": "Source/GameFramework/Base/Probe.cpp:1: <xkbcommon/xkbcommon.h> -> 창 계층 헤더를 include 하는 서버 빌드 .cpp 는 SW_WITH_CLIENT_CODE 로 감쌉니다",
            "files": {
                "Source/GameFramework/GameFramework.module.json": _manifest("Client", "Server"),
                "Source/GameFramework/Base/Probe.cpp": "#include <xkbcommon/xkbcommon.h>\nint probe() { return 0; }\n",
            },
        },
        {
            "name": "_listTarget 이 Client 뿐인 모듈 폴더는 서버 빌드 밖이라 넘기고, 서버 모듈 폴더는 잡는다",
            "expectedViolation": "Source/GameFramework/Server/ServerProbe.cpp:1: <xcb/xcb.h> -> 창 계층 헤더를 include 하는 서버 빌드 .cpp 는 SW_WITH_CLIENT_CODE 로 감쌉니다",
            "unexpectedViolation": "Source/Editor/EditorProbe.cpp",
            "files": {
                "Source/Editor/EditorModule.module.json": _manifest("Client"),
                "Source/Editor/EditorProbe.cpp": "#include <xcb/xcb.h>\nint probe() { return 0; }\n",
                "Source/GameFramework/GameFramework.module.json": _manifest("Client", "Server"),
                "Source/GameFramework/Server/ServerProbe.cpp": "#include <xcb/xcb.h>\nint probe() { return 0; }\n",
            },
        },
        {
            "name": "서버 가드로 짓지 않는다고 적은 테스트 폴더는 넘기고, 가드 없는 테스트 폴더는 잡는다",
            "expectedViolation": "Test/SmokeTest/Probe.cpp:1: <xcb/xcb.h> -> 창 계층 헤더를 include 하는 서버 빌드 .cpp 는 SW_WITH_CLIENT_CODE 로 감쌉니다",
            "unexpectedViolation": "Test/EditorTest/Probe.cpp",
            "files": {
                "Test/EditorTest/CMakeLists.txt": _kServerGuardedCMake,
                "Test/EditorTest/Probe.cpp": "#include <xcb/xcb.h>\nint probe() { return 0; }\n",
                "Test/SmokeTest/CMakeLists.txt": _kStubCMake,
                "Test/SmokeTest/Probe.cpp": "#include <xcb/xcb.h>\nint probe() { return 0; }\n",
            },
        },
        {
            "name": "CMakeLists 도 매니페스트도 없는 새 Source 폴더는 서버 빌드 소속을 몰라 게이트가 실패한다",
            "expectedViolation": "Source/NewFeature/: 서버 빌드 소속을 알 수 없습니다",
            "files": {"Source/NewFeature/Probe.cpp": "int probe() { return 0; }\n"},
        },
        {
            "name": "서버에도 들어가는 엔진 .cpp 가 Wayland 헤더를 감싸지 않고 include 한다",
            "expectedViolation": "Source/Engine/Window/Linux/Probe.cpp:1: <wayland-client.h> -> 창 계층 헤더를 include 하는 서버 빌드 .cpp 는 SW_WITH_CLIENT_CODE 로 감쌉니다",
            "files": _withBuild({"Source/Engine/Window/Linux/Probe.cpp": "#include <wayland-client.h>\nint probe() { return 0; }\n"}),
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def prepareSourceScan(self, repositoryRoot: Path, args: argparse.Namespace) -> None:
        try:
            self.serverBuildMap = readServerBuildMap(repositoryRoot)
        except ServerBuildError as error:
            raise GateError(str(error)) from error

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        return FileFindings(listViolation=findViolationsInFile(source.relative, source.text, self.serverBuildMap.isServerBuilt(source.relative)))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        gateResult = super().finishSourceScan(repositoryRoot, args, result)
        gateResult.listViolation.extend(
            f"{folder}: 서버 빌드 소속을 알 수 없습니다 — 폴더에 CMakeLists.txt(가드 `if(SW_TARGET_TYPE STREQUAL \"Server\") ... return()` 는 서버 밖) 또는 모듈 매니페스트(`_listTarget`)가 없어 4번 규칙이 이 폴더를 못 봅니다"
            for folder in self.serverBuildMap.listUnclassified)
        return gateResult


main = CheckX11IsolationGate.run

if __name__ == "__main__":
    sys.exit(main())
