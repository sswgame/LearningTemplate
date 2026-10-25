"""
Scripts/lint/BranchBraceRules.py

분기문 중괄호 규칙 셋 — 한 줄짜리 if 의 중괄호 벗기기(`inspectIfBraces`), 여러 문장인 switch case 에 중괄호 씌우기(`inspectCaseBraces`),
한 switch 안의 case 중괄호를 한 모양으로 맞추기(`inspectCaseConsistency`). 픽서 `FormatBranchBraces` 가 읽는다(반복문은 `LoopBraceRules.py`).

판정과 고침은 한 함수다: 파일 하나(`SourceFile`)를 받아 고친 완성 글을 `FileFindings.fixedText` 로 돌려준다 — 픽서는 `FixPass(inspect=…)` 로 부르고,
줄 끝(CRLF) · BOM 은 `SourceFile.restore` 가 지킨다(규칙은 줄 끝을 `\\n` 으로 맞춘 글(`SourceFile.text`)만 본다). 이 규칙들에는 짝 게이트가 없다 — 짝이 생기면
같은 함수를 부른다.

문자열 · 문자 · 주석을 가린 글(`maskLiteralsAndCommentsInternal`)은 공유 가림(`SourceFile.masked`)과 다르다 — 리터럴의 **따옴표를 남긴다**(`"   "`). 문장의 끝 · 줄 전체가 리터럴인
줄(인자를 줄바꿈한 호출의 문자열 줄)을 빈 줄로 읽지 않게 하려는 것이라 이 규칙들 자리에 둔다.
"""

from __future__ import annotations

import re

from lint.SourceScan import FileFindings, SourceFile

# 중괄호를 벗기면 안 되는 본문: 스스로 분기/반복을 여는 문장(달랑거리는 else 위험) 및 레이블.
_kNestedControlRe = re.compile(r"^(if|else|for|while|do|switch|case|default)\b")
_kIfHeadRe = re.compile(r"^if(\s+constexpr)?\s*\(")
_kElseIfHeadRe = re.compile(r"^else\s+if(\s+constexpr)?\s*\(")

# switch 의 라벨. 본문이 라벨과 같은 줄에 있으면(`case A: return X;`) 끝이 ':' 가 아니라 걸리지 않는다.
_kCaseLabelRe = re.compile(r"^(?:case\b.*|default\s*):$")


def maskLiteralsAndCommentsInternal(text: str) -> str:
    """
    문자열·문자·주석 내용을 공백으로 덮은 사본을 돌려준다(따옴표는 남긴다). 길이와 줄 구조는 그대로라
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


def isBodyStatementInternal(maskedLine: str, rawLine: str) -> bool:
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


def findConditionEndInternal(listMasked: list[str], startIndex: int) -> int:
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


def parseBranchBodyInternal(listMasked: list[str], listRaw: list[str], bodyStartIndex: int) -> tuple[int | None, int | None, bool] | None:
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
        bSingle = isBodyStatementInternal(listMasked[bodyStartIndex], listRaw[bodyStartIndex])
        return (bodyStartIndex + 1, None, bSingle)

    closeIndex = bodyStartIndex + 2
    if closeIndex >= len(listMasked) or listMasked[closeIndex].strip() != "}":
        return (None, None, False)  # 여러 줄 블록 — 사슬 전체가 중괄호를 유지한다.

    bSingle = isBodyStatementInternal(listMasked[bodyStartIndex + 1], listRaw[bodyStartIndex + 1])
    return (closeIndex + 1, bodyStartIndex, bSingle)


def stripIfBracesInternal(text: str) -> str | None:
    """한 줄짜리 if / else if / else 본문의 중괄호를 제거한 글. 벗길 것이 없으면 None."""
    listMasked = maskLiteralsAndCommentsInternal(text).split("\n")
    listRaw = text.split("\n")
    if len(listMasked) != len(listRaw):
        return None

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
            conditionEnd = findConditionEndInternal(listMasked, cursor)
            if conditionEnd == -1:
                bChainValid = False
                break

            parsed = parseBranchBodyInternal(listMasked, listRaw, conditionEnd + 1)
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
                parsedElse = parseBranchBodyInternal(listMasked, listRaw, nextIndex + 1)
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
        return None

    return "\n".join(line for index, line in enumerate(listRaw) if index not in uniqueDropLine)


def computeBraceDepthsInternal(listMasked: list[str]) -> list[int]:
    """각 줄이 **시작될 때**의 중괄호 깊이를 돌려준다. 여는 줄은 깊이가 아직 오르기 전 값을 갖는다."""
    listDepth: list[int] = []
    depth = 0
    for line in listMasked:
        listDepth.append(depth)
        depth += line.count("{") - line.count("}")
    return listDepth


def collectCaseBodyInternal(listMasked: list[str], listDepth: list[int], labelIndex: int) -> tuple[list[int], bool]:
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
            if stripped.startswith("}") or _kCaseLabelRe.match(stripped) or splitSameLineLabelInternal(stripped) >= 0:
                break
            if not listBodyIndex and stripped.startswith("{"):
                return [], True
        listBodyIndex.append(lineIndex)
        lineIndex += 1
    return listBodyIndex, False


def countCaseStatementsInternal(listMasked: list[str], listBodyIndex: list[int]) -> int:
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


def addCaseBracesInternal(text: str) -> str | None:
    """본문이 두 문장 이상인 switch case / default 에 중괄호를 씌운 글. 씌울 것이 없으면 None."""
    listMasked = maskLiteralsAndCommentsInternal(text).split("\n")
    listRaw = text.split("\n")
    if len(listMasked) != len(listRaw):
        return None

    listDepth = computeBraceDepthsInternal(listMasked)
    mapOpenAfter: dict[int, list[str]] = {}
    mapCloseAfter: dict[int, list[str]] = {}

    for labelIndex, maskedLine in enumerate(listMasked):
        if not _kCaseLabelRe.match(maskedLine.strip()):
            continue

        listBodyIndex, bAlreadyBraced = collectCaseBodyInternal(listMasked, listDepth, labelIndex)
        if bAlreadyBraced or countCaseStatementsInternal(listMasked, listBodyIndex) < 2:
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
        mapOpenAfter.setdefault(labelIndex, []).append(indent + "{")
        mapCloseAfter.setdefault(listBodyIndex[-1], []).insert(0, indent + "}")  # 중첩이면 안쪽이 먼저 닫힌다.

    if not mapOpenAfter:
        return None

    listOut: list[str] = []
    for index, line in enumerate(listRaw):
        listOut.append(line)
        listOut.extend(mapOpenAfter.get(index, ()))
        listOut.extend(mapCloseAfter.get(index, ()))
    return "\n".join(listOut)


def computeOpenBraceOwnersInternal(listMasked: list[str]) -> list[int]:
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


def splitSameLineLabelInternal(stripped: str) -> int:
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


def unifyCaseBracesInternal(text: str) -> str | None:
    """
    한 switch 안에 중괄호를 쓴 case 가 하나라도 있으면 본문이 있는 나머지 case 에도 중괄호를 씌운 글. 바꿀 것이 없으면 None.
    여러 문장인 case 는 앞 패스(`addCaseBracesInternal`)가 이미 씌웠으므로 여기서는 "이미 씌워진 것" 만 본다.
    """
    listMasked = maskLiteralsAndCommentsInternal(text).split("\n")
    listRaw = text.split("\n")
    if len(listMasked) != len(listRaw):
        return None

    listDepth = computeBraceDepthsInternal(listMasked)
    listOwner = computeOpenBraceOwnersInternal(listMasked)

    # switch 블록(여는 '{' 의 줄)마다: 중괄호를 쓴 case 가 있는지, 씌울 후보(라벨만 있는 줄 · 라벨과 본문이 한 줄)
    mapHasBraced: dict[int, bool] = {}
    mapCandidate: dict[int, list[tuple[str, int, list[int]]]] = {}
    for labelIndex, maskedLine in enumerate(listMasked):
        stripped = maskedLine.strip()
        owner = listOwner[labelIndex]
        if _kCaseLabelRe.match(stripped):
            listBodyIndex, bAlreadyBraced = collectCaseBodyInternal(listMasked, listDepth, labelIndex)
            if bAlreadyBraced:
                mapHasBraced[owner] = True
            elif listBodyIndex:
                mapCandidate.setdefault(owner, []).append(("block", labelIndex, listBodyIndex))
            continue
        colon = splitSameLineLabelInternal(stripped)
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
            labelLine = listRaw[labelIndex]
            indent = labelLine[: len(labelLine) - len(labelLine.lstrip())]
            if kind == "block":
                mapOpenAfter.setdefault(labelIndex, []).append(indent + "{")
                mapCloseAfter.setdefault(listBodyIndex[-1], []).insert(0, indent + "}")
                continue
            # 라벨과 본문이 한 줄: 마스킹본과 원문은 길이가 같으므로 같은 자리에서 자른다.
            maskedLine = listMasked[labelIndex]
            leading = len(maskedLine) - len(maskedLine.lstrip())
            colon = leading + splitSameLineLabelInternal(maskedLine.strip())
            label = labelLine[: colon + 1].rstrip()
            body = labelLine[colon + 1 :].strip()
            mapReplace[labelIndex] = [label, indent + "{", indent + "    " + body, indent + "}"]

    if not mapOpenAfter and not mapReplace:
        return None

    listOut: list[str] = []
    for index, line in enumerate(listRaw):
        if index in mapReplace:
            listOut.extend(mapReplace[index])
        else:
            listOut.append(line)
        listOut.extend(mapOpenAfter.get(index, ()))
        listOut.extend(mapCloseAfter.get(index, ()))
    return "\n".join(listOut)


def toFindingsInternal(source: SourceFile, fixedBody: str | None) -> FileFindings:
    """고친 글(`source.text` 모양, 없으면 None) → 원본의 BOM · 줄 끝으로 되돌린 `FileFindings`."""
    return FileFindings(fixedText=None if fixedBody is None else source.restore(fixedBody))


def inspectIfBraces(source: SourceFile) -> FileFindings:
    """한 줄짜리 if / else if / else 본문의 중괄호를 벗긴다 — 한 사슬은 모든 갈래가 한 줄일 때만."""
    return toFindingsInternal(source, stripIfBracesInternal(source.text))


def inspectCaseBraces(source: SourceFile) -> FileFindings:
    """본문이 두 문장 이상인 switch case / default 에 중괄호를 씌운다."""
    return toFindingsInternal(source, addCaseBracesInternal(source.text))


def inspectCaseConsistency(source: SourceFile) -> FileFindings:
    """한 switch 안에 중괄호를 쓴 case 가 있으면 본문이 있는 나머지 case 에도 씌운다."""
    return toFindingsInternal(source, unifyCaseBracesInternal(source.text))
