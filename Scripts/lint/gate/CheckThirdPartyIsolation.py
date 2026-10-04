#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""감싼 서드파티(Jolt · Box2D · ACL · Recast)의 헤더는 그 백엔드 폴더에서만 include 하고, 그 라이브러리는 Engine 의 CMakeLists 에서만 링크한다.

엔진은 3D 물리 · 2D 물리 · 애니메이션 압축 · 내비메시를 **엔진 쪽 인터페이스** 뒤에 감싼다(`IPhysicsScene3D` · `IPhysicsScene2D` · 애니메이션 코덱 ·
`INavMesh` · `INavCrowd`).
라이브러리를 나중에 바꿀 수 있으려면 라이브러리 타입이 백엔드 폴더 밖으로 새지 않아야 한다 — 한 군데라도 새면 바꿀 때 그 자리가
모두 따라 바뀐다. 이 게이트가 그 경계를 지킨다.

  1) `#include <Jolt/...>` 는 `Source/Engine/Physics/Jolt/` 안에서만, `<box2d/...>` 는 `Source/Engine/Physics/Box2D/` 안에서만,
     `<acl/...>` · `<rtm/...>` 는 `Source/Engine/Animation/Codec/Acl/` 안에서만, `<recastnavigation/...>`(와 `Recast*.h` · `Detour*.h` ·
     `DebugDraw.h`)는 `Source/Engine/Navigation/Recast/` 안에서만 쓴다(시험 · 도구 · 게임도 예외 없이 인터페이스를 쓴다).
  2) 그 라이브러리 타깃(`joltphysics` · `Jolt::Jolt` · `box2d` · `box2d::box2d` · `acl` · `recastnavigation` · `RecastNavigation::*`)을 `target_link_libraries` 로 링크하는 것은
     `Source/Engine/CMakeLists.txt` 하나다 — 다른 타깃이 링크하면 헤더 경로 · 정의가 그 타깃으로 번진다. 라이브러리를 정의하는
     `ThirdParty/` 는 보지 않는다.

규칙은 아래 표(`_kListLibraryRule`) 한 곳이다. 라이브러리를 하나 더 감싸면 줄 하나를 더한다.

  python Scripts/lint/gate/CheckThirdPartyIsolation.py [--root <repo>] [--files a.cpp b/CMakeLists.txt]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import normalizePath, readTextFiles  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402


@dataclass(frozen=True)
class LibraryRule:
    """감싼 라이브러리 하나 — 헤더 접두어 · 그 헤더를 include 해도 되는 폴더 · CMake 타깃 이름."""

    name: str
    listIncludePrefix: tuple[str, ...]
    allowedRoot: str
    listCmakeTarget: tuple[str, ...]


_kListLibraryRule: tuple[LibraryRule, ...] = (
    LibraryRule("Jolt", ("Jolt/",), "Source/Engine/Physics/Jolt/", ("joltphysics", "Jolt::Jolt")),
    LibraryRule("Box2D", ("box2d/",), "Source/Engine/Physics/Box2D/", ("box2d", "box2d::box2d")),
    LibraryRule("ACL", ("acl/", "rtm/"), "Source/Engine/Animation/Codec/Acl/", ("acl",)),
    LibraryRule(
        "Recast",
        ("recastnavigation/", "Recast", "Detour", "DebugDraw.h"),
        "Source/Engine/Navigation/Recast/",
        ("recastnavigation", "RecastNavigation::Recast", "RecastNavigation::Detour", "RecastNavigation::DetourCrowd",
         "RecastNavigation::DetourTileCache", "RecastNavigation::DebugUtils"),
    ),
)

#: 감싼 라이브러리를 링크해도 되는 유일한 CMake 파일입니다.
_kCmakeLinkOwner = "Source/Engine/CMakeLists.txt"

_kListSourceRoot = ("Source", "Test", "Tools")
_kSourceSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".ipp")
_kListCmakeRoot = ("Source", "Test", "Tools", "cmake")
_kCmakeSuffixes = (".cmake",)
_kCmakeFileNames = ("CMakeLists.txt",)

_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
_kLinkCallRe = re.compile(r"\btarget_link_libraries\s*\(", re.IGNORECASE)


def findIncludeViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> list[str]:
    """백엔드 폴더 밖에서 감싼 라이브러리 헤더를 include 하는 줄입니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=_kListSourceRoot, suffixes=_kSourceSuffixes)
    listViolation: list[str] = []
    for path, text in readTextFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        for lineNumber, line in enumerate(text.splitlines(), start=1):
            match = _kIncludeRe.match(line)
            if match is None:
                continue
            includePath = normalizePath(match.group(1))
            for rule in _kListLibraryRule:
                if includePath.startswith(rule.listIncludePrefix) and relative.startswith(rule.allowedRoot) is False:
                    listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> {rule.name} 헤더는 {rule.allowedRoot} 안에서만 include 합니다")
    return listViolation


def stripCmakeComments(text: str) -> str:
    """CMake 의 `#` 줄 주석을 지웁니다(따옴표 안의 `#` 은 둔다)."""
    listLine: list[str] = []
    for line in text.splitlines():
        bInQuote = False
        cut = len(line)
        for index, character in enumerate(line):
            if character == '"':
                bInQuote = not bInQuote
            elif character == "#" and bInQuote is False:
                cut = index
                break
        listLine.append(line[:cut])
    return "\n".join(listLine)


def findLinkViolations(repositoryRoot: Path, listFileArgument: list[str] | None) -> list[str]:
    """Engine 의 CMakeLists 밖에서 감싼 라이브러리 타깃을 링크하는 호출입니다."""
    listPath = LintGate.selectTargetFiles(repositoryRoot, listFileArgument, listScanRoot=_kListCmakeRoot, suffixes=_kCmakeSuffixes,
                                          fileNames=_kCmakeFileNames)
    listViolation: list[str] = []
    for path, text in readTextFiles(listPath):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        if relative == _kCmakeLinkOwner or relative.startswith("ThirdParty/"):
            continue
        code = stripCmakeComments(text)
        for callMatch in _kLinkCallRe.finditer(code):
            # 짝이 맞는 닫는 괄호까지가 인자다.
            depth = 1
            cursor = callMatch.end()
            while cursor < len(code) and depth > 0:
                if code[cursor] == "(":
                    depth += 1
                elif code[cursor] == ")":
                    depth -= 1
                cursor += 1
            listArgument = re.split(r"\s+", code[callMatch.end():cursor - 1].strip())
            lineNumber = code.count("\n", 0, callMatch.start()) + 1
            for rule in _kListLibraryRule:
                for target in rule.listCmakeTarget:
                    if any(argument == target or argument.endswith(f":{target}>") for argument in listArgument):
                        listViolation.append(f"{relative}:{lineNumber}: target_link_libraries(... {target} ...) -> {rule.name} 는 {_kCmakeLinkOwner} 에서만 링크합니다")
    return listViolation


class CheckThirdPartyIsolationGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "감싼 서드파티(Jolt · Box2D · ACL · Recast)의 헤더 · 링크가 백엔드 폴더 · Engine CMakeLists 밖으로 새지 않는지 검사"
    buildComment = "Checking that wrapped third-party libraries stay inside their backend folders..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*", "Test/*", "Tools/*", "cmake/*", "CMakeLists.txt")
    preCommitFileArgument = "--files"
    violationHeader = "서드파티 경계 위반"
    hint = (
        "  감싼 라이브러리는 엔진 인터페이스로만 씁니다 — 물리는 Engine/Physics/IPhysicsScene.h, 애니메이션 압축은 코덱 인터페이스,\n"
        "  내비메시는 Engine/Navigation/INavMesh.h.\n"
        "  라이브러리 헤더가 필요한 코드는 백엔드 폴더(Physics/Jolt · Physics/Box2D · Animation/Codec/Acl · Navigation/Recast)로 옮기고,\n"
        f"  링크는 {_kCmakeLinkOwner} 에만 둡니다."
    )
    selfTestCases = [
        {
            "name": "Jolt 헤더를 오브젝트 층에서 include 한다",
            "files": {"Source/Engine/Object/Component/Probe.cpp": "#include <Jolt/Jolt.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "Box2D 헤더를 시험에서 include 한다",
            "files": {"Test/EngineTest/TestProbe.cpp": '#include "box2d/box2d.h"\nint probe() { return 0; }\n'},
        },
        {
            "name": "RTM 헤더를 애니메이션 코덱 폴더 밖에서 include 한다",
            "files": {"Source/Engine/Animation/Probe.h": "#pragma once\n#include <rtm/quatf.h>\n"},
        },
        {
            "name": "Detour 헤더를 게임 프레임워크에서 include 한다",
            "files": {"Source/GameFramework/AI/Probe.cpp": "#include <recastnavigation/DetourNavMeshQuery.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "Recast 헤더를 경로 없이 내비게이션 인터페이스 폴더에서 include 한다",
            "files": {"Source/Engine/Navigation/Probe.h": "#pragma once\n#include <Recast.h>\n"},
        },
        {
            "name": "Jolt 를 게임 모듈의 CMakeLists 에서 링크한다",
            "files": {"Source/Games/Probe/CMakeLists.txt": "add_library(Probe MODULE probe.cpp)\ntarget_link_libraries(Probe\n    PRIVATE\n    joltphysics\n)\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listViolation = findIncludeViolations(repositoryRoot, args.files) + findLinkViolations(repositoryRoot, args.files)
        return GateResult(listViolation=listViolation, summary="Jolt · Box2D · ACL · Recast 헤더와 링크가 제자리에 있다")


main = CheckThirdPartyIsolationGate.run

if __name__ == "__main__":
    sys.exit(main())
