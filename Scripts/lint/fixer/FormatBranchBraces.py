#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatBranchBraces.py

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
    중괄호를 쓴 case 는 `break;` 를 중괄호 **안**에 둔다 — 이 저장소에서 break 는 본문의 일부지
    라벨의 종결자가 아니다.
  - 인자를 줄바꿈한 호출처럼 한 문장이 여러 줄에 걸친 것은 한 문장으로 센다.

**3) 한 switch 안에서 한 case 라도 중괄호가 있으면 본문이 있는 모든 case 에 중괄호를 씌운다.**

  - if 사슬의 "한 갈래라도 중괄호면 모두" 와 같은 일관성 규칙이다. `case A: return X;` 처럼 라벨과 같은 줄에
    본문이 있으면 라벨과 본문을 나눠 씌운다. 모든 case 가 한 문장인 표 모양 switch 는 그대로 둔다.
  - 본문이 없는 폴스루 라벨 · 전처리기 · 매크로 줄바꿈 · `[[fallthrough]]` 가 낀 본문은 2) 와 같이 건드리지 않는다.

**4) 본문에 중괄호가 없는 for · 범위 for · while 에 중괄호를 씌운다.**

  - 판정은 게이트 `Scripts/lint/gate/CheckLoopBraces.py` 한 자리다(`insertLoopBraces`). 여러 줄에 걸친 한 문장 · if/else 사슬 · 중첩 반복
    본문은 그 전체를 한 문장으로 감싸고, 같은 줄 본문(`for ( … ) f();`)은 줄을 끊어 감싸며, 빈 본문(`while ( x );`)은 `{}` 로 쓴다.
  - `do { … } while ( … )` 의 꼬리는 반복문 머리가 아니다. 머리 다음 줄부터 본문 끝까지 주석 · 전처리기 · 매크로 줄바꿈이 끼면
    건드리지 않는다(게이트가 "손으로" 로 알린다).

clang-format 은 둘 다 표현하지 못한다. RemoveBracesLLVM 은 for/while 까지 같이 벗겨내고,
InsertBraces 는 if/for/while 만 보고 case 라벨은 건드리지 않는다. 그래서 clang-format
앞단에서 이 스크립트가 돌고, 뒤이어 도는 clang-format 이 들여쓰기를 맞춘다.

사용법:
  py -3 Scripts/lint/fixer/FormatBranchBraces.py                    # Git 변경 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatBranchBraces.py [파일들...]        # 지정한 파일들만 포맷팅
  py -3 Scripts/lint/fixer/FormatBranchBraces.py --all              # 전체 파일 포맷팅
  py -3 Scripts/lint/fixer/FormatBranchBraces.py --check            # 수정 없이 위반 여부만 검사
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintFixer

from gate.CheckLoopBraces import insertLoopBraces  # noqa: E402
from LintFixer import FixPass, LintFixer  # noqa: E402
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
            if stripped.startswith("}") or _kCaseLabelRe.match(stripped) or _splitSameLineLabelInternal(stripped) >= 0:
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
            # 닫는 중괄호가 #if 의 반대편에 놓이면 한쪽 빌드에서만 짝이 맞는다.
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


def _computeOpenBraceOwnersInternal(listMasked: list[str]) -> list[int]:
    """
    줄마다 그 줄이 시작할 때 가장 안쪽에서 열려 있던 '{' 의 줄 번호를 돌려준다(없으면 -1).
    case 라벨의 이 값이 곧 그 라벨이 속한 switch 블록이다.
    """
    listOwner: list[int] = []
    stack: list[int] = []
    for lineIndex, line in enumerate(listMasked):
        listOwner.append(stack[-1] if stack else -1)
        for ch in line:
            if ch == "{":
                stack.append(lineIndex)
            elif ch == "}" and stack:
                stack.pop()
    return listOwner


def _splitSameLineLabelInternal(stripped: str) -> int:
    """
    `case A::B: return X;` 처럼 라벨과 본문이 한 줄인 마스킹된 줄에서 라벨을 끝내는 ':' 의 자리를 돌려준다.
    `::` 는 건너뛴다. 라벨 줄이 아니거나 ':' 뒤에 본문이 없으면 -1 이다.
    """
    if not (stripped.startswith("case") and (len(stripped) == 4 or not (stripped[4].isalnum() or stripped[4] == "_"))) and not stripped.startswith("default"):
        return -1
    index = 0
    while index < len(stripped):
        if stripped[index] == ":":
            if index + 1 < len(stripped) and stripped[index + 1] == ":":
                index += 2
                continue
            rest = stripped[index + 1 :].strip()
            return index if rest else -1
        index += 1
    return -1


def makeCaseBracesConsistent(text: str) -> tuple[str, bool]:
    """
    한 switch 안에 중괄호를 쓴 case 가 하나라도 있으면 본문이 있는 나머지 case 에도 중괄호를 씌운 텍스트와 수정 여부를 돌려준다.
    여러 문장인 case 는 앞 패스(`insertCaseBraces`)가 이미 씌웠으므로 여기서는 "이미 씌워진 것" 만 본다.
    """
    masked = _maskLiteralsAndCommentsInternal(text)
    listMasked = masked.split("\n")
    listRaw = text.split("\n")
    if len(listMasked) != len(listRaw):
        return text, False
    lineSuffix = "\r" if text.count("\r\n") * 2 > text.count("\n") else ""

    listDepth = _computeBraceDepthsInternal(listMasked)
    listOwner = _computeOpenBraceOwnersInternal(listMasked)

    # switch 블록(여는 '{' 의 줄)마다: 중괄호를 쓴 case 가 있는지, 씌울 후보(라벨만 있는 줄 · 라벨과 본문이 한 줄)
    mapHasBraced: dict[int, bool] = {}
    mapCandidate: dict[int, list[tuple[str, int, list[int]]]] = {}
    for labelIndex, maskedLine in enumerate(listMasked):
        stripped = maskedLine.strip()
        owner = listOwner[labelIndex]
        if _kCaseLabelRe.match(stripped):
            listBodyIndex, bAlreadyBraced = _collectCaseBodyInternal(listMasked, listDepth, labelIndex)
            if bAlreadyBraced:
                mapHasBraced[owner] = True
            elif listBodyIndex:
                mapCandidate.setdefault(owner, []).append(("block", labelIndex, listBodyIndex))
            continue
        colon = _splitSameLineLabelInternal(stripped)
        if colon >= 0:
            body = stripped[colon + 1 :].strip()
            if body.startswith("{"):
                mapHasBraced[owner] = True
            else:
                mapCandidate.setdefault(owner, []).append(("inline", labelIndex, [labelIndex]))

    mapOpenAfter: dict[int, list[str]] = {}
    mapCloseAfter: dict[int, list[str]] = {}
    mapReplace: dict[int, list[str]] = {}
    for owner, listCandidate in mapCandidate.items():
        if mapHasBraced.get(owner, False) is False:
            continue
        for kind, labelIndex, listBodyIndex in listCandidate:
            if any(listMasked[index].strip().startswith("#") for index in listBodyIndex):
                continue
            if any(listRaw[index].rstrip().endswith("\\") for index in listBodyIndex):
                continue
            if any("[[fallthrough]]" in listRaw[index] for index in listBodyIndex):
                continue
            labelLine = listRaw[labelIndex].rstrip("\r")
            indent = labelLine[: len(labelLine) - len(labelLine.lstrip())]
            if kind == "block":
                mapOpenAfter.setdefault(labelIndex, []).append(indent + "{" + lineSuffix)
                mapCloseAfter.setdefault(listBodyIndex[-1], []).insert(0, indent + "}" + lineSuffix)
                continue
            # 라벨과 본문이 한 줄: 마스킹본과 원문은 길이가 같으므로 같은 자리에서 자른다.
            maskedLine = listMasked[labelIndex]
            leading = len(maskedLine) - len(maskedLine.lstrip())
            colon = leading + _splitSameLineLabelInternal(maskedLine.strip())
            label = labelLine[: colon + 1].rstrip()
            body = labelLine[colon + 1 :].strip()
            mapReplace[labelIndex] = [
                label + lineSuffix,
                indent + "{" + lineSuffix,
                indent + "    " + body + lineSuffix,
                indent + "}" + lineSuffix,
            ]

    if not mapOpenAfter and not mapReplace:
        return text, False

    listOut: list[str] = []
    for index, line in enumerate(listRaw):
        if index in mapReplace:
            listOut.extend(mapReplace[index])
        else:
            listOut.append(line)
        listOut.extend(mapOpenAfter.get(index, ()))
        listOut.extend(mapCloseAfter.get(index, ()))
    return "\n".join(listOut), True


class FormatBranchBracesFixer(LintFixer):
    """
    if 를 먼저 벗기고 **그 다음에** case 를 센다 — 순서가 반대면 벗겨질 중괄호가 문장 수를 부풀린다.
    `listPass` 의 선언 순서가 그 계약이다.
    """

    tag = "BranchBraces"
    description = "한 줄짜리 if 본문의 중괄호 제거 · 여러 문장인 case 본문에 중괄호 추가 · 한 switch 안의 case 중괄호 일관성 · 반복문 본문 중괄호"
    listPass = (
        FixPass(
            transform=formatBranchBraces,
            problem="한 줄짜리 if 본문에 불필요한 중괄호가 있습니다.",
            done="한 줄짜리 if 본문의 중괄호 제거 완료",
            # 한 줄짜리 if 는 벗긴다.
            badSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    if ( count > 0 )\n    {\n        return;\n    }\n}\n"
            ),
            # for 는 본문이 한 줄이어도 유지한다 — 이 조각이 바뀌면 규칙이 너무 넓어진 것이다.
            goodSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n    {\n        doThing();\n    }\n}\n"
            ),
        ),
        FixPass(
            transform=insertCaseBraces,
            problem="본문이 여러 문장인 case 에 중괄호가 없습니다.",
            done="여러 문장인 case 본문에 중괄호 추가 완료",
            # 두 문장(호출 + break)이면 씌운다.
            badSample=(
                '#include "pch.h"\n\nvoid probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n        doThing();\n        break;\n    }\n}\n"
            ),
            # 한 문장짜리 본문은 그대로 둔다.
            goodSample=(
                '#include "pch.h"\n\nvoid probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n        return;\n    }\n}\n"
            ),
        ),
        FixPass(
            transform=makeCaseBracesConsistent,
            problem="같은 switch 안에서 중괄호를 쓴 case 와 쓰지 않은 case 가 섞여 있습니다.",
            done="switch 안의 case 중괄호를 한 모양으로 맞춤",
            # 중괄호를 쓴 case 옆의 한 문장 case(다음 줄 본문 · 같은 줄 본문)에도 씌운다.
            badSample=(
                '#include "pch.h"\n\nint32 probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n    {\n        doThing();\n        return 1;\n    }\n"
                "    case 1:\n        return 2;\n"
                "    case Mode::Two: return 3;\n"
                "    }\n    return 0;\n}\n"
            ),
            # 모든 case 가 한 문장인 표 모양 switch · 본문 없는 폴스루 라벨은 그대로 둔다.
            goodSample=(
                '#include "pch.h"\n\nconst char* probe( int32 mode )\n{\n    switch ( mode )\n    {\n'
                "    case 0:\n    case 1:\n        return \"low\";\n"
                "    case 2: return \"mid\";\n"
                "    default:\n        return \"high\";\n    }\n}\n"
            ),
        ),
        FixPass(
            transform=insertLoopBraces,
            problem="본문에 중괄호가 없는 반복문(for · while)이 있습니다.",
            done="반복문 본문에 중괄호 추가 완료",
            # 다음 줄 본문 · 같은 줄 본문 · 빈 본문을 모두 감싼다.
            badSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n        doThing( index );\n"
                "    for ( int32 value : listValue ) doThing( value );\n"
                "    while ( poll() );\n}\n"
            ),
            # 중괄호가 있는 반복문 · do-while 꼬리 · 머리와 본문 사이에 주석이 낀 반복문(손으로 고친다)은 그대로 둔다.
            goodSample=(
                '#include "pch.h"\n\nvoid probe( int32 count )\n{\n'
                "    for ( int32 index = 0; index < count; ++index )\n    {\n        doThing( index );\n    }\n"
                "    do\n    {\n        doThing( 0 );\n    } while ( poll() );\n"
                "    while ( poll() )\n        // 손으로 고칠 자리\n        doThing( 1 );\n}\n"
            ),
        ),
    )


main = FormatBranchBracesFixer.run


if __name__ == "__main__":
    sys.exit(main())
