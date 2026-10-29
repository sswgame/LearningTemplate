#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/CMakePathCase.py

CMake 의 `include()` · `add_subdirectory()` 경로가 저장소의 실제 이름과 대소문자까지 같은지 본다.

왜 린트인가: C++ include 는 컴파일러가 본다(`-Werror=nonportable-include-path`, cmake/Modules/Compiler/Clang.cmake). CMake 경로는 컴파일러 · clang-tidy 가
보지 않고, Windows 는 대소문자를 가리지 않아 Linux 구성에서야 "없는 파일" 로 선다(`RhiBackends.cmake` 를 `RHIBackends.cmake` 로 부르던 결함). 형식으로 막을 수 없는 틈이다.
CMake 파일만이 아니라 이름 바꾸기도 참조를 깨므로 커밋마다 CMake 파일 전부를 본다(수백 개라 빠르다).

  python Scripts/lint/CMakePathCase.py    (= `py -3 -m Scripts cmake-path-case`)

종료 코드: 0 통과, 1 위반.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kCheckTag = "CMakePathCase"

_kPathCallRe = re.compile(r'\b(?:include|add_subdirectory)\(\s*"?([^\s")]+)', re.IGNORECASE)


def checkCMakePathCase(projectRoot: Path) -> list[str]:
    """대소문자만 다른 CMake 경로를 "파일: 적은 경로 → 실제 이름" 줄로 돌려준다."""
    listFile = subprocess.run(["git", "ls-files"], cwd=projectRoot, capture_output=True, text=True, encoding="utf-8", check=True).stdout.splitlines()
    setExact = set(listFile)
    for file in listFile:
        parts = file.split("/")
        setExact.update("/".join(parts[:index]) for index in range(1, len(parts)))
    mapLower = {path.lower(): path for path in setExact}

    listViolation: list[str] = []
    for file in listFile:
        if not ( file.endswith(".cmake") or file.endswith("CMakeLists.txt") ):
            continue
        text = (projectRoot / file).read_text(encoding="utf-8", errors="ignore")
        baseDir = str(Path(file).parent).replace("\\", "/")
        for match in _kPathCallRe.finditer(text):
            written = match.group(1)
            if "$" in written or "<" in written:
                continue   # 변수 · 생성기 식은 구성 때에만 풀린다
            candidates = [written, f"{baseDir}/{written}" if baseDir != "." else written]
            if any(candidate in setExact for candidate in candidates):
                continue
            for candidate in candidates:
                actual = mapLower.get(candidate.lower())
                if actual is not None:
                    listViolation.append(f"{file}: '{written}' → 실제 이름 '{actual}'")
                    break
    return listViolation


def main(argv: list[str] | None = None) -> int:
    listViolation = checkCMakePathCase(getProjectRoot())
    for line in listViolation:
        print(f"  - {line}")
    return ExitCode.Violation if listViolation else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
