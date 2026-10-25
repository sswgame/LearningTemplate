#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunSyntaxCheck.py
@brief 작업자가 바꾼 C++ 파일만 단일 파일 문법 검사(-fsyntax-only)한다 — 빌드 · 링크 · 테스트 없이 컴파일 오류를 통합 전에 잡는다.

    py -3 -m Scripts syntax-check                  # base...HEAD 와 작업 트리에서 바뀐 Source/ · Test/ 의 C++ 파일
    py -3 -m Scripts syntax-check Source/A/B.cpp   # 이 파일만
    py -3 -m Scripts syntax-check --base wt/other  # 다른 브랜치에서 갈라졌으면

바뀐 `.cpp` 는 컴파일 DB 의 자기 항목(새 파일이면 같은 폴더의 이웃 TU)으로, 바뀐 `.h` 는 혼자 서는지 보는 탐침과 그 헤더를 include 하는 `.cpp` 한두 개로 본다.
플래그는 **메인 저장소** 빌드 폴더(`--preset` · `--build-dir`, 기본 `.clangd` 가 가리키는 트리)의 컴파일 DB 에서 빌려 오고 소스 쪽 경로만 이 워크트리로 옮긴다.
판정은 `common/SyntaxCheck.py` 다. 종료 코드: 0 통과 · 1 컴파일 오류 · 2 검사 불성립(컴파일 DB 없음 · 코드젠 안 돎 · git 실패 · 없는 파일).
템플릿 인스턴스화 · 링크 오류 · 바꾼 헤더가 파급되는 다른 TU 는 못 잡는다 — 전체 빌드는 통합이 한다.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.BuildTree import addBuildTreeArguments, BuildTree, BuildTreeError  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.HeaderSelfContained import findHeaderProbeProblem, loadCompileDatabase  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.CompileDatabaseRehome import rehomeDatabase  # noqa: E402
from common.SyntaxCheck import listChangedSourceFiles, planSyntaxJobs, runSyntaxJobs  # noqa: E402
from common.Worktree import findMainRoot  # noqa: E402

kTag = "[SyntaxCheck]"


def resolveFileArguments(listFile: list[str]) -> list[Path]:
    """인자 파일을 절대 경로로(상대 경로는 현재 폴더 기준). 없는 파일이면 FileNotFoundError."""
    listPath = [(Path.cwd() / name).resolve() for name in listFile]
    for path in listPath:
        if not path.is_file():
            raise FileNotFoundError(f"{path} 가 없다")
    return listPath


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="바꾼 C++ 파일만 -fsyntax-only 로 검사한다(전체 빌드 · 링크 없음)")
    parser.add_argument("files", nargs="*", help="검사할 파일(없으면 base...HEAD 와 작업 트리에서 바뀐 .cpp · .h)")
    parser.add_argument("--base", default="main", help="바뀐 파일을 셀 기준 브랜치(기본 main)")
    addBuildTreeArguments(parser)
    args = parser.parse_args(argv)

    root = getProjectRoot()
    try:
        mainRoot = findMainRoot(root)
        tree = BuildTree.fromArguments(args, mainRoot)
        problem = findHeaderProbeProblem(tree.path)
        if problem:
            print(f"{kTag} 검사할 수 없다 — {problem}", file=sys.stderr)
            return ExitCode.CheckInvalid
        listFile = resolveFileArguments(args.files) if args.files else listChangedSourceFiles(root, args.base)
        database = rehomeDatabase(loadCompileDatabase(tree.path), mainRoot, root)
    except (BuildTreeError, RuntimeError, FileNotFoundError) as error:
        print(f"{kTag} 검사할 수 없다 — {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    if not listFile:
        print(f"{kTag} 바뀐 C++ 파일이 없다")
        return ExitCode.Passed

    with tempfile.TemporaryDirectory(prefix="swSyntaxCheck") as probeDirName:
        try:
            listJob, listNote = planSyntaxJobs(root, tree.path, listFile, database, Path(probeDirName))
        except RuntimeError as error:
            print(f"{kTag} 검사할 수 없다 — {error}", file=sys.stderr)
            return ExitCode.CheckInvalid
        print(f"{kTag} 파일 {len(listFile)}개 → 컴파일 {len(listJob)}번 (플래그: {tree.compileDatabasePath.as_posix()})")
        for note in listNote:
            print(f"  참고: {note}")
        listResult = runSyntaxJobs(listJob, tree.path, root)

    listFailed = [job.label for job, listDiagnostic in listResult if listDiagnostic]
    if not listFailed:
        print(f"{kTag} 통과 — 템플릿 인스턴스화 · 링크 · 다른 TU 로의 파급은 이 검사가 못 본다")
        return ExitCode.Passed
    print(f"{kTag} 컴파일 오류:")
    for diagnostic in dict.fromkeys(line for _, listDiagnostic in listResult for line in listDiagnostic):
        print(f"  {diagnostic}")
    print(f"{kTag} 실패 {len(listFailed)}/{len(listJob)}: " + " · ".join(listFailed))
    return ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
