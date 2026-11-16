#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/cmake_path_case.py

CMake 의 `include()` · `add_subdirectory()` 경로가 저장소의 실제 이름과 대소문자까지 같은지 본다.

왜 린트인가: C++ include 는 컴파일러가 본다(`-Werror=nonportable-include-path`, cmake/Toolchain/Compiler/Clang.cmake). CMake 경로는 컴파일러 · clang-tidy 가
보지 않고, Windows 는 대소문자를 가리지 않아 Linux 구성에서야 "없는 파일" 로 선다(`RhiBackends.cmake` 를 `RHIBackends.cmake` 로 부르던 결함). 형식으로 막을 수 없는 틈이다.
CMake 파일만이 아니라 이름 바꾸기도 참조를 깨므로 판정은 CMake 파일 전부의 색인(`index_cache`, 바뀐 파일만 다시 읽는다)으로 낸다.

  python Scripts/lint/cmake_path_case.py    (= `py -3 -m Scripts lint cmake-path-case`)

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
from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.index_cache import open_facts  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope  # noqa: E402

CHECK_TAG = "CMakePathCase"

_PATH_CALL_RE = re.compile(r'\b(?:include|add_subdirectory)\(\s*"?([^\s")]+)', re.IGNORECASE)
_ROOT_PATH_RE = re.compile(r'\$\{CMAKE_SOURCE_DIR\}/([^\s"$)<]+)')


def extract_paths(text: str) -> dict[str, list[str]]:
    """CMake 글 하나가 적은 경로 — 저장소 루트 기준(`${CMAKE_SOURCE_DIR}/…`)과 `include()` · `add_subdirectory()` 인자. 색인(`index_cache`)에 둔다."""
    return {"root": [match.group(1) for match in _ROOT_PATH_RE.finditer(text)],
            "call": [match.group(1) for match in _PATH_CALL_RE.finditer(text) if "$" not in match.group(1) and "<" not in match.group(1)]}   # 변수 · 생성기 식은 구성 때에만 풀린다


def check_cmake_path_case(project_root: Path, scope: LintScope) -> list[str]:
    """대소문자만 다른 CMake 경로를 "파일: 적은 경로 → 실제 이름" 줄로 돌려준다.
    이름 바꾸기도 바뀌지 않은 CMake 파일의 참조를 깨므로 판정은 늘 저장소 전체 색인으로 낸다 — 커밋 훅은 CMake 파일이 바뀌었거나 파일을 지웠을(옮겼을) 때만 돈다."""
    if not scope.whole_repo and not scope.list_removed and not scope.touches(suffixes=(".cmake", "CMakeLists.txt")):
        return []
    list_file = list_repo_files(project_root)
    list_cmake = [file for file in list_file if file.endswith(".cmake") or file.endswith("CMakeLists.txt")]
    facts = open_facts(project_root, "cmake-path-case", [__file__], list_cmake, lambda _, text: extract_paths(text), errors="ignore",
                      contribute=lambda file, fact: [json.dumps([file, kind, index, written]) for kind in ("root", "call") for index, written in enumerate(fact[kind])])
    return evaluate_paths(list_file, unfold_paths(list_cmake, facts.summary))


def unfold_paths(list_cmake: Sequence[str], summary: Iterable[str]) -> dict[str, dict[str, list[str]]]:
    """요약 토큰을 `extractPaths` 모양(파일 순 · 적은 차례)으로 되돌린다."""
    map_row: dict[str, list[tuple[str, int, str]]] = {}
    for token in summary:
        file, kind, index, written = json.loads(token)
        map_row.setdefault(file, []).append((kind, index, written))
    return {file: {kind: [written for row_kind, _, written in sorted(map_row[file], key=lambda row: row[1]) if row_kind == kind] for kind in ("root", "call")}
            for file in list_cmake if file in map_row}


def evaluate_paths(list_file: Sequence[str], map_fact: dict[str, dict[str, list[str]]]) -> list[str]:
    """저장소 파일 목록과 CMake 파일마다 적은 경로(`extractPaths`)로 대소문자만 다른 경로를 찾는다."""
    set_exact = set(list_file)
    for file in list_file:
        parts = file.split("/")
        set_exact.update("/".join(parts[:index]) for index in range(1, len(parts)))
    map_lower = {path.lower(): path for path in set_exact}

    list_violation: list[str] = []
    for file, facts in map_fact.items():
        base_dir = str(Path(file).parent).replace("\\", "/")
        # 스크립트 경로(`-P`) · 파일 인자처럼 저장소 루트에서 적은 경로도 같은 덫이다 — 윈도우는 통과하고 리눅스 구성 · 빌드가 선다.
        for written in facts["root"]:
            actual = map_lower.get(written.lower())
            if written not in set_exact and actual is not None:
                list_violation.append(f"{file}: '{written}' → 실제 이름 '{actual}'")
        for written in facts["call"]:
            candidates = [written, f"{base_dir}/{written}" if base_dir != "." else written]
            if any(candidate in set_exact for candidate in candidates):
                continue
            for candidate in candidates:
                actual = map_lower.get(candidate.lower())
                if actual is not None:
                    list_violation.append(f"{file}: '{written}' → 실제 이름 '{actual}'")
                    break
    return list_violation


def run_self_check() -> None:
    list_file = ["CMakeLists.txt", "cmake/Modules/Build/BuildLayout.cmake", "Source/Engine/CMakeLists.txt", "Source/Engine/Audio/CMakeLists.txt"]
    text = 'include(${CMAKE_SOURCE_DIR}/cmake/modules/build/BuildLayout.cmake)\nadd_subdirectory(Audio)\nadd_subdirectory(audio)\nadd_subdirectory(${X})\n'
    list_violation = evaluate_paths(list_file, {"Source/Engine/CMakeLists.txt": extract_paths(text)})
    assert list_violation == ["Source/Engine/CMakeLists.txt: 'cmake/modules/build/BuildLayout.cmake' → 실제 이름 'cmake/Modules/Build/BuildLayout.cmake'",
                             "Source/Engine/CMakeLists.txt: 'audio' → 실제 이름 'Source/Engine/Audio'"], list_violation
    assert evaluate_paths(list_file, {"CMakeLists.txt": extract_paths("add_subdirectory(Source/Engine)\n")}) == [], "맞는 대소문자는 통과한다"
    fact = extract_paths("add_subdirectory(b)\nadd_subdirectory(a)\nadd_subdirectory(b)\n")
    tokens = [json.dumps(["x/CMakeLists.txt", kind, index, written]) for kind in ("root", "call") for index, written in enumerate(fact[kind])]
    assert unfold_paths(["x/CMakeLists.txt"], reversed(tokens)) == {"x/CMakeLists.txt": fact}, "요약에서 차례 · 겹친 경로를 되돌린다"


def main(argv: list[str] | None = None) -> int:
    if argv and argv[0] == "--self-check":
        run_self_check()
        print(f"[{CHECK_TAG}] self-check OK")
        return ExitCode.PASSED
    list_violation = check_cmake_path_case(get_project_root(), make_whole_scope(get_project_root()))
    for line in list_violation:
        print(f"  - {line}")
    return ExitCode.VIOLATION if list_violation else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
