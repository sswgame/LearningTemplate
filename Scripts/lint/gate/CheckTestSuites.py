#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
테스트 스위트 규칙 검사.

스위트 이름은 장식이 아니다. CI 가 못 돌리는 것은 **스위트 단위로** 선언되어 빠지고(`SW_TEST_REQUIRES_HOST`),
`--test_filter` 도 스위트 단위로 고른다. 그래서 이름이 흔들리면 그 둘이 흔들린다.

강제 규칙 여섯:

  1) 스위트 이름은 `XxxTest` — 대문자로 시작하고 `Test` 로 끝나며 밑줄이 없다.
     관례가 섞이면 필터가 흔들린다 — `Core` 라는 스위트가 있으면 `--test_filter=Core*` 가 `Core_String` 까지 끌어온다.
     계층 접두어(`Core_` · `Engine_`)는 **실행 파일 이름이 이미 말한다** — CoreTest.exe 안의 `Core_String` 은
     같은 말을 두 번 하고, 파일이 옮겨지면 접두어가 거짓말이 된다(`Engine_Event` 가 CoreTest 에 있는 식).

  2) 한 스위트는 **한 파일에만** 산다. 갈라져 있으면 "이 스위트를 고치려면 어디를 여나" 에 답이 둘이 된다.

  3) CI 가 못 돌리는 스위트는 자기 파일에서 `SW_TEST_REQUIRES_HOST( 스위트, "이유" );` 로 선언한다.
     **선언이 곧 분류다** — 테스트 실행 파일이 그 선언을 들고 있다가 `--host_suites=exclude|only` 로 가르고,
     `sw_addTestExecutable( ... HOST_SPLIT )` 가 그 둘을 `<타깃>_NoGPU` · `<타깃>_HostOnly` 로 등록한다.
     이 검사가 보는 것은 선언이 **실제로 효력이 있는가** 다:
       - 선언한 스위트가 그 파일에 있는가(이름을 바꾸고 선언을 놓치면 그 스위트가 CI 로 들어간다).
       - 그 폴더의 실행 파일이 `HOST_SPLIT` 으로 등록되는가(아니면 선언을 아무도 읽지 않는다 — CI 가 그것을 돈다).
       - 거꾸로 `HOST_SPLIT` 인데 선언이 하나도 없으면 `_HostOnly` 는 빈 그물이다.
       - 주석 형태의 마커(`// SW_TEST_REQUIRES_HOST( X ): ...`)는 아무 효력이 없으므로 있으면 실패다.

  4) 마커가 붙은 스위트가 있는 파일에는 **다른 스위트를 두지 않는다.**
     호스트 스위트와 CI 스위트가 한 파일에 섞여 있으면 새 케이스를 옆 스위트에 붙이기가 너무 쉽다 —
     그 순간 GPU 가 필요한 케이스가 CI 로 들어간다.

  5) `EditorTest` 가 끌어오는 Editor 소스는 **존재해야 하고, ImGui 헤더를 include 하지 않아야 한다.**
     `EditorModule` 은 MODULE DLL 이라 링크할 수 없어서, 테스트가 필요한 `.cpp` 만 손으로 나열해 같이
     컴파일한다. 그 목록이 "Editor/Common 전부" 도 "ImGui 안 쓰는 것 전부" 도 아니라 **테스트가 실제로
     링크해야 하는 것** 이라 기계가 대신 고를 수 없다. 그래서 목록 자체는 손으로 두되, 썩는 두 방향만 막는다:
     파일이 옮겨져 경로가 죽는 것과, ImGui 를 타는 파일이 섞여 들어오는 것
     (EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).

  6) `CoreTest` 는 **엔진 없이** Core 를 시험한다 — `Test/CoreTest` 의 파일은 Engine · GameFramework · Editor · Games · App 헤더를
     include 하지 않고, 코드에서 `engine::` 를 부르지 않는다(주석은 보지 않는다). 엔진 타입이 필요한 시험은 EngineTest 에 둔다.
     include 경로로는 막을 수 없다: 공용 `TestFramework` 가 Engine 을 PUBLIC 링크하고 `TestFramework.h` 가 `EngineMinimal.h` 를
     끌어온다. 그래서 이 규칙이 보는 것은 CoreTest 파일이 **직접** 쓰는 것이다.

  python Scripts/lint/gate/CheckTestSuites.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

_kTestRoot = "Test"
_kEditorTestCMake = "Test/EditorTest/CMakeLists.txt"

# 줄 맨 앞만 보면 안 된다 — `namespace sw::editor { ... }` 안에 들여쓴 케이스가 있으면 그 파일이 검사에서
# 통째로 빠진다.
_kCaseRe = re.compile(r"^[ 	]*SW_TEST_CASE\(\s*(\w+)\s*,\s*(\w+)\s*\)", re.M)
# 위 정규식이 놓치는 표기가 생기면 조용히 줄어들 뿐이라, 토큰을 따로 세어 대조한다.
_kCaseTokenRe = re.compile(r"\bSW_TEST_CASE\s*\(")
_kSuiteNameRe = re.compile(r"^[A-Z][A-Za-z0-9]*Test$")
_kMarkerRe = re.compile(r'^[ \t]*SW_TEST_REQUIRES_HOST\(\s*(\w+)\s*,\s*"([^"]*)"\s*\)\s*;', re.M)
# 선언도 케이스처럼 토큰을 따로 세어 대조한다 - 여러 줄로 쪼갠 표기를 놓치면 그 스위트가 조용히 CI 로 간다.
_kMarkerTokenRe = re.compile(r"^[ \t]*SW_TEST_REQUIRES_HOST\s*\(", re.M)
_kLegacyMarkerRe = re.compile(r"//\s*SW_TEST_REQUIRES_HOST\(\s*(\w+)\s*\)\s*:")
_kHostSplitRe = re.compile(r"\bHOST_SPLIT\b")
_kEditorSourceRe = re.compile(r"\$\{CMAKE_SOURCE_DIR\}/(Source/Editor/[\w/]+\.cpp)")
_kImGuiIncludeRe = re.compile(r"^\s*#\s*include\s*[<\"][^>\"]*imgui[^>\"]*[>\"]", re.I | re.M)
_kCoreTestFolder = "Test/CoreTest"
_kCoreTestForbiddenIncludeRe = re.compile(r"^[ \t]*#[ \t]*include[ \t]*[<\"]((?:Engine|GameFramework|Editor|Games|App)/[^>\"]*)[>\"]", re.M)
_kEngineNamespaceRe = re.compile(r"\bengine::\w+")
_kBlockCommentRe = re.compile(r"/\*.*?\*/", re.S)
_kLineCommentRe = re.compile(r"//[^\n]*")


def collectCases(rootDir: Path) -> tuple[list[tuple[str, str, str]], list[str]]:
    """(스위트, 케이스, 저장소 상대 경로) 전부와, 파싱이 놓친 자리."""
    out: list[tuple[str, str, str]] = []
    missed: list[str] = []
    for path in sorted((rootDir / _kTestRoot).rglob("*.cpp")):
        relPath = path.relative_to(rootDir).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        parsed = list(_kCaseRe.finditer(text))
        out += [(m.group(1), m.group(2), relPath) for m in parsed]
        tokenCount = len(_kCaseTokenRe.findall(text))
        if tokenCount != len(parsed):
            missed.append(f"{relPath}: SW_TEST_CASE 를 {tokenCount}개 썼는데 {len(parsed)}개만 읽혔습니다 "
                          f"— 이 검사가 그 파일의 스위트를 놓치고 있습니다 (표기를 맞추거나 파서를 고치세요)")
    return out, missed


def collectMarkers(rootDir: Path) -> tuple[dict[str, tuple[str, str]], list[str]]:
    """(스위트 -> (파일, 이유), 오류들). 선언은 그 스위트가 사는 파일에 둔다."""
    out: dict[str, tuple[str, str]] = {}
    errors: list[str] = []
    for path in sorted((rootDir / _kTestRoot).rglob("*.cpp")):
        relPath = path.relative_to(rootDir).as_posix()
        text = path.read_text(encoding="utf-8", errors="ignore")
        parsed = list(_kMarkerRe.finditer(text))
        for match in parsed:
            out[match.group(1)] = (relPath, match.group(2).strip())
        tokenCount = len(_kMarkerTokenRe.findall(text))
        if tokenCount != len(parsed):
            errors.append(f"{relPath}: SW_TEST_REQUIRES_HOST 를 {tokenCount}개 썼는데 {len(parsed)}개만 읽혔습니다 "
                          f"— 한 줄에 `SW_TEST_REQUIRES_HOST( 스위트, \"이유\" );` 로 쓰세요")
        for legacy in _kLegacyMarkerRe.finditer(text):
            errors.append(f"{relPath}: 옛 주석 마커 `// SW_TEST_REQUIRES_HOST( {legacy.group(1)} ): ...` 는 아무 효력이 "
                          f"없습니다 — `SW_TEST_REQUIRES_HOST( {legacy.group(1)}, \"이유\" );` 로 선언하세요")
    return out, errors


def collectHostSplitFolders(rootDir: Path) -> set[str]:
    """`HOST_SPLIT` 으로 등록하는 테스트 폴더(`Test/<폴더>`). 주석에 적힌 낱말은 세지 않는다."""
    out: set[str] = set()
    for path in sorted((rootDir / _kTestRoot).glob("*/CMakeLists.txt")):
        code = "\n".join(line.split("#", 1)[0] for line in path.read_text(encoding="utf-8", errors="ignore").splitlines())
        if _kHostSplitRe.search(code) is not None:
            out.add(path.parent.relative_to(rootDir).as_posix())
    return out


def testFolderOf(relPath: str) -> str:
    """`Test/EngineTest/TestRHIDevice.cpp` -> `Test/EngineTest`. 그 폴더의 CMakeLists 가 실행 파일 하나를 만든다."""
    return "/".join(relPath.split("/")[:2])


def checkEditorTestSources(rootDir: Path) -> list[str]:
    """EditorTest 가 나열한 Editor 소스가 살아 있고 ImGui 를 타지 않는지 봅니다."""
    path = rootDir / _kEditorTestCMake
    if path.exists() is False:
        return [f"{_kEditorTestCMake}: 파일이 없습니다"]
    listed = _kEditorSourceRe.findall(path.read_text(encoding="utf-8", errors="ignore"))
    if not listed:
        return [f"{_kEditorTestCMake}: Editor 소스 항목을 하나도 찾지 못했습니다 (검사가 헛돌고 있습니다)"]

    errors: list[str] = []
    for relPath in listed:
        sourcePath = rootDir / relPath
        if sourcePath.exists() is False:
            errors.append(f"{_kEditorTestCMake}: `{relPath}` 가 없습니다 — 파일이 옮겨졌으면 경로를 고치세요")
            continue
        for candidate in (sourcePath, sourcePath.with_suffix(".h")):
            if candidate.exists() is False:
                continue
            match = _kImGuiIncludeRe.search(candidate.read_text(encoding="utf-8", errors="ignore"))
            if match is not None:
                errors.append(
                    f"{candidate.relative_to(rootDir).as_posix()}: EditorTest 가 이 파일을 컴파일하는데 "
                    f"ImGui 를 include 합니다 (`{match.group(0).strip()}`) — EditorTest 는 ImGui 를 링크하지 "
                    f"않습니다. 테스트가 쓰는 부분을 ImGui 없는 TU 로 가르세요"
                )
    return errors


def stripCommentsInternal(text: str) -> str:
    """블록 · 줄 주석을 지운다. 블록 주석 안의 줄바꿈은 남겨 위반 줄 번호가 원문과 맞는다."""
    text = _kBlockCommentRe.sub(lambda match: "\n" * match.group(0).count("\n"), text)
    return _kLineCommentRe.sub("", text)


def checkCoreTestIsEngineFree(rootDir: Path) -> list[str]:
    """`Test/CoreTest` 의 파일이 엔진 계층 헤더를 include 하거나 `engine::` 를 부르지 않는지 봅니다."""
    folder = rootDir / _kCoreTestFolder
    if folder.is_dir() is False:
        return []
    errors: list[str] = []
    for path in sorted(folder.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        relPath = path.relative_to(rootDir).as_posix()
        code = stripCommentsInternal(path.read_text(encoding="utf-8", errors="ignore"))
        for match in _kCoreTestForbiddenIncludeRe.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            errors.append(f"{relPath}:{line}: CoreTest 가 `{match.group(1)}` 를 include 합니다 — CoreTest 는 엔진 없이 Core 를 "
                          f"시험합니다. 엔진 타입이 필요한 시험은 EngineTest 로 옮기거나 지역 대역을 쓰세요")
        for match in _kEngineNamespaceRe.finditer(code):
            line = code.count("\n", 0, match.start()) + 1
            errors.append(f"{relPath}:{line}: CoreTest 가 `{match.group(0)}` 를 부릅니다 — 프로세스의 엔진 서비스 대신 "
                          f"지역 객체(예: 지역 GlobalVariableManager)를 쓰거나 EngineTest 로 옮기세요")
    return errors


def check(rootDir: Path) -> tuple[list[str], int, int]:
    cases, missed = collectCases(rootDir)
    errors: list[str] = list(missed)
    if not cases:
        return [f"{_kTestRoot}: SW_TEST_CASE 를 하나도 찾지 못했습니다 (검사가 헛돌고 있습니다)"], 0, 0

    homes: dict[str, set[str]] = defaultdict(set)
    firstSite: dict[str, str] = {}
    seen: dict[tuple[str, str], str] = {}
    for suite, caseName, relPath in cases:
        homes[suite].add(relPath)
        firstSite.setdefault(suite, relPath)
        key = (suite, caseName)
        if key in seen:
            errors.append(f"{relPath}: `{suite}.{caseName}` 이 {seen[key]} 와 이름이 겹칩니다 "
                          f"(필터로 하나만 고를 수 없습니다)")
        else:
            seen[key] = relPath

    # 1) 이름 규칙
    for suite in sorted(homes):
        if _kSuiteNameRe.match(suite) is None:
            errors.append(f"{firstSite[suite]}: 스위트 `{suite}` — 이름은 `XxxTest` 여야 합니다 "
                          f"(대문자 시작, `Test` 로 끝, 밑줄 없음). 계층 접두어는 실행 파일 이름이 이미 말합니다")

    # 2) 한 스위트 한 파일
    for suite in sorted(homes):
        if len(homes[suite]) > 1:
            errors.append(f"스위트 `{suite}` 가 파일 {len(homes[suite])}개에 걸쳐 있습니다 "
                          f"({' · '.join(sorted(homes[suite]))}) — 한 파일로 모으거나 이름을 가르세요")

    # 3) 호스트 스위트 선언이 효력이 있는가
    markers, markerErrors = collectMarkers(rootDir)
    errors += markerErrors
    hostSplitFolders = collectHostSplitFolders(rootDir)
    for suite, (relPath, reason) in sorted(markers.items()):
        if suite not in homes:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` — 그런 스위트가 없습니다 "
                          f"(이름을 바꿨다면 선언도 바꾸세요 — 안 그러면 그 스위트가 CI 로 들어갑니다)")
            continue
        if relPath not in homes[suite]:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` — 그 스위트는 이 파일에 없습니다 "
                          f"({' · '.join(sorted(homes[suite]))} 에 있습니다)")
        if not reason:
            errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` 에 이유가 없습니다")
        folder = testFolderOf(relPath)
        if folder not in hostSplitFolders:
            errors.append(f"{relPath}: `{suite}` 는 호스트가 필요하다고 선언했는데 `{folder}/CMakeLists.txt` 의 "
                          f"`sw_addTestExecutable` 에 `HOST_SPLIT` 이 없습니다 — **CI 가 이것을 돌립니다**")
    for folder in sorted(hostSplitFolders - {testFolderOf(relPath) for relPath, _ in markers.values()}):
        errors.append(f"{folder}/CMakeLists.txt: `HOST_SPLIT` 인데 이 폴더에 `SW_TEST_REQUIRES_HOST` 선언이 하나도 "
                      f"없습니다 — `_HostOnly` 가 아무것도 돌지 않습니다")

    # 4) 마커가 붙은 스위트는 자기 파일을 독차지한다
    suitesByFile: dict[str, set[str]] = defaultdict(set)
    for suite, _, relPath in cases:
        suitesByFile[relPath].add(suite)
    for suite, (relPath, _) in sorted(markers.items()):
        if suite not in homes:
            continue  # 위 3) 이 이미 "그런 스위트가 없다" 로 보고했다
        others = sorted(suitesByFile.get(relPath, set()) - {suite})
        if others:
            errors.append(f"{relPath}: `{suite}` 는 CI 가 못 돌리는데 같은 파일에 "
                          f"{' · '.join(others)} 가 있습니다 — 파일을 가르세요 "
                          f"(안 그러면 새 케이스가 CI 쪽 스위트에 붙어 CI 로 들어갑니다)")

    # 5) EditorTest 가 손으로 나열한 Editor 소스
    errors += checkEditorTestSources(rootDir)

    # 6) CoreTest 는 엔진을 직접 쓰지 않는다
    errors += checkCoreTestIsEngineFree(rootDir)

    return errors, len(homes), len(cases)


# 이 검사가 **통과하는** 가장 작은 저장소 — 호스트 스위트 하나(HOST_SPLIT 폴더) · 평범한 스위트 하나 · EditorTest 목록.
_kCleanFixture: dict[str, str] = {
    "Test/EngineTest/CMakeLists.txt": "sw_addTestExecutable(EngineTest\n\tHOST_SPLIT\n)\n",
    "Test/EngineTest/TestHostProbe.cpp": (
        'SW_TEST_REQUIRES_HOST( HostProbeTest, "needs a GPU" );\nSW_TEST_CASE( HostProbeTest, One )\n{\n}\n'
    ),
    "Test/EngineTest/TestPlainProbe.cpp": "SW_TEST_CASE( PlainProbeTest, One )\n{\n}\n",
    "Test/EditorTest/CMakeLists.txt": (
        'sw_addTestExecutable(EditorTest\n\tSOURCES\n\t\t"${CMAKE_SOURCE_DIR}/Source/Editor/Common/Probe.cpp"\n)\n'
    ),
    "Source/Editor/Common/Probe.cpp": "int probe() { return 0; }\n",
}


class CheckTestSuitesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "테스트 스위트 규칙 검사"
    buildComment = "Checking test suite naming, one-file-per-suite, and that every SW_TEST_REQUIRES_HOST takes effect..."
    timeoutSeconds = 15
    preCommitPattern = ("Test/*",)
    # 조각마다 **위반 하나만** 넣는다 — 나머지는 깨끗한 바탕(`_kCleanFixture`)이라, 이 검사가 그 바탕에서 통과하는
    # 한 실패는 넣은 위반 때문이다. 주의: 바탕부터 다른 규칙(예: EditorTest 소스 목록이 빈 것)에 걸리는 조각은
    # 넣은 규칙이 죽어도 실패하므로 증거가 아니다.
    selfTestCases = [
        {
            "name": "스위트 이름이 XxxTest 가 아님",
            "files": {**_kCleanFixture, "Test/EngineTest/TestProbe.cpp": "SW_TEST_CASE( Probe_Bad, Something )\n{\n}\n"},
        },
        {
            "name": "한 스위트가 두 파일에",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": "SW_TEST_CASE( ProbeTest, One )\n{\n}\n",
                "Test/EngineTest/TestProbeB.cpp": "SW_TEST_CASE( ProbeTest, Two )\n{\n}\n",
            },
        },
        {
            # 선언을 읽는 것은 `HOST_SPLIT` 으로 등록된 실행 파일뿐이다 — 없으면 그 스위트는 CI 에서 돈다.
            "name": "호스트 선언이 있는데 HOST_SPLIT 이 없음",
            "files": {
                **_kCleanFixture,
                "Test/GpuTest/TestGpuProbe.cpp": (
                    'SW_TEST_REQUIRES_HOST( GpuProbeTest, "needs a GPU" );\nSW_TEST_CASE( GpuProbeTest, One )\n{\n}\n'
                ),
                "Test/GpuTest/CMakeLists.txt": "# HOST_SPLIT 은 주석으로만 적혀 있다\nsw_addTestExecutable(GpuTest)\n",
            },
        },
        {
            # 이름을 바꾸고 선언을 놓치면 선언은 아무것도 빼지 않고, 바뀐 스위트가 CI 로 들어간다.
            "name": "호스트 선언이 없는 스위트를 가리킴",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'SW_TEST_REQUIRES_HOST( OldNameTest, "needs a GPU" );\nSW_TEST_CASE( NewNameTest, One )\n{\n}\n'
                ),
            },
        },
        {
            "name": "호스트 스위트 파일에 다른 스위트가 섞임",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    'SW_TEST_REQUIRES_HOST( ProbeTest, "needs a GPU" );\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n'
                    "SW_TEST_CASE( NeighbourTest, Two )\n{\n}\n"
                ),
            },
        },
        {
            "name": "CoreTest 가 Engine 헤더를 include",
            "files": {
                **_kCleanFixture,
                "Test/CoreTest/TestProbe.cpp": '#include "Engine/Physics/AABB.h"\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n',
            },
        },
        {
            # 주석의 `engine::` 는 세지 않는다 — 걸리는 것은 본문의 호출이다.
            "name": "CoreTest 가 engine:: 서비스를 부름",
            "files": {
                **_kCleanFixture,
                "Test/CoreTest/TestProbe.cpp": (
                    "// engine::releaseModuleCode 가 부르는 길\n"
                    "SW_TEST_CASE( ProbeTest, One )\n{\n    sw::engine::getGlobalVariableManager();\n}\n"
                ),
            },
        },
        {
            "name": "옛 주석 마커",
            "files": {
                **_kCleanFixture,
                "Test/EngineTest/TestProbe.cpp": (
                    "// SW_TEST_REQUIRES_HOST( ProbeTest ): GPU 가 필요합니다\nSW_TEST_CASE( ProbeTest, One )\n{\n}\n"
                ),
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        errors, suiteCount, caseCount = check(repositoryRoot)
        return GateResult(listViolation=errors, summary=f"{suiteCount} suites, {caseCount} cases")


main = CheckTestSuitesGate.run


if __name__ == "__main__":
    sys.exit(main())
