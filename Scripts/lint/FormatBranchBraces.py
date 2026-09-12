#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FormatBranchBraces.py

중괄호의 모양을 정리합니다 (AGENTS.md '분기문 규칙'). 두 가지를 본다.

**1) 본문이 한 줄인 if / else if / else 의 중괄호를 제거한다.**

  - if 계열만 대상이다. for / while / do 는 본문이 한 줄이어도 중괄호를 유지한다.
  - else / else if 가 붙은 사슬은 **모든 갈래가 한 줄일 때만** 중괄호를 벗긴다.
    한 갈래라도 여러 줄이면 그 사슬은 전부 중괄호를 유지한다.
  - 블록 안에 주석 줄·전처리기 지시문이 있으면 한 줄이 아니므로 건드리지 않는다.

**2) 본문이 두 문장 이상인 switch 의 case / default 에 중괄호를 씌운다.**

  - 한 문장짜리 본문은 그대로 둔다 — `case A: return X;` 도, 라벨 다음 줄에 문장 하나가
    오는 형태도 대상이 아니다. 폴스루 라벨(본문이 없는 라벨)도 마찬가지다.
  - `break;` 는 본문의 한 문장으로 센다. 그래서 `문장 하나 + break;` 에는 중괄호가 붙는다.
    이미 중괄호가 있던 자리들이 예외 없이 `break;` 를 중괄호 **안**에 두고 있었다 —
    이 저장소에서 break 는 본문의 일부지 라벨의 종결자가 아니다.
  - 인자를 줄바꿈한 호출처럼 한 문장이 여러 줄에 걸친 것은 한 문장으로 센다.

clang-format 은 둘 다 표현하지 못한다. RemoveBracesLLVM 은 for/while 까지 같이 벗겨내고,
InsertBraces 는 if/for/while 만 보고 case 라벨은 건드리지 않는다. 그래서 clang-format
앞단에서 이 스크립트가 돌고, 뒤이어 도는 clang-format 이 들여쓰기를 맞춘다.

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

# switch 의 라벨. 본문이 라벨과 같은 줄에 있으면(`case A: return X;`) 끝이 ':' 가 아니라 걸리지 않는다.
_kCaseLabelRe = re.compile(r"^(?:case\b.*|default\s*):$")


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


def _computeBraceDepthsInternal(listMasked: list[str]) -> list[int]:
    """각 줄이 **시작될 때**의 중괄호 깊이를 돌려준다. 여는 줄은 깊이가 아직 오르기 전 값을 갖는다."""
    listDepth: list[int] = []
    depth = 0
    for line in listMasked:
        listDepth.append(depth)
        depth += line.count("{") - line.count("}")
    return listDepth


def _collectCaseBodyInternal(listMasked: list[str], listDepth: list[int], labelIndex: int) -> tuple[list[int], bool]:
    """
    case / default 라벨 하나의 본문을 이루는 줄 번호를 모은다.

    본문은 **같은 깊이의** 다음 라벨이나 switch 를 닫는 '}' 앞에서 끝난다. 중첩 switch 의
    라벨과 중괄호는 깊이가 더 깊으므로 바깥 본문을 끊지 않는다. 주석만 있는 줄은 마스킹되어
    비어 있으므로 문장 수에 들어가지 않는다.

    Returns:
        (본문 줄 번호 목록, 이미 중괄호로 열려 있으면 True)
    """
    labelDepth = listDepth[labelIndex]
    listBodyIndex: list[int] = []
    lineIndex = labelIndex + 1
    while lineIndex < len(listMasked):
        stripped = listMasked[lineIndex].strip()
        if not stripped:
            lineIndex += 1
            continue
        if listDepth[lineIndex] == labelDepth:
            if stripped.startswith("}") or _kCaseLabelRe.match(stripped):
                break
            if not listBodyIndex and stripped.startswith("{"):
                return [], True
        listBodyIndex.append(lineIndex)
        lineIndex += 1
    return listBodyIndex, False


def _countCaseStatementsInternal(listMasked: list[str], listBodyIndex: list[int]) -> int:
    """
    본문의 문장 수를 센다. 앞 줄이 ';' / '{' / '}' 로 끝나지 않았으면 이어지는 줄로 보므로,
    인자를 줄바꿈한 호출 한 개는 한 문장으로 센다.
    """
    count = 0
    bContinuing = False
    for index in listBodyIndex:
        if not bContinuing:
            count += 1
        bContinuing = not listMasked[index].strip().endswith((";", "{", "}"))
    return count


def insertCaseBraces(text: str) -> tuple[str, bool]:
    """
    본문이 두 문장 이상인 switch case / default 에 중괄호를 씌운 텍스트와 수정 여부를 돌려준다.
    """
    masked = _maskLiteralsAndCommentsInternal(text)
    listMasked = masked.split("\n")
    listRaw = text.split("\n")
    if len(listMasked) != len(listRaw):
        return text, False

    # 파일은 newline="" 로 읽혀 줄 끝의 '\r' 이 줄 문자열에 남아 있다. 끼워 넣는 줄에도
    # 같은 줄 끝을 붙여야 한 파일 안에서 CRLF 와 LF 가 섞이지 않는다.
    lineSuffix = "\r" if text.count("\r\n") * 2 > text.count("\n") else ""

    listDepth = _computeBraceDepthsInternal(listMasked)
    mapOpenAfter: dict[int, list[str]] = {}
    mapCloseAfter: dict[int, list[str]] = {}

    for labelIndex, maskedLine in enumerate(listMasked):
        if not _kCaseLabelRe.match(maskedLine.strip()):
            continue

        listBodyIndex, bAlreadyBraced = _collectCaseBodyInternal(listMasked, listDepth, labelIndex)
        if bAlreadyBraced or _countCaseStatementsInternal(listMasked, listBodyIndex) < 2:
            continue
        if any(listMasked[index].strip().startswith("#") for index in listBodyIndex):
            # 전처리기 지시문이 끼어 있으면 본문의 끝이 글자만으로 정해지지 않는다. 여는 중괄호와
            # 닫는 중괄호가 #if 의 반대편에 놓여 한쪽 빌드에서만 짝이 맞는 일이 실제로 있었다.
            continue
        if any(listRaw[index].rstrip().endswith("\\") for index in listBodyIndex):
            continue  # 매크로 줄바꿈 — 중괄호를 끼우면 이어붙던 줄이 끊긴다.
        if any("[[fallthrough]]" in listRaw[index] for index in listBodyIndex):
            continue  # 블록 안으로 들어가면 다음 라벨 바로 앞이 아니게 되어 경고가 난다.

        # IndentCaseLabels 가 켜져 있어 중괄호 없는 본문이 이미 라벨 + 한 단계에 놓여 있다.
        # 중괄호를 씌워도 본문이 있을 자리는 같으므로 들여쓰기는 건드리지 않는다.
        labelLine = listRaw[labelIndex]
        indent = labelLine[: len(labelLine) - len(labelLine.lstrip())]
        mapOpenAfter.setdefault(labelIndex, []).append(indent + "{" + lineSuffix)
        mapCloseAfter.setdefault(listBodyIndex[-1], []).insert(0, indent + "}" + lineSuffix)  # 중첩이면 안쪽이 먼저 닫힌다.

    if not mapOpenAfter:
        return text, False

    listOut: list[str] = []
    for index, line in enumerate(listRaw):
        listOut.append(line)
        listOut.extend(mapOpenAfter.get(index, ()))
        listOut.extend(mapCloseAfter.get(index, ()))
    return "\n".join(listOut), True


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

    # if 계열을 먼저 벗긴 뒤에 case 를 센다. 순서가 반대면 벗겨질 중괄호가 문장 수를 부풀린다.
    formattedContent, bBranchModified = formatBranchBraces(content)
    formattedContent, bCaseModified = insertCaseBraces(formattedContent)
    if not bBranchModified and not bCaseModified:
        return []

    if checkOnly:
        listMessage: list[str] = []
        if bBranchModified:
            listMessage.append(f"[BranchBraces] {filePath}: 한 줄짜리 if 본문에 불필요한 중괄호가 있습니다.")
        if bCaseModified:
            listMessage.append(f"[BranchBraces] {filePath}: 본문이 여러 문장인 case 에 중괄호가 없습니다.")
        return listMessage

    try:
        with filePath.open("w", encoding="utf-8", newline="") as file:
            file.write(formattedContent)
    except Exception as exception:
        return [f"[BranchBraces] {filePath} 쓰기 실패: {exception}"]

    listDone: list[str] = []
    if bBranchModified:
        listDone.append(f"[BranchBraces] {filePath}: 한 줄짜리 if 본문의 중괄호 제거 완료")
    if bCaseModified:
        listDone.append(f"[BranchBraces] {filePath}: 여러 문장인 case 본문에 중괄호 추가 완료")
    return listDone


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

    parser = argparse.ArgumentParser(description="한 줄짜리 if 본문의 중괄호 제거 · 여러 문장인 case 본문에 중괄호 추가")
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
