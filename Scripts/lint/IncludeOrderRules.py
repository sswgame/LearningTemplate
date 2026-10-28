"""
Scripts/lint/IncludeOrderRules.py

include 순서 규칙 — `inspectText`(판정 + 고친 글)와 `fixText`(고침). 게이트 `CheckIncludeOrder` 와 픽서 `FormatIncludeOrder` 가 같이 읽는다.
"""

from __future__ import annotations

import re
from pathlib import Path

from common.ProjectConstants import kCppSourceExtensions, kLintTargetBaseDirNames
from common.Paths import getProjectRoot
from common.FileCollect import collectRepositoryFiles

_kIncludeRe = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]', re.MULTILINE)


def buildHeaderLookupInternal(repositoryRoot: Path, baseName: str) -> dict[str, str]:
    """
    `baseName`(Source · Test · Tools) 아래 헤더를 이름 · 소문자 상대 경로로 찾는 표입니다. 이름이 겹치는 헤더(pch.h 등)는 모호하므로 뺍니다.

    내려받은 외부 도구(`Tools/vcpkg` · `Tools/LLVM` …)로는 내려가지 않습니다(`collectRepositoryFiles`) — `glob("**/*.h")` 로 걸으면
    표 대부분이 vcpkg 헤더가 되고 파일 셋만 검사해도 표를 짓느라 1 초를 넘깁니다. 이 표는 따옴표 include 의
    경로 복원에만 쓰이고 외부 헤더는 꺾쇠로 include 하므로 결과는 같습니다.
    """
    baseDir = repositoryRoot / baseName
    lookupMap: dict[str, str] = {}
    nameCounts: dict[str, int] = {}
    for headerPath in collectRepositoryFiles(repositoryRoot, (baseName,), suffixes=(".h",)):
        rel = headerPath.relative_to(baseDir).as_posix()
        nameCounts[headerPath.name] = nameCounts.get(headerPath.name, 0) + 1
        lookupMap[headerPath.name] = rel
        lookupMap[rel.lower()] = rel
        lookupMap[headerPath.name.lower()] = rel
    # Remove ambiguous duplicates (like pch.h)
    return {k: v for k, v in lookupMap.items() if not (k in nameCounts and nameCounts[k] > 1)}


def buildHeaderLookupMap(repositoryRoot: Path) -> tuple[dict[str, str], dict[str, str], dict[str, str]]:
    return tuple(buildHeaderLookupInternal(repositoryRoot, baseName) for baseName in kLintTargetBaseDirNames)


_kConditionalOpenRe = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef)\b(.*)$')
_kConditionalBranchRe = re.compile(r'^\s*#\s*(?:elif|else)\b(.*)$')
_kConditionalEndRe = re.compile(r'^\s*#\s*endif\b')
_kConditionMacroRe = re.compile(r'\b[A-Za-z_]\w*\b')
_kNotConditionMacro = frozenset({"defined", "__has_include"})


def conditionFamilyInternal(macroName: str) -> str:
    """플랫폼 매크로는 하나의 사슬(`#if WINDOWS / #elif LINUX`)로 다루므로 같은 계열로 묶는다."""
    if macroName.startswith("SW_PLATFORM_"):
        return "SW_PLATFORM_*"
    return macroName


def findSplitConditionalIncludes(lines: list[str], relativeFilePath: str) -> list[str]:
    """
    include 를 담은 최상위 `#if` 블록 둘이 같은 조건 계열을 물으면 위반이다.
    순서 검사(위)는 첫 `#if` 에서 멈추므로 조건부 include 는 그 밖에 있다 — 같은 조건으로 프로젝트 헤더 블록과
    시스템 헤더 블록을 따로 여는 일이 그래서 생겼다. 한 사슬의 갈래 안에 프로젝트 헤더, 빈 줄, 시스템 헤더 순으로 둔다.
    """
    listBlock: list[tuple[int, set[str]]] = []
    listOpen: list[list] = []   # [시작 줄, 계열 집합, include 포함 여부]
    for lineIndex, line in enumerate(lines):
        openMatch = _kConditionalOpenRe.match(line)
        if openMatch:
            listOpen.append([lineIndex + 1, set(), False])
            condition = openMatch.group(1).split("//")[0]
        else:
            branchMatch = _kConditionalBranchRe.match(line)
            if branchMatch and listOpen:
                condition = branchMatch.group(1).split("//")[0]
            elif _kConditionalEndRe.match(line) and listOpen:
                block = listOpen.pop()
                if not listOpen and block[2] and block[1]:
                    listBlock.append((block[0], block[1]))
                continue
            else:
                if _kIncludeRe.match(line):
                    for block in listOpen:
                        block[2] = True
                continue
        for macroName in _kConditionMacroRe.findall(condition):
            if macroName not in _kNotConditionMacro and not macroName.isdigit():
                listOpen[-1][1].add(conditionFamilyInternal(macroName))

    violationsList = []
    mapFirstLine: dict[str, int] = {}
    for startLine, families in listBlock:
        for family in sorted(families):
            if family in mapFirstLine:
                violationsList.append(f"{relativeFilePath}:{startLine}: {family} 로 include 를 고르는 #if 블록이 {mapFirstLine[family]} 줄에도 있습니다 — "
                                      f"한 사슬로 합치고 갈래 안에 프로젝트 헤더 → 시스템 헤더 순으로 두십시오")
            else:
                mapFirstLine[family] = startLine
    return violationsList


def computeIncludeOrderInternal(text: str, relativeFilePath: str,
                                sourceHeaderMap: dict[str, str] | None,
                                testHeaderMap: dict[str, str] | None,
                                toolsHeaderMap: dict[str, str] | None) -> tuple[str, list[str]]:
    """규칙대로 정리한 글과, 정리로 풀 수 없는 위반(같은 조건의 include 블록 둘 등). 글은 줄끝 `\n` 으로 받는다."""
    violationsList = []
    isCpp = Path(relativeFilePath).suffix.lower() in kCppSourceExtensions
    
    lines = text.split('\n')
    violationsList.extend(findSplitConditionalIncludes(lines, relativeFilePath))
    inIfDirectiveLevel = 0
    boundaryIndex = -1
    
    # 1. 상단 인클루드 영역의 경계선(boundary) 찾기
    # 첫 번째 #if 나 매크로 정의, namespace, class 등이 나타나는 곳을 경계로 삼음
    for lineIndex, line in enumerate(lines):
        stripped = line.strip().lstrip('\ufeff')
        if stripped.startswith('#if'):
            if boundaryIndex == -1:
                boundaryIndex = lineIndex
            inIfDirectiveLevel += 1
        elif stripped.startswith('#endif'):
            inIfDirectiveLevel = max(0, inIfDirectiveLevel - 1)
        elif inIfDirectiveLevel == 0:
            if stripped and not stripped.startswith('//') and not stripped.startswith('/*') and not stripped.startswith('*') and not stripped.startswith('#include') and not stripped.startswith('#pragma'):
                if boundaryIndex == -1:
                    boundaryIndex = lineIndex

    if boundaryIndex == -1:
        boundaryIndex = len(lines)

    # 경계 줄에 빈 줄 없이 붙은 주석(doc 블록 · 구획 주석)은 그 코드의 것이다 — 아래 본문으로 넘긴다. 상단 영역에 두면 "include 영역과
    # 본문 사이 한 줄" 이 주석과 그것이 설명하는 코드 사이에 끼어 doc 블록이 떨어진다 규칙과 부딪친다).
    while boundaryIndex > 0 and boundaryIndex < len(lines):
        previous = lines[boundaryIndex - 1].strip()
        if previous == "" or previous.startswith(('#include', '#pragma', '#if', '#endif', '#else', '#elif')):
            break
        if previous.startswith(('//', '/*', '*')) is False:
            break
        boundaryIndex -= 1

    topLines = lines[:boundaryIndex]
    bottomLines = lines[boundaryIndex:]

    # 3. 상단 영역에서 모든 인클루드 추출 및 정렬/그룹핑
    nonIncludeLines = []
    includeLines = []
    firstIncludeIndex = -1

    for line in topLines:
        includeMatch = _kIncludeRe.match(line)
        if includeMatch:
            if firstIncludeIndex == -1:
                firstIncludeIndex = len(nonIncludeLines)
            includeLines.append(line)
        else:
            if line.strip() != "":  # 주석이나 pragma 유지, 빈 줄은 어차피 나중에 추가/포매팅됨
                nonIncludeLines.append(line)

    if firstIncludeIndex == -1:
        firstIncludeIndex = len(nonIncludeLines)

    seenIncludes = set()
    pchLine = None
    matchingHeaderLine = None
    localIncludesList = []
    systemIncludesList = []

    baseFileName = Path(relativeFilePath).stem

    for line in includeLines:
        includeMatch = _kIncludeRe.match(line)
        includeType = includeMatch.group(1)
        includeName = includeMatch.group(2)

        # <sw/...> 같은 프로젝트 자동 생성 헤더는 System Include가 아니므로 "" 로 변환
        if includeType == '<' and includeName.startswith("sw/"):
            includeType = '"'
            line = f'#include "{includeName}"'

        # 상대 경로 및 대소문자 정규화: Source/, Test/, Tools/ 상대 경로 복원 및 대소문자 교정
        if includeType == '"' and not includeName.endswith(".xxx") and includeName != "pch.h":
            includeLower = includeName.lower()
            if sourceHeaderMap and includeLower in sourceHeaderMap:
                normalizedPath = sourceHeaderMap[includeLower]
                line = f'#include "{normalizedPath}"'
                includeName = normalizedPath
            elif testHeaderMap and "Test" in relativeFilePath and includeLower in testHeaderMap:
                normalizedPath = testHeaderMap[includeLower]
                line = f'#include "{normalizedPath}"'
                includeName = normalizedPath
            elif toolsHeaderMap and "Tools" in relativeFilePath and includeLower in toolsHeaderMap:
                normalizedPath = toolsHeaderMap[includeLower]
                line = f'#include "{normalizedPath}"'
                includeName = normalizedPath

        # 중복 제거
        # 닫는 괄호를 **먼저 변수로 뽑는다.** f-string 식 안의 백슬래시는 Python 3.12(PEP 701) 부터
        # 허용된 것이고, CI 러너(ubuntu-22.04)의 `python3` 는 3.10 이라 SyntaxError 로 죽는다 —
        # 그리고 그 죽는 자리가 **CMake configure** 라 리눅스 CI 가 통째로 멈췄다.
        closingBracket = ">" if includeType == "<" else '"'
        includeFull = f"{includeType}{includeName}{closingBracket}"
        if not includeName.endswith(".xxx"):
            if includeFull in seenIncludes:
                continue
            seenIncludes.add(includeFull)

        if includeName == "pch.h":
            pchLine = line
        elif includeType == '<':
            systemIncludesList.append(line)
        else:
            # 매칭 헤더 판별 (대소문자 무시 비교). 단, .cpp 파일에서만 적용
            if isCpp and Path(includeName).stem.lower() == baseFileName.lower():
                matchingHeaderLine = line
            else:
                localIncludesList.append(line)

    # 로컬 인클루드는 알파벳순 정렬, 시스템/서드파티(<...>) 인클루드는 선언 순서 유지 (헤더 간 순서 의존성 보존)
    localIncludesList.sort()

    if isCpp and not pchLine:
        if "ThirdParty" not in relativeFilePath and "Tools/vcpkg" not in relativeFilePath:
            pchLine = '#include "pch.h"'
            violationsList.append(f'{relativeFilePath}: "pch.h"가 누락되어 자동 추가했습니다.')

    sortedIncludesList = []
    if pchLine:
        sortedIncludesList.append(pchLine)
        sortedIncludesList.append("")

    if matchingHeaderLine:
        sortedIncludesList.append(matchingHeaderLine)
        sortedIncludesList.append("")

    # 로컬 인클루드 폴더(루트) 기준으로 한 줄씩 띄우기
    lastRootFolder = None
    for line in localIncludesList:
        includeMatch = _kIncludeRe.match(line)
        includeName = includeMatch.group(2)
        parts = includeName.split('/')
        rootFolder = parts[0] if len(parts) > 1 else ""

        if lastRootFolder is not None and rootFolder != lastRootFolder:
            sortedIncludesList.append("")
        
        sortedIncludesList.append(line)
        lastRootFolder = rootFolder

    if systemIncludesList:
        if localIncludesList and sortedIncludesList and sortedIncludesList[-1] != "":
            sortedIncludesList.append("")
        for line in systemIncludesList:
            sortedIncludesList.append(line)

    while sortedIncludesList and sortedIncludesList[-1] == "":
        sortedIncludesList.pop()

    # 최종 상단 텍스트 조합
    if sortedIncludesList and nonIncludeLines[firstIncludeIndex:]:
        finalTopLines = nonIncludeLines[:firstIncludeIndex] + sortedIncludesList + [""] + nonIncludeLines[firstIncludeIndex:]
    else:
        finalTopLines = nonIncludeLines[:firstIncludeIndex] + sortedIncludesList + nonIncludeLines[firstIncludeIndex:]

    while finalTopLines and finalTopLines[-1] == "":
        finalTopLines.pop()

    while bottomLines and bottomLines[0] == "":
        bottomLines.pop(0)

    # 마지막 include(또는 상단 헤더 영역)와 하단 본문(namespace 등) 사이에 항상 1줄 띄우기
    if finalTopLines and bottomLines:
        finalTopLines.append("")

    # 4. (기존 검사 로직은 위에서 자동 추가로 대체됨)

    # 변경사항이 있다면 파일에 쓰기
    newText = "\n".join(finalTopLines + bottomLines)
    if text.endswith("\n") and not newText.endswith("\n"):
        newText += "\n"

    return newText, violationsList


#: 헤더 조회표(저장소 루트 → 표 셋) — 픽서는 파일마다 변환을 부르므로 한 번 만들어 둔다.
_s_mapHeaderLookup: dict[Path, tuple[dict[str, str], dict[str, str], dict[str, str]]] = {}


def inspectText(rawText: str, relative: str, headerMaps: tuple[dict[str, str], dict[str, str], dict[str, str]]) -> tuple[list[str], str | None]:
    """
    파일 하나의 판정과 고친 글 — 게이트 `CheckIncludeOrder` 와 픽서(`fixText`)가 **같은 이 함수**를 부른다. `rawText` 는 줄 끝을 건드리지 않고 읽은 원본이다.
    BOM 을 뗀 글로 판정하고, 고친 글은 원본의 BOM · 줄 끝으로 되돌린다. 줄 끝만 섞인 파일은 위반이 아니지만 고친 글은 줄 끝을 하나로 맞춘다.
    돌려주는 것은 `(위반 목록, 고친 글 — 고칠 것이 없으면 None)`.
    """
    text = rawText.replace("\r\n", "\n").replace("\r", "\n")
    lineEnding = "\r\n" if "\r\n" in rawText else "\n"
    body = text.removeprefix("\ufeff")
    newBody, listViolation = computeIncludeOrderInternal(body, relative, *headerMaps)
    if newBody != body:
        listViolation.append(f"{relative}: Include 순서/중복 문제가 발견되었습니다. "
                             f"(`py -3 Scripts/lint/fixer/FormatIncludeOrder.py --files {relative}` 로 고친다)")
    restored = ("\ufeff" if text.startswith("\ufeff") else "") + newBody.replace("\n", lineEnding)
    return listViolation, (restored if restored != rawText else None)


def fixText(text: str, relativeFilePath: str) -> tuple[str, bool]:
    """픽서 변환 — 게이트와 같은 `inspectText`. 헤더 조회표는 이 저장소의 것이다. 돌려주는 것은 `(고친 글, 바뀌었는가)`."""
    repositoryRoot = getProjectRoot().resolve()
    if repositoryRoot not in _s_mapHeaderLookup:
        _s_mapHeaderLookup[repositoryRoot] = buildHeaderLookupMap(repositoryRoot)
    _, fixedText = inspectText(text, relativeFilePath, _s_mapHeaderLookup[repositoryRoot])
    return (text, False) if fixedText is None else (fixedText, True)
