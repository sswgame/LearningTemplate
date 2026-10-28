#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Include 순서 및 스타일 검사 린터.

규칙:
1. .cpp 파일의 첫 번째 include는 무조건 "pch.h" 이어야 합니다.
2. 프로젝트 헤더 ("...") 인클루드가 시스템/외부 헤더 (<...>) 인클루드보다 먼저 와야 합니다.

이 게이트는 검사만 한다 — 고치는 것은 같은 규칙을 부르는 픽서 `Scripts/lint/fixer/FormatIncludeOrder.py`(규칙 · 판정은 `Scripts/lint/IncludeOrderRules.py` 한 자리).
헤더 조회표는 훑기 전에 한 번 만들어 자식 프로세스로 같이 보낸다.

사용법:
  python Scripts/lint/gate/CheckIncludeOrder.py [--root <repo>] [--files <path> ...]

종료 코드: 0 통과(또는 `--files` 로 좁혀 볼 파일 없음), 1 위반, 2 읽을 수 없는 파일.
"""

from __future__ import annotations

import argparse
import functools
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Constants import kCppAllExtensions, kLintTargetRelDirs, kNotOurDirNames  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from lint.Parallel import flatMapInProcesses  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Search import collectRepositoryFiles, resolveFileArguments  # noqa: E402
from lint.IncludeOrderRules import buildHeaderLookupMap, inspectText  # noqa: E402

kTag = "CheckIncludeOrder"


def inspectFileInternal(headerMaps: tuple, repositoryRoot: Path, path: Path) -> list[str]:
    """파일 하나의 위반(읽을 수 없으면 `!` 로 시작하는 한 줄). 훑는 사이에 지워진 파일은 건너뛴다."""
    relative = path.resolve().relative_to(repositoryRoot).as_posix()
    try:
        rawText = path.read_bytes().decode("utf-8")
    except FileNotFoundError:
        return []
    except (OSError, UnicodeDecodeError) as exception:
        return [f"!{relative}: 읽기 실패: {exception}"]
    return inspectText(rawText, relative, headerMaps)[0]


def checkIncludeOrder(repositoryRoot: Path, listFileArgument: Sequence[str] | None) -> tuple[list[str], int]:
    """`listFileArgument`(None 이면 전체)의 C++ 파일을 검사한다. 돌려주는 것은 `(위반 줄 목록, 본 파일 수)`."""
    repositoryRoot = repositoryRoot.resolve()
    suffixes = tuple(sorted(kCppAllExtensions))
    if listFileArgument is not None:
        listPath = resolveFileArguments(repositoryRoot, listFileArgument, listScanRoot=kLintTargetRelDirs, suffixes=suffixes)
    else:
        listPath = collectRepositoryFiles(repositoryRoot, kLintTargetRelDirs, suffixes=suffixes, excludedDirNames=kNotOurDirNames)
    if not listPath:
        return [], 0
    worker = functools.partial(inspectFileInternal, buildHeaderLookupMap(repositoryRoot), repositoryRoot)
    return flatMapInProcesses(worker, listPath), len(listPath)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Include 순서 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    parser.add_argument("--files", nargs="*", default=None, help="검사할 파일 (생략 시 전체)")
    args = parser.parse_args(argv)

    listViolation, checked = checkIncludeOrder(args.root or getProjectRoot(), args.files)
    if args.files is not None and checked == 0:
        print(f"[{kTag}] SKIPPED (검사할 파일 없음)")
        return ExitCode.Passed
    listUnreadable = [line for line in listViolation if line.startswith("!")]
    if listUnreadable:
        print(f"[{kTag}] FAIL — 읽을 수 없는 파일 {len(listUnreadable)}개")
        for line in listUnreadable:
            print(f"  - {line[1:]}")
        return ExitCode.CheckInvalid
    if listViolation:
        print(f"[{kTag}] FAIL — Include 순서 규칙 위반 {len(listViolation)}건")
        for line in listViolation:
            print(f"  - {line}")
        return ExitCode.Violation
    print(f"[{kTag}] OK ({checked} files)")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
