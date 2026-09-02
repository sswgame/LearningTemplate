#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FormatForwardDeclarations.py

C++ 소스코드 내 전방 선언(Forward Declaration)을
enum (및 enum class) -> struct -> class 순서로 정렬하고,
각 그룹 간 1개의 공백 라인을 삽입하여 구분합니다.
각 그룹 내에서는 타입 이름 기준 알파벳 순으로 정렬합니다.

사용법:
  py -3 Scripts/lint/FormatForwardDeclarations.py                    # Git 변경 파일 포맷팅
  py -3 Scripts/lint/FormatForwardDeclarations.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/FormatForwardDeclarations.py --all              # 전체 파일 포맷팅
  py -3 Scripts/lint/FormatForwardDeclarations.py --check            # 수정 없이 위반 여부만 검사
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    collectSourceFiles,
    getLintSearchDirs,
    getModifiedCppFiles,
    getProjectRoot,
)

_kSingleFwdRe = re.compile(
    r"^(\s*)(?:template\s*<[^;{}>]+>\s*)?"
    r"(enum(?:\s+class|\s+struct)?|struct|class)\s+"
    r"(?:(?:SW_\w+_API|SW_API|SW_GF_API|SW_MODULE_API)\s+)?"
    r"([A-Za-z0-9_]+)"
    r"(?:\s*:\s*[A-Za-z0-9_:]+)?"
    r"\s*;(?:\s*//.*)?$"
)

_kTemplateHeaderRe = re.compile(r"^\s*template\s*<[^;{}>]+>\s*$")
_kSecondFwdRe = re.compile(
    r"^(\s*)"
    r"(enum(?:\s+class|\s+struct)?|struct|class)\s+"
    r"(?:(?:SW_\w+_API|SW_API|SW_GF_API|SW_MODULE_API)\s+)?"
    r"([A-Za-z0-9_]+)"
    r"(?:\s*:\s*[A-Za-z0-9_:]+)?"
    r"\s*;(?:\s*//.*)?$"
)

_kNonFwdWords = ("friend", "using", "typedef", "extern", "return", "case", "default")


def _getKindRankInternal(kindStr: str) -> int:
    stripped = kindStr.strip()
    if stripped.startswith("enum"):
        return 0
    if stripped == "struct":
        return 1
    if stripped == "class":
        return 2
    return 3


def _parseNextForwardDeclItemInternal(
    lines: list[str], index: int
) -> tuple[int, str, list[str], int] | None:
    """
    lines[index]부터 전방 선언 항목(1줄 또는 multiline template 2줄)을 파싱합니다.
    성공 시 (kindRank, typeName, linesList, nextIndex) 튜플을 반환하고,
    전방 선언이 아니면 None을 반환합니다.
    """
    if index >= len(lines):
        return None

    line = lines[index]
    stripped = line.strip()
    if not stripped or stripped.startswith(_kNonFwdWords):
        return None

    matchSingle = _kSingleFwdRe.match(line)
    if matchSingle:
        rank = _getKindRankInternal(matchSingle.group(2))
        typeName = matchSingle.group(3)
        return (rank, typeName, [line], index + 1)

    if _kTemplateHeaderRe.match(line) and index + 1 < len(lines):
        nextLine = lines[index + 1]
        matchSecond = _kSecondFwdRe.match(nextLine)
        if matchSecond:
            rank = _getKindRankInternal(matchSecond.group(2))
            typeName = matchSecond.group(3)
            return (rank, typeName, [line, nextLine], index + 2)

    return None


def formatForwardDeclarations(text: str) -> tuple[str, bool]:
    """
    주어진 C++ 코드 문자열에서 전방 선언 블록을 탐색하여
    enum -> struct -> class 순으로 정렬하고 그룹 간 1개의 공백 라인을 둡니다.
    반환값: (포맷팅된 문자열, 변경 여부)
    """
    newline = "\r\n" if "\r\n" in text else "\n"
    rawLines = text.split("\n")
    lines = [line[:-1] if line.endswith("\r") else line for line in rawLines]

    newLines: list[str] = []
    lineIndex = 0
    bModified = False

    while lineIndex < len(lines):
        item = _parseNextForwardDeclItemInternal(lines, lineIndex)
        if item is not None:
            blockItems = [item]
            startIndex = lineIndex
            currIndex = item[3]
            pendingBlankLines = 0

            while currIndex < len(lines):
                candidateLine = lines[currIndex]
                strippedCandidate = candidateLine.strip()
                if not strippedCandidate:
                    pendingBlankLines += 1
                    currIndex += 1
                    continue

                nextItem = _parseNextForwardDeclItemInternal(lines, currIndex)
                if nextItem is not None:
                    pendingBlankLines = 0
                    blockItems.append(nextItem)
                    currIndex = nextItem[3]
                    continue
                break

            # 블록 뒤에 붙어있던 공백 라인은 후속 일반 코드 영역의 빈 줄로 복원
            currIndex -= pendingBlankLines

            enums = [it for it in blockItems if it[0] == 0]
            structs = [it for it in blockItems if it[0] == 1]
            classes = [it for it in blockItems if it[0] == 2]

            enums.sort(key=lambda x: (x[1].lower(), x[1]))
            structs.sort(key=lambda x: (x[1].lower(), x[1]))
            classes.sort(key=lambda x: (x[1].lower(), x[1]))

            formattedBlock: list[str] = []
            groups = [enums, structs, classes]
            nonEmptyGroups = [group for group in groups if group]

            for groupIndex, group in enumerate(nonEmptyGroups):
                if groupIndex > 0:
                    formattedBlock.append("")
                for it in group:
                    formattedBlock.extend(it[2])

            originalBlock = lines[startIndex:currIndex]
            if formattedBlock != originalBlock:
                bModified = True
                newLines.extend(formattedBlock)
            else:
                newLines.extend(originalBlock)

            lineIndex = currIndex
        else:
            newLines.append(lines[lineIndex])
            lineIndex += 1

    return newline.join(newLines), bModified


def processFile(filePath: Path, checkOnly: bool = False) -> list[str]:
    """
    단일 파일을 읽어 전방 선언을 검사하거나 포맷팅합니다.
    위반/수정 사항이 있으면 메시지 목록을 반환합니다.
    """
    try:
        content = filePath.read_text(encoding="utf-8", errors="ignore")
    except Exception as exception:
        return [f"[ForwardDeclaration] {filePath} 읽기 실패: {exception}"]

    formattedContent, bModified = formatForwardDeclarations(content)
    if not bModified:
        return []

    if checkOnly:
        return [
            f"[ForwardDeclaration] {filePath}: 전방 선언 정렬(enum -> struct -> class 및 빈 줄)이 어긋났습니다."
        ]

    try:
        filePath.write_text(formattedContent, encoding="utf-8")
        return [f"[ForwardDeclaration] {filePath}: 전방 선언 정렬 완료"]
    except Exception as exception:
        return [f"[ForwardDeclaration] {filePath} 쓰기 실패: {exception}"]


def formatForwardDeclarationsBatch(
    files: Sequence[Path], checkOnly: bool = False, maxWorkers: int = 8
) -> list[str]:
    """
    여러 파일을 스레드 풀을 이용하여 병렬로 처리합니다.
    """
    if not files:
        return []

    allResults: list[str] = []
    workerCount = min(maxWorkers, len(files), os.cpu_count() or 4)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workerCount) as executor:
        futures = {
            executor.submit(processFile, path, checkOnly): path for path in files
        }
        for future in concurrent.futures.as_completed(futures):
            results = future.result()
            if results:
                allResults.extend(results)

    return allResults


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="전방 선언(Forward Declaration) 정렬 (enum -> struct -> class 및 그룹 간 빈 줄 삽입)"
    )
    parser.add_argument(
        "files",
        nargs="*",
        help="대상 C++ 파일 목록 (생략 시 Git 변경 파일, 없으면 전체 대상)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="프로젝트 전체 C++ 파일에 대해 실행",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="파일을 수정하지 않고 규칙 위반 여부만 검사",
    )
    args = parser.parse_args(argv)

    root = getProjectRoot()

    if args.files:
        fileList = [Path(f).resolve() for f in args.files if Path(f).is_file()]
    elif args.all:
        roots = getLintSearchDirs(root)
        fileList = collectSourceFiles(roots)
    else:
        modifiedFiles = getModifiedCppFiles(root)
        if modifiedFiles:
            fileList = modifiedFiles
            print(
                f"[FormatForwardDeclarations] Git 변경 파일 {len(fileList)}개 감지.",
                file=sys.stderr,
            )
        else:
            roots = getLintSearchDirs(root)
            fileList = collectSourceFiles(roots)
            print(
                f"[FormatForwardDeclarations] 변경된 파일이 없어 전체 {len(fileList)}개 파일 대상 실행.",
                file=sys.stderr,
            )

    if not fileList:
        print("[FormatForwardDeclarations] 대상 C++ 파일이 없습니다.", file=sys.stderr)
        return 0

    results = formatForwardDeclarationsBatch(fileList, checkOnly=args.check)
    if results:
        for message in results:
            print(message)

    if args.check and results:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
