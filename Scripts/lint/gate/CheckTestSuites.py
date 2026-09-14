#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
테스트 스위트 규칙 검사.

스위트 이름은 장식이 아니다. `EngineTest_NoGPU` 가 **스위트 이름으로** CI 가 못 돌리는 것을 걸러내고,
`--test_filter` 도 스위트 단위로 고른다. 그래서 이름이 흔들리면 필터가 흔들린다.

강제 규칙 다섯:

  1) 스위트 이름은 `XxxTest` — 대문자로 시작하고 `Test` 로 끝나며 밑줄이 없다.
     예전에는 한 저장소 안에 세 관례가 섞여 있었다(2026-09-13 기준 123 개 중 접두어형 45 · 접미어형 66 ·
     맨이름 12). `Core` 라는 스위트가 있어서 `--test_filter=Core*` 가 `Core_String` 까지 끌어왔다.
     계층 접두어(`Core_` · `Engine_`)는 **실행 파일 이름이 이미 말한다** — CoreTest.exe 안의 `Core_String` 은
     같은 말을 두 번 한다. 게다가 그 접두어는 믿을 수도 없었다: `Engine_Event` 는 CoreTest 에 있었다.

  2) 한 스위트는 **한 파일에만** 산다. 갈라져 있으면 "이 스위트를 고치려면 어디를 여나" 에 답이 둘이 된다.

  3) CI 가 못 돌리는 스위트는 자기 파일에 `// SW_TEST_REQUIRES_HOST( 스위트 ): <이유>` 를 적고,
     그 집합이 `Test/EngineTest/CMakeLists.txt` 의 `EngineTest_NoGPU` 필터와 **양방향으로** 같아야 한다.
       - 마커가 있는데 필터에 없다 → CI 가 그것을 돌리고 있다. 2026-09-08 에 이 방향으로 나흘간 빨갰다.
       - 필터에 있는데 마커가 없다 → 죽은 필터 항목. 지워야 할지 아무도 모른다.
     **목록이 아니라 마커가 정본이다** — 필터는 마커에서 유도되는 값으로 본다.

  4) 마커가 붙은 스위트가 있는 파일에는 **다른 스위트를 두지 않는다.**
     예전엔 `TestRHI.cpp` 가 `RHITest`(CI 제외)와 Support 세 스위트(CI 실행)를 같이 들고 있었고,
     `TestShader.cpp` 도 마찬가지였다. 한 파일에 섞여 있으면 새 케이스를 옆 스위트에 붙이기가 너무 쉽다 —
     그 순간 GPU 가 필요한 케이스가 CI 로 들어간다.

  5) `EditorTest` 가 끌어오는 Editor 소스는 **존재해야 하고, ImGui 헤더를 include 하지 않아야 한다.**
     `EditorModule` 은 MODULE DLL 이라 링크할 수 없어서, 테스트가 필요한 `.cpp` 만 손으로 나열해 같이
     컴파일한다. 그 목록이 "Editor/Common 전부" 도 "ImGui 안 쓰는 것 전부" 도 아니라 **테스트가 실제로
     링크해야 하는 것** 이라 기계가 대신 고를 수 없다. 그래서 목록 자체는 손으로 두되, 썩는 두 방향만 막는다:
     파일이 옮겨져 경로가 죽는 것(실제로 있었다)과, ImGui 를 타는 파일이 섞여 들어오는 것
     (EditorTest 는 ImGui 를 링크하지 않으므로 그 순간 빌드가 깨진다).

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
_kNoGpuFilterFile = "Test/EngineTest/CMakeLists.txt"
_kNoGpuTestName = "EngineTest_NoGPU"
_kEditorTestCMake = "Test/EditorTest/CMakeLists.txt"

# 줄 맨 앞만 보면 안 된다 — `namespace sw::editor { ... }` 안에 들여쓴 케이스가 실제로 있었고,
# 그 파일이 검사에서 통째로 빠져 밑줄 든 스위트 이름이 그대로 살아 있었다(2026-09-13).
_kCaseRe = re.compile(r"^[ 	]*SW_TEST_CASE\(\s*(\w+)\s*,\s*(\w+)\s*\)", re.M)
# 위 정규식이 놓치는 표기가 생기면 조용히 줄어들 뿐이라, 토큰을 따로 세어 대조한다.
_kCaseTokenRe = re.compile(r"\bSW_TEST_CASE\s*\(")
_kSuiteNameRe = re.compile(r"^[A-Z][A-Za-z0-9]*Test$")
_kMarkerRe = re.compile(r"//\s*SW_TEST_REQUIRES_HOST\(\s*(\w+)\s*\)\s*:\s*(\S.*)")
_kFilterRe = re.compile(r"--test_filter=(\S+)")
_kEditorSourceRe = re.compile(r"\$\{CMAKE_SOURCE_DIR\}/(Source/Editor/[\w/]+\.cpp)")
_kImGuiIncludeRe = re.compile(r"^\s*#\s*include\s*[<\"][^>\"]*imgui[^>\"]*[>\"]", re.I | re.M)


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


def collectMarkers(rootDir: Path) -> dict[str, tuple[str, str]]:
    """스위트 -> (파일, 이유). 마커는 그 스위트가 사는 파일에 적는다."""
    out: dict[str, tuple[str, str]] = {}
    for path in sorted((rootDir / _kTestRoot).rglob("*.cpp")):
        relPath = path.relative_to(rootDir).as_posix()
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            match = _kMarkerRe.search(line)
            if match is not None:
                out[match.group(1)] = (relPath, match.group(2).strip())
    return out


def readNoGpuFilter(rootDir: Path) -> tuple[set[str], str | None]:
    """`EngineTest_NoGPU` 가 제외하는 스위트 집합."""
    path = rootDir / _kNoGpuFilterFile
    if path.exists() is False:
        return set(), f"{_kNoGpuFilterFile}: 파일이 없습니다"
    text = path.read_text(encoding="utf-8", errors="ignore")
    if _kNoGpuTestName not in text:
        return set(), f"{_kNoGpuFilterFile}: `{_kNoGpuTestName}` 등록을 찾지 못했습니다"
    match = _kFilterRe.search(text)
    if match is None:
        return set(), f"{_kNoGpuFilterFile}: `--test_filter=` 를 찾지 못했습니다"
    excluded: set[str] = set()
    for token in match.group(1).split(","):
        token = token.strip()
        if token.startswith("-") and token.endswith(".*"):
            excluded.add(token[1:-2])
    return excluded, None


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

    # 3) 마커 <-> 필터 양방향
    markers = collectMarkers(rootDir)
    excluded, filterError = readNoGpuFilter(rootDir)
    if filterError is not None:
        errors.append(filterError)
    else:
        for suite, (relPath, reason) in sorted(markers.items()):
            if suite not in homes:
                errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` — 그런 스위트가 없습니다")
                continue
            if relPath not in homes[suite]:
                errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` — 그 스위트는 이 파일에 없습니다 "
                              f"({' · '.join(sorted(homes[suite]))} 에 있습니다)")
            if not reason:
                errors.append(f"{relPath}: `SW_TEST_REQUIRES_HOST( {suite} )` 에 이유가 없습니다")
            if suite not in excluded:
                errors.append(f"{relPath}: `{suite}` 는 호스트가 필요하다고 적혀 있는데 "
                              f"`{_kNoGpuTestName}` 필터에 없습니다 — **CI 가 이것을 돌리고 있습니다**")
        for suite in sorted(excluded - set(markers)):
            where = f" ({' · '.join(sorted(homes[suite]))})" if suite in homes else " (그런 스위트가 없습니다)"
            errors.append(f"{_kNoGpuFilterFile}: 필터가 `{suite}` 를 빼는데 그 스위트에 "
                          f"`SW_TEST_REQUIRES_HOST` 마커가 없습니다{where} — 마커를 달거나 필터에서 지우세요")

    # 4) 마커가 붙은 스위트는 자기 파일을 독차지한다
    suitesByFile: dict[str, set[str]] = defaultdict(set)
    for suite, _, relPath in cases:
        suitesByFile[relPath].add(suite)
    for suite, (relPath, _) in sorted(markers.items()):
        others = sorted(suitesByFile.get(relPath, set()) - {suite})
        if others:
            errors.append(f"{relPath}: `{suite}` 는 CI 가 못 돌리는데 같은 파일에 "
                          f"{' · '.join(others)} 가 있습니다 — 파일을 가르세요 "
                          f"(안 그러면 새 케이스가 CI 쪽 스위트에 붙어 CI 로 들어갑니다)")

    # 5) EditorTest 가 손으로 나열한 Editor 소스
    errors += checkEditorTestSources(rootDir)

    return errors, len(homes), len(cases)


class CheckTestSuitesGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "테스트 스위트 규칙 검사"
    selfTestCases = [
        {
            "name": "스위트 이름이 XxxTest 가 아님",
            "files": {
                "Test/EngineTest/TestProbe.cpp": "SW_TEST_CASE( Probe_Bad, Something )\n{\n}\n",
                "Test/EngineTest/CMakeLists.txt": (
                    "add_test(\n\tNAME EngineTest_NoGPU\n"
                    "\tCOMMAND EngineTest --test_filter=-RHIDeviceTest.*\n)\n"
                ),
                "Test/EditorTest/CMakeLists.txt": 'sw_addTestExecutable(EditorTest)\n',
            },
        },
        {
            "name": "한 스위트가 두 파일에",
            "files": {
                "Test/EngineTest/TestProbeA.cpp": "SW_TEST_CASE( ProbeTest, One )\n{\n}\n",
                "Test/EngineTest/TestProbeB.cpp": "SW_TEST_CASE( ProbeTest, Two )\n{\n}\n",
                "Test/EngineTest/CMakeLists.txt": (
                    "add_test(\n\tNAME EngineTest_NoGPU\n"
                    "\tCOMMAND EngineTest --test_filter=-RHIDeviceTest.*\n)\n"
                ),
                "Test/EditorTest/CMakeLists.txt": 'sw_addTestExecutable(EditorTest)\n',
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        errors, suiteCount, caseCount = check(repositoryRoot)
        return GateResult(listViolation=errors, summary=f"{suiteCount} suites, {caseCount} cases")


main = CheckTestSuitesGate.run


if __name__ == "__main__":
    sys.exit(main())
