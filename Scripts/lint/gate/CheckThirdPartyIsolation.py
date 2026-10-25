#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""감싼 서드파티(`lint/SharedRules.py` 의 표 `kListLibraryRule`)의 헤더는 그 백엔드 · 드라이버 · 감싼 클래스 폴더에서만 include 하고, 그 라이브러리는 규칙마다 정한 CMakeLists 하나에서만 링크한다.

엔진은 3D 물리 · 2D 물리 · 애니메이션 압축 · 내비메시를 **엔진 쪽 인터페이스** 뒤에 감싼다(`IPhysicsScene3D` · `IPhysicsScene2D` · 애니메이션 코덱 ·
`INavMesh` · `INavCrowd`).
라이브러리를 나중에 바꿀 수 있으려면 라이브러리 타입이 백엔드 폴더 밖으로 새지 않아야 한다 — 한 군데라도 새면 바꿀 때 그 자리가
모두 따라 바뀐다. 이 게이트가 그 경계를 지킨다.

  1) `#include <Jolt/...>` 는 `Source/Engine/Physics/Jolt/` 안에서만, `<box2d/...>` 는 `Source/Engine/Physics/Box2D/` 안에서만,
     `<acl/...>` · `<rtm/...>` 는 `Source/Engine/Animation/Codec/ACL/` 안에서만, `<tracy/...>` 는 `Source/Engine/Profiling/Tracy/`
     안에서만, `<recastnavigation/...>`(와 `Recast*.h` · `Detour*.h` · `DebugDraw.h`)는 `Source/Engine/Navigation/Recast/` 안에서만,
     `<openssl/...>` 는 `Source/GameFramework/Base/Online/Security/OpenSSL/` 안에서만,
     `<ft2build.h>` · `<freetype/...>` 는 `Source/Engine/Text/FreeType/` 안에서만, `<sqlite3.h>` 는 키트 드라이버 폴더 `Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/Driver/SQLite/`
     안에서만, `<libpq-fe.h>` 는 서버 키트 드라이버 폴더 `Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/` 안에서만 쓴다
     (테스트 · 도구 · 게임도 예외 없이 인터페이스를 쓴다 — Tracy 는 `IProfilerBackend` · `SW_PROFILE_SCOPE`, Recast 는 `INavMesh`).
  2) 그 라이브러리 타깃(`joltphysics` · `Jolt::Jolt` · `box2d` · `box2d::box2d` · `acl` · `tracy` · `Tracy::TracyClient` · `recastnavigation` · `RecastNavigation::*` · `openssl` · `OpenSSL::SSL` · `OpenSSL::Crypto` · `freetype` · `Freetype::Freetype`)을 `target_link_libraries` 로 링크하는 것은
     규칙의 링크 주인 하나다(엔진 백엔드는 `Source/Engine/CMakeLists.txt`, 키트 안 드라이버는 그 키트의 CMakeLists) — 다른 타깃이 링크하면 헤더 경로 · 정의가 그 타깃으로 번진다. 라이브러리를 정의하는
     `ThirdParty/` 는 보지 않는다.

규칙은 표(`lint/SharedRules.py` 의 `kListLibraryRule` — 제품 이름 게이트도 읽는다) 한 곳이다 — 위 1) · 2) 의 목록은 예일 뿐, 형식(pugixml · json) · 압축(lz4 · zstd · zlib) · 셰이더(DXC) ·
그래픽 API(glad · Vulkan) · 가져오기(stb · cgltf · meshoptimizer · DirectXTex) · 에디터 UI(ImGui 일가)도 같은 표에 있다. 라이브러리를 하나 더 감싸면 줄 하나를 더한다.
링크 검사는 `target_link_libraries(` 만 읽는다 — `sw_addModuleLibrary( … LINK_PRIVATE … )` 인자로 링크한 것은 아직 보지 않는다.

  python Scripts/lint/gate/CheckThirdPartyIsolation.py [--root <repo>] [--files a.cpp b/CMakeLists.txt]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import iterIncludes  # noqa: E402
from common.Constants import kLintTargetRelDirs  # noqa: E402
from common.Paths import normalizePath  # noqa: E402
from lint.LintGate import LintGate  # noqa: E402
from lint.SharedRules import kListLibraryRule  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanSet  # noqa: E402


_kListSourceRoot = kLintTargetRelDirs
_kSourceSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx", ".ipp")
_kListCmakeRoot = kLintTargetRelDirs + ("cmake",)
_kCmakeSuffixes = (".cmake",)
_kCmakeFileNames = ("CMakeLists.txt",)

_kLinkCallRe = re.compile(r"\btarget_link_libraries\s*\(", re.IGNORECASE)


def isEditorExtensionPath(relative: str) -> bool:
    """GameFramework · 게임의 에디터 확장 폴더(`Source/GameFramework/PlugIn/Editor/` · `Source/Games/<게임>/Editor/`) 안의 파일인가."""
    return relative.startswith("Source/GameFramework/PlugIn/Editor/") or (relative.startswith("Source/Games/") and "/Editor/" in relative)


def findIncludeViolations(relative: str, text: str) -> list[str]:
    """백엔드 폴더 밖에서 감싼 라이브러리 헤더를 include 하는 줄입니다(C++ 파일 하나)."""
    listViolation: list[str] = []
    for lineNumber, rawInclude in iterIncludes(text):
        includePath = normalizePath(rawInclude)
        for rule in kListLibraryRule:
            if rule.name == "ImGui" and isEditorExtensionPath(relative):
                continue  # 키트 · 기반 · 게임의 에디터 확장(`<…>/Editor/`, Dev 전용 모듈)은 에디터와 같은 ImGui 를 쓴다
            if includePath.startswith(rule.listIncludePrefix) and relative.startswith(rule.listAllowedRoot) is False:
                listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> {rule.name} 헤더는 {' · '.join(rule.listAllowedRoot)} 안에서만 include 합니다")
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


def findLinkViolations(relative: str, text: str) -> list[str]:
    """규칙의 링크 주인 밖에서 감싼 라이브러리 타깃을 링크하는 호출입니다(CMake 파일 하나)."""
    listViolation: list[str] = []
    if relative.startswith("ThirdParty/"):
        return listViolation
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
        for rule in kListLibraryRule:
            if relative == rule.cmakeLinkOwner:
                continue
            for target in rule.listCmakeTarget:
                if any(argument == target or argument.endswith(f":{target}>") for argument in listArgument):
                    listViolation.append(f"{relative}:{lineNumber}: target_link_libraries(... {target} ...) -> {rule.name} 는 {rule.cmakeLinkOwner} 에서만 링크합니다")
    return listViolation


class CheckThirdPartyIsolationGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "감싼 서드파티(kListLibraryRule 표)의 헤더 · 링크가 백엔드 · 드라이버 폴더와 규칙의 CMakeLists 밖으로 새지 않는지 검사"
    buildComment = "Checking that wrapped third-party libraries stay inside their backend folders..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*", "Test/*", "Tools/*", "cmake/*", "CMakeLists.txt")
    preCommitFileArgument = "--files"
    violationHeader = "서드파티 경계 위반"
    hint = (
        "  감싼 라이브러리는 엔진 인터페이스로만 씁니다 — 물리는 Engine/Physics/IPhysicsScene.h, 애니메이션 압축은 코덱 인터페이스,\n"
        "  프로파일러는 SW_PROFILE_SCOPE · Engine/Profiling/IProfilerBackend.h.\n"
        "  내비메시는 Engine/Navigation/INavMesh.h.\n"
        "  암호 · TLS 는 Core/Network/Security/INetSecurityProvider.h(구현 GameFramework/Base/Online/Security/NetSecurity.h).\n"
        "  글리프 래스터화는 Engine/Text/IFontRasterizer.h(구현 Engine/Text/FreeType/).\n"
        "  SQL 은 GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h(드라이버는 Kits/Storage/SQLStore/Driver/<제품>/).\n"
        "  라이브러리 헤더가 필요한 코드는 백엔드 폴더(Physics/Jolt · Physics/Box2D · Animation/Codec/ACL · Profiling/Tracy · Navigation/Recast)로 옮기고,\n"
        "  링크는 규칙마다의 CMakeLists(엔진 백엔드는 Source/Engine/CMakeLists.txt, 키트 드라이버는 그 키트의 CMakeLists)에만 둡니다."
    )
    selfTestCases = [
        {
            "name": "pugixml 헤더를 씬에서 include 한다(XMLDocument 를 비켜 감)",
            "expectedViolation": "Source/Engine/Scene/Probe.cpp:1: <pugixml.hpp> -> pugixml 헤더는 Source/Engine/Serialization/XML/ 안에서만 include 합니다",
            "files": {"Source/Engine/Scene/Probe.cpp": "#include <pugixml.hpp>\nint probe() { return 0; }\n"},
        },
        {
            "name": "ImGui 헤더를 엔진에서 include 한다",
            "expectedViolation": "Source/Engine/Scene/ProbeGui.cpp:1: <imgui.h> -> ImGui 헤더는 Source/Editor/ · Test/EditorUITest/ 안에서만 include 합니다",
            "files": {"Source/Engine/Scene/ProbeGui.cpp": '#include "imgui.h"\nint probe() { return 0; }\n'},
        },
        {
            "name": "Jolt 헤더를 오브젝트 층에서 include 한다",
            "expectedViolation": "Source/Engine/Object/Component/Probe.cpp:1: <Jolt/Jolt.h> -> Jolt 헤더는 Source/Engine/Physics/Jolt/ 안에서만 include 합니다",
            "files": {"Source/Engine/Object/Component/Probe.cpp": "#include <Jolt/Jolt.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "Box2D 헤더를 테스트에서 include 한다",
            "expectedViolation": "Test/EngineTest/TestProbe.cpp:1: <box2d/box2d.h> -> Box2D 헤더는 Source/Engine/Physics/Box2D/ 안에서만 include 합니다",
            "files": {"Test/EngineTest/TestProbe.cpp": '#include "box2d/box2d.h"\nint probe() { return 0; }\n'},
        },
        {
            "name": "RTM 헤더를 애니메이션 코덱 폴더 밖에서 include 한다",
            "expectedViolation": "Source/Engine/Animation/Probe.h:2: <rtm/quatf.h> -> ACL 헤더는 Source/Engine/Animation/Codec/ACL/ 안에서만 include 합니다",
            "files": {"Source/Engine/Animation/Probe.h": "#pragma once\n#include <rtm/quatf.h>\n"},
        },
        {
            "name": "Tracy 헤더를 렌더러에서 include 한다",
            "expectedViolation": "Source/Engine/Renderer/Probe.cpp:1: <tracy/TracyC.h> -> Tracy 헤더는 Source/Engine/Profiling/Tracy/ 안에서만 include 합니다",
            "files": {"Source/Engine/Renderer/Probe.cpp": "#include <tracy/TracyC.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "Tracy 클라이언트를 RHI 백엔드 모듈에서 링크한다",
            "expectedViolation": "Source/Engine/Graphics/RHI/Modules/Probe/CMakeLists.txt:1: target_link_libraries(... Tracy::TracyClient ...) -> Tracy 는 Source/Engine/CMakeL",
            "files": {"Source/Engine/Graphics/RHI/Modules/Probe/CMakeLists.txt": "target_link_libraries(RHI_Probe PRIVATE Tracy::TracyClient)\n"},
        },
        {
            "name": "Detour 헤더를 게임 프레임워크에서 include 한다",
            "expectedViolation": "Source/GameFramework/Base/Actor/AI/Probe.cpp:1: <recastnavigation/DetourNavMeshQuery.h> -> Recast 헤더는 Source/Engine/Navigation/Recast/ 안에서만",
            "files": {"Source/GameFramework/Base/Actor/AI/Probe.cpp": "#include <recastnavigation/DetourNavMeshQuery.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "Recast 헤더를 경로 없이 내비게이션 인터페이스 폴더에서 include 한다",
            "expectedViolation": "Source/Engine/Navigation/Probe.h:2: <Recast.h> -> Recast 헤더는 Source/Engine/Navigation/Recast/ 안에서만 include 합니다",
            "files": {"Source/Engine/Navigation/Probe.h": "#pragma once\n#include <Recast.h>\n"},
        },
        {
            "name": "Jolt 를 게임 모듈의 CMakeLists 에서 링크한다",
            "expectedViolation": "Source/Games/Probe/CMakeLists.txt:2: target_link_libraries(... joltphysics ...) -> Jolt 는 Source/Engine/CMakeLists.txt 에서만 링크합니다",
            "files": {"Source/Games/Probe/CMakeLists.txt": "add_library(Probe MODULE probe.cpp)\ntarget_link_libraries(Probe\n    PRIVATE\n    joltphysics\n)\n"},
        },
        {
            "name": "OpenSSL 헤더를 Core 네트워크에서 include 한다",
            "expectedViolation": "Source/Core/Network/Security/Probe.cpp:1: <openssl/evp.h> -> OpenSSL 헤더는 Source/GameFramework/Base/Online/Security/OpenSSL/ 안에서만 include 합니다",
            "files": {"Source/Core/Network/Security/Probe.cpp": "#include <openssl/evp.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "OpenSSL 을 키트 CMakeLists 에서 링크한다",
            "expectedViolation": "Source/GameFramework/Kits/Feature/Online/Account/Shared/CMakeLists.txt:1: target_link_libraries(... OpenSSL::SSL ...) -> OpenSSL 는 Source/Ga",
            "files": {"Source/GameFramework/Kits/Feature/Online/Account/Shared/CMakeLists.txt": "target_link_libraries(GF_Account PRIVATE OpenSSL::SSL)\n"},
        },
        {
            "name": "FreeType 헤더를 글자 백엔드 폴더 밖(UI)에서 include 한다",
            "expectedViolation": "Source/Engine/UI/Probe.cpp:1: <freetype/freetype.h> -> FreeType 헤더는 Source/Engine/Text/FreeType/ 안에서만 include 합니다",
            "files": {"Source/Engine/UI/Probe.cpp": "#include <freetype/freetype.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "FreeType 를 에디터 CMakeLists 에서 링크한다",
            "expectedViolation": "Source/Editor/Probe/CMakeLists.txt:1: target_link_libraries(... Freetype::Freetype ...) -> FreeType 는 Source/Engine/CMakeLists.txt 에서만 링크합니다",
            "files": {"Source/Editor/Probe/CMakeLists.txt": "target_link_libraries(EditorProbe PRIVATE Freetype::Freetype)\n"},
        },
        {
            "name": "SQLite 헤더를 SQL 키트의 드라이버 폴더 밖에서 include 한다",
            "expectedViolation": "Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/Probe.cpp:1: <sqlite3.h> -> SQLite 헤더는 Source/GameFramework/Kits/Feature/Stora",
            "files": {"Source/GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/Probe.cpp": "#include <sqlite3.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "libpq 헤더를 서버 SQL 키트의 드라이버 폴더 밖에서 include 한다",
            "expectedViolation": "Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/Probe.cpp:1: <libpq-fe.h> -> PostgreSQL 헤더는 Source/GameFramework/Kits/Feature/Stor",
            "files": {"Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/Probe.cpp": "#include <libpq-fe.h>\nint probe() { return 0; }\n"},
        },
        {
            "name": "SQLite 를 서버 키트의 CMakeLists 에서 링크한다",
            "expectedViolation": "Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/CMakeLists.txt:1: target_link_libraries(... unofficial::sqlite3::sqlite3 ...) -> S",
            "files": {"Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/CMakeLists.txt": "target_link_libraries(GF_Server_SQLStore PRIVATE unofficial::sqlite3::sqlite3)\n"},
        },
        {
            # 집합 둘: `cmake/` 뿌리는 CMake 집합에만 있어 거기의 `.cmake` 는 링크로 보고, C++ 집합의 백엔드 폴더 파일은 include 가 맞다.
            "name": "cmake/ 폴더의 .cmake 가 Box2D 를 링크한다(백엔드 폴더의 Jolt include 는 위반이 아니다)",
            "expectedViolation": "cmake/Modules/Probe.cmake:1: target_link_libraries(... box2d ...) -> Box2D 는 Source/Engine/CMakeLists.txt 에서만 링크합니다",
            "unexpectedViolation": "Source/Engine/Physics/Jolt/Probe.cpp",
            "files": {
                "cmake/Modules/Probe.cmake": "target_link_libraries(Probe PRIVATE box2d)\n",
                "Source/Engine/Physics/Jolt/Probe.cpp": "#include <Jolt/Jolt.h>\nint probe() { return 0; }\n",
            },
        },
    ]

    sourceScanSets = (SourceScanSet("cpp", _kListSourceRoot, _kSourceSuffixes),
                      SourceScanSet("cmake", _kListCmakeRoot, _kCmakeSuffixes, _kCmakeFileNames))
    sourceSummary = f"{len(kListLibraryRule)} 라이브러리({' · '.join(rule.name for rule in kListLibraryRule)})의 헤더와 링크가 제자리에 있다"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        if source.setName == "cmake":
            return FileFindings(listViolation=findLinkViolations(source.relative, source.text))
        return FileFindings(listViolation=findIncludeViolations(source.relative, source.text))


main = CheckThirdPartyIsolationGate.run

if __name__ == "__main__":
    sys.exit(main())
