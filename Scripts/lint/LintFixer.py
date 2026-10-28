#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
픽서가 나눠 쓰는 대상 파일 고르기 — 명시 경로(`--files`) > `--all` > git 수정분 > 전부, 그리고 `--all` · `--files` 인자.
`FormatIncludeOrder` 와 `FormatClangFormat`(셰이더는 `FixerFileKind`)가 같은 규칙으로 파일을 고른다.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path


from common.Constants import kCppAllExtensions, kNotOurDirNames  # noqa: E402
from common.Host import getModifiedCppFiles  # noqa: E402
from common.Search import collectRepositoryFiles, collectSourceFiles, getLintSearchDirs, resolveFileArguments  # noqa: E402


def addFileArguments(parser: argparse.ArgumentParser) -> None:
    """대상 파일을 고르는 인자 — 픽서들이 같은 철자를 쓴다."""
    parser.add_argument(
        "--files",
        nargs="*",
        default=None,
        help="대상 C++ 파일 목록 (생략 시 Git 변경 파일, 없으면 전체 대상) ",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="프로젝트 전체 C++ 파일에 대해 실행",
    )


@dataclass(frozen=True)
class FixerFileKind:
    """
    C++ 가 아닌 파일(셰이더)을 고치는 픽서의 대상 — 확장자(대소문자 무시)와, 내려가지 않는 폴더 이름(빌드 산출물 · 남의 코드).
    """

    suffixes: tuple[str, ...] = ()
    excludedDirNames: frozenset[str] = kNotOurDirNames

    def isTarget(self, path: Path, repositoryRoot: Path) -> bool:
        if path.suffix.lower() not in self.suffixes:
            return False
        try:
            relative = path.resolve().relative_to(repositoryRoot.resolve()).as_posix()
        except ValueError:
            return False
        return not any(part in self.excludedDirNames for part in relative.split("/")[:-1])


def selectFixerTargetFiles(args: argparse.Namespace, repositoryRoot: Path, tag: str, fileKind: FixerFileKind | None = None) -> list[Path]:
    """
    무엇을 고칠지 고릅니다: 지정한 파일(`--files` — `common.Search.resolveFileArguments`) > `--all` > Git 변경 파일 > 전체.
    `fileKind` 가 없으면 C++ 소스, 있으면 그 종류(셰이더).

    마지막 폴백("변경된 파일이 없으면 전체")이 이 규칙의 핵심이다. 깨끗한 트리에서 돌려도
    아무 일도 안 하는 대신 전체를 본다 — 처음 받은 저장소에서도 한 번에 맞춰진다.
    """
    if fileKind is not None:
        return selectFileKindTargetsInternal(args, repositoryRoot, tag, fileKind)

    if args.files:
        return resolveFileArguments(repositoryRoot, args.files, suffixes=kCppAllExtensions)

    if args.all:
        return collectSourceFiles(getLintSearchDirs(repositoryRoot))

    listModified = getModifiedCppFiles(repositoryRoot)
    if listModified:
        print(f"[{tag}] Git 변경 파일 {len(listModified)}개 감지.", file=sys.stderr)
        return listModified

    listAll = collectSourceFiles(getLintSearchDirs(repositoryRoot))
    print(f"[{tag}] 변경된 파일이 없어 전체 {len(listAll)}개 파일 대상 실행.", file=sys.stderr)
    return listAll


def selectFileKindTargetsInternal(args: argparse.Namespace, repositoryRoot: Path, tag: str, fileKind: FixerFileKind) -> list[Path]:
    """`selectFixerTargetFiles` 의 C++ 가 아닌 갈래 — 같은 순서(`--files` > `--all` > Git 변경 > 전체), 같은 제외."""
    if args.files:
        listFile = resolveFileArguments(repositoryRoot, args.files, suffixes=fileKind.suffixes, excludedDirNames=fileKind.excludedDirNames)
        return [path for path in listFile if fileKind.isTarget(path, repositoryRoot)]

    if not args.all:
        listModified = [path for path in getModifiedCppFiles(repositoryRoot, extensions=set(fileKind.suffixes))
                        if fileKind.isTarget(path, repositoryRoot)]
        if listModified:
            print(f"[{tag}] Git 변경 파일 {len(listModified)}개 감지.", file=sys.stderr)
            return listModified

    listAll = [path for path in collectRepositoryFiles(repositoryRoot, ("",), suffixes=fileKind.suffixes, excludedDirNames=fileKind.excludedDirNames)
               if fileKind.isTarget(path, repositoryRoot)]
    if not args.all:
        print(f"[{tag}] 변경된 파일이 없어 전체 {len(listAll)}개 파일 대상 실행.", file=sys.stderr)
    return listAll
