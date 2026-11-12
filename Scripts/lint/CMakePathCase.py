#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CMakePathCase.py

CMake 의 `include()` · `add_subdirectory()` 경로가 저장소의 실제 이름과 대소문자까지 같은지 본다.

왜 린트인가: C++ include 는 컴파일러가 본다(`-Werror=nonportable-include-path`, cmake/Modules/Compiler/Clang.cmake). CMake 경로는 컴파일러 · clang-tidy 가
보지 않고, Windows 는 대소문자를 가리지 않아 Linux 구성에서야 "없는 파일" 로 선다(`RhiBackends.cmake` 를 `RHIBackends.cmake` 로 부르던 결함). 형식으로 막을 수 없는 틈이다.
CMake 파일만이 아니라 이름 바꾸기도 참조를 깨므로 판정은 CMake 파일 전부의 색인(`FactCache`, 바뀐 파일만 다시 읽는다)으로 낸다.

  python Scripts/lint/CMakePathCase.py    (= `py -3 -m Scripts lint cmake-path-case`)

종료 코드: 0 통과, 1 위반.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.FactCache import openFacts  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402

kCheckTag = "CMakePathCase"

_kPathCallRe = re.compile(r'\b(?:include|add_subdirectory)\(\s*"?([^\s")]+)', re.IGNORECASE)
_kRootPathRe = re.compile(r'\$\{CMAKE_SOURCE_DIR\}/([^\s"$)<]+)')


def extractPaths(text: str) -> dict[str, list[str]]:
    """CMake 글 하나가 적은 경로 — 저장소 루트 기준(`${CMAKE_SOURCE_DIR}/…`)과 `include()` · `add_subdirectory()` 인자. 색인(`FactCache`)에 둔다."""
    return {"root": [match.group(1) for match in _kRootPathRe.finditer(text)],
            "call": [match.group(1) for match in _kPathCallRe.finditer(text) if "$" not in match.group(1) and "<" not in match.group(1)]}   # 변수 · 생성기 식은 구성 때에만 풀린다


def checkCMakePathCase(projectRoot: Path, scope: LintScope) -> list[str]:
    """대소문자만 다른 CMake 경로를 "파일: 적은 경로 → 실제 이름" 줄로 돌려준다.
    이름 바꾸기도 바뀌지 않은 CMake 파일의 참조를 깨므로 판정은 늘 저장소 전체 색인으로 낸다 — 커밋 훅은 CMake 파일이 바뀌었거나 파일을 지웠을(옮겼을) 때만 돈다."""
    if not scope.bWholeRepo and not scope.listRemoved and not scope.touches(suffixes=(".cmake", "CMakeLists.txt")):
        return []
    listFile = listRepoFiles(projectRoot)
    listCMake = [file for file in listFile if file.endswith(".cmake") or file.endswith("CMakeLists.txt")]
    facts = openFacts(projectRoot, "cmake-path-case", [__file__], listCMake, lambda _, text: extractPaths(text), errors="ignore",
                      contribute=lambda file, fact: [json.dumps([file, kind, index, written]) for kind in ("root", "call") for index, written in enumerate(fact[kind])])
    return judgePaths(listFile, unfoldPaths(listCMake, facts.summary))


def unfoldPaths(listCMake: Sequence[str], summary: Iterable[str]) -> dict[str, dict[str, list[str]]]:
    """요약 토큰을 `extractPaths` 모양(파일 순 · 적은 차례)으로 되돌린다."""
    mapRow: dict[str, list[tuple[str, int, str]]] = {}
    for token in summary:
        file, kind, index, written = json.loads(token)
        mapRow.setdefault(file, []).append((kind, index, written))
    return {file: {kind: [written for rowKind, _, written in sorted(mapRow[file], key=lambda row: row[1]) if rowKind == kind] for kind in ("root", "call")}
            for file in listCMake if file in mapRow}


def judgePaths(listFile: Sequence[str], mapFact: dict[str, dict[str, list[str]]]) -> list[str]:
    """저장소 파일 목록과 CMake 파일마다 적은 경로(`extractPaths`)로 대소문자만 다른 경로를 찾는다."""
    setExact = set(listFile)
    for file in listFile:
        parts = file.split("/")
        setExact.update("/".join(parts[:index]) for index in range(1, len(parts)))
    mapLower = {path.lower(): path for path in setExact}

    listViolation: list[str] = []
    for file, facts in mapFact.items():
        baseDir = str(Path(file).parent).replace("\\", "/")
        # 스크립트 경로(`-P`) · 파일 인자처럼 저장소 루트에서 적은 경로도 같은 덫이다 — 윈도우는 통과하고 리눅스 구성 · 빌드가 선다.
        for written in facts["root"]:
            actual = mapLower.get(written.lower())
            if written not in setExact and actual is not None:
                listViolation.append(f"{file}: '{written}' → 실제 이름 '{actual}'")
        for written in facts["call"]:
            candidates = [written, f"{baseDir}/{written}" if baseDir != "." else written]
            if any(candidate in setExact for candidate in candidates):
                continue
            for candidate in candidates:
                actual = mapLower.get(candidate.lower())
                if actual is not None:
                    listViolation.append(f"{file}: '{written}' → 실제 이름 '{actual}'")
                    break
    return listViolation


def runSelfCheck() -> None:
    listFile = ["CMakeLists.txt", "cmake/Engine/BuildLayout.cmake", "Source/Engine/CMakeLists.txt", "Source/Engine/Audio/CMakeLists.txt"]
    text = 'include(${CMAKE_SOURCE_DIR}/cmake/engine/BuildLayout.cmake)\nadd_subdirectory(Audio)\nadd_subdirectory(audio)\nadd_subdirectory(${X})\n'
    listViolation = judgePaths(listFile, {"Source/Engine/CMakeLists.txt": extractPaths(text)})
    assert listViolation == ["Source/Engine/CMakeLists.txt: 'cmake/engine/BuildLayout.cmake' → 실제 이름 'cmake/Engine/BuildLayout.cmake'",
                             "Source/Engine/CMakeLists.txt: 'audio' → 실제 이름 'Source/Engine/Audio'"], listViolation
    assert judgePaths(listFile, {"CMakeLists.txt": extractPaths("add_subdirectory(Source/Engine)\n")}) == [], "맞는 대소문자는 통과한다"
    fact = extractPaths("add_subdirectory(b)\nadd_subdirectory(a)\nadd_subdirectory(b)\n")
    tokens = [json.dumps(["x/CMakeLists.txt", kind, index, written]) for kind in ("root", "call") for index, written in enumerate(fact[kind])]
    assert unfoldPaths(["x/CMakeLists.txt"], reversed(tokens)) == {"x/CMakeLists.txt": fact}, "요약에서 차례 · 겹친 경로를 되돌린다"


def main(argv: list[str] | None = None) -> int:
    if argv and argv[0] == "--self-check":
        runSelfCheck()
        print(f"[{kCheckTag}] self-check OK")
        return ExitCode.Passed
    listViolation = checkCMakePathCase(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for line in listViolation:
        print(f"  - {line}")
    return ExitCode.Violation if listViolation else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
