#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FormatBranchBraces.py

본문이 한 줄인 if / else if / else 분기의 중괄호를 제거합니다 (AGENTS.md '분기문 규칙').

  - if 계열만 대상이다. for / while / do 는 본문이 한 줄이어도 중괄호를 유지한다.
  - else / else if 가 붙은 사슬은 **모든 갈래가 한 줄일 때만** 중괄호를 벗긴다.
    한 갈래라도 여러 줄이면 그 사슬은 전부 중괄호를 유지한다.
  - 블록 안에 주석 줄·전처리기 지시문이 있으면 한 줄이 아니므로 건드리지 않는다.

clang-format 의 RemoveBracesLLVM 은 for/while 까지 같이 벗겨내 이 규칙을 표현하지 못한다.
그래서 clang-format 앞단에서 이 스크립트가 if 계열만 정리하고, 뒤이어 도는 clang-format 은
중괄호를 되돌리지 않는다 (InsertBraces 를 켜지 않았으므로).

사용법:
  py -3 Scripts/lint/FormatBranchBraces.py                    # Git 변경 파일 포맷팅
  py -3 Scripts/lint/FormatBranchBraces.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/FormatBranchBraces.py --all              # 전체 파일 포맷팅
  py -3 Scripts/lint/FormatBranchBraces.py --check            # 수정 없이 위반 여부만 검사
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
    useUtf8Stdout,
)

# 중괄호를 벗기면 안 되는 본문: 스스로 분기/반복을 여는 문장(달랑거리는 else 위험) 및 레이블.
_kNestedControlRe = re.compile(r"^(if|else|for|while|do|switch|case|default)\b")
_kIfHeadRe = re.compile(r"^if(\s+constexpr)?\s*\(")
_kElseIfHeadRe = re.compile(r"^else\s+if(\s+constexpr)?\s*\(")


def _maskLiteralsAndCommentsInternal(text: str) -> str:
    """
    문자열·문자·주석 내용을 공백으로 덮은 사본을 돌려준다. 길이와 줄 구조는 그대로라
    괄호/중괄호 세기와 줄 단위 비교를 원문 좌표 그대로 할 수 있다.
    """
    resultList: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        ch = text[index]

        # 원시 문자열 R"delim( ... )delim"
        if ch in "Rr" and index + 1 < length and text[index + 1] == '"':
            closeParen = text.find("(", index + 2)
            if closeParen != -1:
                delimiter = text[index + 2 : closeParen]
                terminator = ")" + delimiter + '"'
                endIndex = text.find(terminator, closeParen + 1)
                if endIndex != -1:
                    endIndex += len(terminator)
                    for maskedCh in text[index:endIndex]:
                        resultList.append("\n" if maskedCh == "\n" else " ")
                    index = endIndex
                    continue

        if ch == "/" and index + 1 < length and text[index + 1] == "/":
            endIndex = text.find("\n", index)
            endIndex = length if endIndex == -1 else endIndex
            resultList.append(" " * (endIndex - index))
            index = endIndex
            continue

        if ch == "/" and index + 1 < length and text[index + 1] == "*":
            endIndex = text.find("*/", index + 2)
            endIndex = length if endIndex == -1 else endIndex + 2
            for maskedCh in text[index:endIndex]:
                resultList.append("\n" if maskedCh == "\n" else " ")
            index = endIndex
            continue

        if ch == '"' or ch == "'":
            # 리터럴은 같은 줄에서 닫힐 때만 리터럴로 본다. 닫히지 않으면
            # 숫자 구분자(1'000'000) 같은 것이므로 그대로 흘려보낸다.
            scanIndex = index + 1
            bClosed = False
            while scanIndex < length and text[scanIndex] != "\n":
                if text[scanIndex] == "\\":
                    scanIndex += 2
                    continue
                if text[scanIndex] == ch:
                    bClosed = True
                    break
                scanIndex += 1
            if bClosed:
                resultList.append(ch)
                resultList.append(" " * (scanIndex - index - 1))
                resultList.append(ch)
                index = scanIndex + 1
                continue

        resultList.append(ch)
        index += 1

    return "".join(resultList)


def _isBodyStatementInternal(maskedLine: str, rawLine: str) -> bool:
    """한 줄짜리 본문으로 인정할 수 있는 문장인지 판정한다."""
    stripped = maskedLine.strip()
    if not stripped:
        return False
    if stripped.startswith("#"):
        return False
    if rawLine.rstrip().endswith("\\"):  # 매크로 줄바꿈
        return False
    if "{" in stripped or "}" in stripped:
        return False
    if _kNestedControlRe.match(stripped):
        return False
    if stripped.endswith(":"):  # 레이블
        return False
    return stripped.endswith(";")


def _findConditionEndInternal(listMasked: list[str], startIndex: int) -> int:
    """
    조건의 여는 괄호부터 짝이 맞는 닫는 괄호가 놓인 줄 번호를 돌려준다.
    그 줄에서 ')' 뒤에 다른 토큰이 있으면(예: 한 줄 if) -1.
    """
    depth = 0
    bSeenOpen = False
    lineIndex = startIndex
    while lineIndex < len(listMasked):
        line = listMasked[lineIndex]
        for ch in line:
            if ch == "(":
                depth += 1
                bSeenOpen = True
            elif ch == ")":
                depth -= 1
        if bSeenOpen and depth == 0:
            return lineIndex if line.rstrip().endswith(")") else -1
        if depth < 0:
            return -1
        lineIndex += 1
    return -1


def _parseBranchBodyInternal(listMasked: list[str], listRaw: list[str], bodyStartIndex: int):
    """
    분기 본문 하나를 읽는다.

    Returns:
        (다음 줄 번호, 여는 중괄호 줄 번호 또는 None, 한 줄 본문이면 True)
        해석할 수 없으면 None.
    """
    if bodyStartIndex >= len(listMasked):
        return None

    if listMasked[bodyStartIndex].strip() != "{":
        # 이미 중괄호가 없는 갈래 — 한 줄짜리 문장이어야 사슬 전체를 벗길 수 있다.
        bSingle = _isBodyStatementInternal(listMasked[bodyStartIndex], listRaw[bodyStartIndex])
        return (bodyStartIndex + 1, None, bSingle)

    closeIndex = bodyStartIndex + 2
    if closeIndex >= len(listMasked) or listMasked[closeIndex].strip() != "}":
        return (None, None, False)  # 여러 줄 블록 — 사슬 전체가 중괄호를 유지한다.

    bSingle = _isBodyStatementInternal(listMasked[bodyStartIndex + 1], listRaw[bodyStartIndex + 1])
    return (closeIndex + 1, bodyStartIndex, bSingle)


def formatBranchBraces(text: str) -> tuple[str, bool]:
    """
    한 줄짜리 if / else if / else 본문의 중괄호를 제거한 텍스트와 수정 여부를 돌려준다.
    """
    masked = _maskLiteralsAndCommentsInternal(text)
    listMasked = masked.split("\n")
    listRaw = text.split("\n")
    if len(listMasked) != len(listRaw):
        return text, False

    uniqueDropLine: set[int] = set()
    lineIndex = 0
    while lineIndex < len(listMasked):
        stripped = listMasked[lineIndex].strip()
        if not _kIfHeadRe.match(stripped):
            lineIndex += 1
            continue

        # 사슬 전체를 먼저 훑는다 — 한 갈래라도 여러 줄이면 아무것도 벗기지 않는다.
        listBraceLine: list[int] = []
        bAllSingle = True
        cursor = lineIndex
        bChainValid = True

        while True:
            conditionEnd = _findConditionEndInternal(listMasked, cursor)
            if conditionEnd == -1:
                bChainValid = False
                break

            parsed = _parseBranchBodyInternal(listMasked, listRaw, conditionEnd + 1)
            if parsed is None or parsed[0] is None:
                bChainValid = False
                break

            nextIndex, braceLine, bSingle = parsed
            if braceLine is not None:
                listBraceLine.append(braceLine)
            bAllSingle = bAllSingle and bSingle

            while nextIndex < len(listMasked) and not listMasked[nextIndex].strip():
                nextIndex += 1
            if nextIndex >= len(listMasked):
                break

            nextStripped = listMasked[nextIndex].strip()
            if _kElseIfHeadRe.match(nextStripped):
                cursor = nextIndex
                continue

            if nextStripped == "else":
                parsedElse = _parseBranchBodyInternal(listMasked, listRaw, nextIndex + 1)
                if parsedElse is None or parsedElse[0] is None:
                    bChainValid = False
                    break
                _, elseBraceLine, bElseSingle = parsedElse
                if elseBraceLine is not None:
                    listBraceLine.append(elseBraceLine)
                bAllSingle = bAllSingle and bElseSingle
                break

            if nextStripped.startswith("else"):
                bChainValid = False  # '} else {' 같은 한 줄 형태는 건드리지 않는다.
            break

        if bChainValid and bAllSingle and listBraceLine:
            for braceLine in listBraceLine:
                uniqueDropLine.add(braceLine)
                uniqueDropLine.add(braceLine + 2)

        lineIndex += 1

    if not uniqueDropLine:
        return text, False

    listKept = [line for index, line in enumerate(listRaw) if index not in uniqueDropLine]
    return "\n".join(listKept), True


def processFile(filePath: Path, checkOnly: bool = False) -> list[str]:
    """
    단일 파일의 분기 중괄호를 검사하거나 정리합니다.
    위반/수정 사항이 있으면 메시지 목록을 반환합니다.
    """
    try:
        with filePath.open("r", encoding="utf-8", errors="ignore", newline="") as file:
            content = file.read()
    except Exception as exception:
        return [f"[BranchBraces] {filePath} 읽기 실패: {exception}"]

    formattedContent, bModified = formatBranchBraces(content)
    if not bModified:
        return []

    if checkOnly:
        return [f"[BranchBraces] {filePath}: 한 줄짜리 if 본문에 불필요한 중괄호가 있습니다."]

    try:
        with filePath.open("w", encoding="utf-8", newline="") as file:
            file.write(formattedContent)
        return [f"[BranchBraces] {filePath}: 한 줄짜리 if 본문의 중괄호 제거 완료"]
    except Exception as exception:
        return [f"[BranchBraces] {filePath} 쓰기 실패: {exception}"]


def formatBranchBracesBatch(files: Sequence[Path], checkOnly: bool = False, maxWorkers: int = 8) -> list[str]:
    """
    여러 파일을 스레드 풀을 이용하여 병렬로 처리합니다.
    """
    if not files:
        return []

    allResults: list[str] = []
    workerCount = min(maxWorkers, len(files), os.cpu_count() or 4)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workerCount) as executor:
        futures = {executor.submit(processFile, path, checkOnly): path for path in files}
        for future in concurrent.futures.as_completed(futures):
            results = future.result()
            if results:
                allResults.extend(results)

    return allResults


def main(argv: Sequence[str] | None = None) -> int:
    useUtf8Stdout()

    parser = argparse.ArgumentParser(description="한 줄짜리 if / else if / else 본문의 중괄호 제거")
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
            print(f"[FormatBranchBraces] Git 변경 파일 {len(fileList)}개 감지.", file=sys.stderr)
        else:
            roots = getLintSearchDirs(root)
            fileList = collectSourceFiles(roots)
            print(f"[FormatBranchBraces] 변경된 파일이 없어 전체 {len(fileList)}개 파일 대상 실행.", file=sys.stderr)

    if not fileList:
        print("[FormatBranchBraces] 대상 C++ 파일이 없습니다.", file=sys.stderr)
        return 0

    results = formatBranchBracesBatch(fileList, checkOnly=args.check)
    if results:
        for message in results:
            print(message)

    if args.check and results:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
