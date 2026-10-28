#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/Format.py

C++ · 셰이더 소스를 고친다 — include 순서 · 중복(`format-includes`, 규칙은 `IncludeOrder.py`)과 clang-format(`format`).
대상 고르기(`--files` > `--all` > git 변경분 > 전체)와 clang-format 일괄 실행(`runClangFormatBatch`, 커밋 훅도 부른다)이 한 자리다.

  py -3 -m Scripts format [--files ...|--all]            clang-format 적용(C++ 와 셰이더)
  py -3 -m Scripts format-includes [--files ...|--all] [--check]   include 순서 정리(--check 는 검사만, 위반이면 1)

파일을 고쳐 쓰되 줄 끝 · BOM 은 건드리지 않는다.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ClangFormat import resolveClangFormat  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.FileCollect import collectRepositoryFiles, collectSourceFiles, getLintSearchDirs, resolveFileArguments  # noqa: E402
from common.Git import kStagedFileArgument, listGitFiles  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ProjectConstants import kCppAllExtensions, kNotOurDirNames, kShaderSourceExtensions  # noqa: E402
from common.RunProcess import runProcess  # noqa: E402
from lint.IncludeOrder import fixText  # noqa: E402
from lint.Parallel import flatMapConcurrent, runUntilNonZero  # noqa: E402

kIncludeTag = "FormatIncludeOrder"
kClangFormatTag = "FormatClangFormat"


# --- git 이 알려 주는 대상 ------------------------------------------------------

def getStagedCppFiles(root: Path | None = None,
                      extensions: set[str] | None = None) -> list[Path]:
    """
    Git Staged(인덱스) 상태인 C++ 소스 및 헤더 파일 목록을 반환합니다.
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    return sorted(listGitFiles(root or getProjectRoot(), kStagedFileArgument, targetExtensions))


def getModifiedCppFiles(root: Path | None = None,
                        extensions: set[str] | None = None,
                        includeUntracked: bool = True) -> list[Path]:
    """
    Git 작업 트리에서 수정(Tracked)되었거나 새로 생성된(Untracked) C++ 소스/헤더 파일 목록을 반환합니다.
    """
    targetExtensions = extensions if extensions is not None else kCppAllExtensions
    projectRoot = root or getProjectRoot()

    # 1. 수정된 파일 (Staged + Unstaged)
    fileSet = listGitFiles(projectRoot, ["diff", "--name-only", "HEAD"], targetExtensions)
    # 2. 새로 추가된 파일 (Untracked)
    if includeUntracked:
        fileSet |= listGitFiles(projectRoot, ["ls-files", "--others", "--exclude-standard"], targetExtensions)
    return sorted(fileSet)


# --- clang-format 일괄 실행 -----------------------------------------------------

def runClangFormatBatch(files: Sequence[Path | str],
                        *,
                        checkOnly: bool = False,
                        cwd: Path | None = None,
                        clangFormatPath: str | None = None,
                        batchSize: int = 64) -> int:
    """
    지정된 파일 목록에 대해 clang-format을 배치 단위로 병렬 실행합니다.

    Args:
        files: 포맷팅할 파일 경로 목록
        checkOnly: True이면 --dry-run --Werror 모드로 검사만 수행, False이면 -i 모드로 파일 직접 수정
        cwd: 실행 기준 디렉터리 (기본값: 프로젝트 루트)
        clangFormatPath: 사용할 clang-format 실행 파일 경로 (None이면 자동 탐색)
        batchSize: 1회 배치당 처리할 파일 개수 (기본 64개)

    Returns:
        모든 배치가 성공하면 0, 실패 시 해당 프로세스 종료 코드 반환
    """
    if not files:
        return ExitCode.Passed
    projectRoot = cwd or getProjectRoot()
    clangFormatExe = clangFormatPath or resolveClangFormat()
    if not clangFormatExe:
        sys.stderr.write(
            "[FormatClangFormat] clang-format을 찾을 수 없습니다. "
            "'py -3 Scripts/setup/SetupEnvironment.py'를 먼저 실행해주세요.\n"
        )
        return ExitCode.Violation

    filePathStrings = [str(f) for f in files]
    batches = [
        filePathStrings[startIndex : startIndex + batchSize]
        for startIndex in range(0, len(filePathStrings), batchSize)
    ]

    def runSingleBatchInternal(batch: list[str]) -> int:
        command = [clangFormatExe]
        if checkOnly:
            command.extend(["--dry-run", "--Werror"])
        else:
            command.append("-i")
        command.extend(batch)
        return runProcess(command, cwd=projectRoot, bCapture=False).returnCode

    # clang-format 은 자식 프로세스라 코어 수를 넘겨 띄우면 서로 경합한다 (Parallel 의 프로세스 정책).
    return runUntilNonZero(runSingleBatchInternal, batches)


# --- 대상 고르기 ----------------------------------------------------------------

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
    무엇을 고칠지 고릅니다: 지정한 파일(`--files` — `common.FileCollect.resolveFileArguments`) > `--all` > Git 변경 파일 > 전체.
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


# --- include 순서 -----------------------------------------------------------------

class FormatIncludeOrderFixer:
    """include 순서 픽서 — 파일 하나를 읽어 `fixText` 로 고친다."""

    def processFile(self, filePath: Path, checkOnly: bool = False) -> list[str]:
        try:
            relativePath = filePath.resolve().relative_to(getProjectRoot().resolve()).as_posix()
        except ValueError:
            relativePath = filePath.as_posix()
        try:
            with filePath.open("r", encoding="utf-8", errors="strict", newline="") as file:
                content = file.read()
        except UnicodeDecodeError as error:
            # 고쳐 쓰면 UTF-8 이 아닌 바이트가 사라진다 — 읽지 못한 파일은 손대지 않고 알린다.
            return [f"[{kIncludeTag}] {filePath}: UTF-8 이 아니라 건너뜁니다({error.reason}, 바이트 {error.start}) — 파일을 UTF-8 로 저장하십시오"]
        except OSError as error:
            return [f"[{kIncludeTag}] {filePath} 읽기 실패: {error}"]

        formatted, bChanged = fixText(content, relativePath)
        if not bChanged:
            return []
        if checkOnly:
            return [f"[{kIncludeTag}] {filePath}: include 순서 · 중복이 규칙과 다르다"]
        try:
            with filePath.open("w", encoding="utf-8", newline="") as file:
                file.write(formatted)
        except OSError as error:
            return [f"[{kIncludeTag}] {filePath} 쓰기 실패: {error}"]
        return [f"[{kIncludeTag}] {filePath}: include 순서를 고쳤다"]

    def processFiles(self, files: Sequence[Path], checkOnly: bool = False, maxWorkers: int | None = None) -> list[str]:
        return flatMapConcurrent(lambda path: self.processFile(path, checkOnly), list(files), workerCount=maxWorkers)


def mainFormatIncludes(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="include 순서 · 중복을 고친다(IncludeOrderRules 의 규칙)")
    addFileArguments(parser)
    parser.add_argument("--check", action="store_true", help="파일을 수정하지 않고 규칙 위반 여부만 검사")
    args = parser.parse_args(argv)

    listFile = selectFixerTargetFiles(args, getProjectRoot(), kIncludeTag)
    if not listFile:
        print(f"[{kIncludeTag}] 대상 파일이 없습니다.", file=sys.stderr)
        return ExitCode.Passed
    listMessage = FormatIncludeOrderFixer().processFiles(listFile, checkOnly=args.check)
    for message in listMessage:
        print(message)
    return ExitCode.Violation if (args.check and listMessage) else ExitCode.Passed


# --- clang-format -----------------------------------------------------------------

kShaderFileKind = FixerFileKind(suffixes=tuple(sorted(kShaderSourceExtensions)))


def mainFormatClang(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C++ 소스코드에 clang-format을 적용합니다.")
    addFileArguments(parser)
    args = parser.parse_args(argv)

    root = getProjectRoot()
    listCpp = selectFixerTargetFiles(args, root, kClangFormatTag)
    listShader = selectFixerTargetFiles(args, root, kClangFormatTag, kShaderFileKind)
    fileList = listCpp + listShader

    if not fileList:
        sys.stderr.write(f"[{kClangFormatTag}] 포맷팅 대상 C++ · 셰이더 파일이 없습니다.\n")
        return ExitCode.Passed

    print(f"[{kClangFormatTag}] {len(fileList)}개 파일(셰이더 {len(listShader)}개)에 대해 clang-format 적용 중...", file=sys.stderr)
    return runClangFormatBatch(fileList, checkOnly=False, cwd=root)
