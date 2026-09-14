#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Include 순서 및 스타일 검사 린터.

규칙:
1. .cpp 파일의 첫 번째 include는 무조건 "pch.h" 이어야 합니다.
2. 프로젝트 헤더 ("...") 인클루드가 시스템/외부 헤더 (<...>) 인클루드보다 먼저 와야 합니다.

사용법:
  python Scripts/lint/gate/CheckIncludeOrder.py [--root <repo>]
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import collectSourceFiles, flatMapConcurrent, getLintSearchDirs, kCppSourceExtensions  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kIncludeRe = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]', re.MULTILINE)


def buildHeaderLookupMap(repositoryRoot: Path) -> tuple[dict[str, str], dict[str, str], dict[str, str]]:
    sourceMap: dict[str, str] = {}
    sourceCounts: dict[str, int] = {}
    for headerPath in (repositoryRoot / "Source").glob("**/*.h"):
        rel = headerPath.relative_to(repositoryRoot / "Source").as_posix()
        sourceCounts[headerPath.name] = sourceCounts.get(headerPath.name, 0) + 1
        sourceMap[headerPath.name] = rel
        sourceMap[rel.lower()] = rel
        sourceMap[headerPath.name.lower()] = rel

    # Remove ambiguous duplicates (like pch.h)
    sourceMap = {k: v for k, v in sourceMap.items() if not (k in sourceCounts and sourceCounts[k] > 1)}

    testMap: dict[str, str] = {}
    testCounts: dict[str, int] = {}
    for headerPath in (repositoryRoot / "Test").glob("**/*.h"):
        rel = headerPath.relative_to(repositoryRoot / "Test").as_posix()
        testCounts[headerPath.name] = testCounts.get(headerPath.name, 0) + 1
        testMap[headerPath.name] = rel
        testMap[rel.lower()] = rel
        testMap[headerPath.name.lower()] = rel
    testMap = {k: v for k, v in testMap.items() if not (k in testCounts and testCounts[k] > 1)}

    toolsMap: dict[str, str] = {}
    toolsCounts: dict[str, int] = {}
    for headerPath in (repositoryRoot / "Tools").glob("**/*.h"):
        rel = headerPath.relative_to(repositoryRoot / "Tools").as_posix()
        toolsCounts[headerPath.name] = toolsCounts.get(headerPath.name, 0) + 1
        toolsMap[headerPath.name] = rel
        toolsMap[rel.lower()] = rel
        toolsMap[headerPath.name.lower()] = rel
    toolsMap = {k: v for k, v in toolsMap.items() if not (k in toolsCounts and toolsCounts[k] > 1)}

    return sourceMap, testMap, toolsMap


def processFile(filePath: Path, repositoryRoot: Path,
                sourceHeaderMap: dict[str, str] | None = None,
                testHeaderMap: dict[str, str] | None = None,
                toolsHeaderMap: dict[str, str] | None = None,
                checkOnly: bool = False) -> list[str]:
    relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8-sig", errors="strict")
    except Exception as exception:
        return [f"[CheckIncludeOrder] 읽기 실패: {relativeFilePath}: {exception}"]

    violationsList = []
    isCpp = filePath.suffix.lower() in kCppSourceExtensions
    
    lines = text.split('\n')
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
        includeFull = f"{includeType}{includeName}{'>' if includeType == '<' else '\"'}"
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

    if newText != text:
        if checkOnly:
            violationsList.append(f'{relativeFilePath}: Include 순서/중복 문제가 발견되었습니다. (FormatModified.py를 실행하세요)')
        else:
            try:
                filePath.write_text(newText, encoding="utf-8")
            except Exception as exception:
                violationsList.append(f'{relativeFilePath}: 파일 쓰기 실패: {exception}')

    return violationsList


class CheckIncludeOrderGate(LintGate):
    """
    기본은 **검사만** 한다. 고치려면 `--fix` 를 준다.

    예전에는 정반대였다: 인자가 `--root` 뿐이라 `main()` 이 늘 **고치는 모드**로 돌았고, 위반을
    찍은 뒤에도 무조건 `0` 을 돌려줬다. 그래서 CTest 에 "게이트" 로 등록돼 있는데도
    **실패할 수가 없었고**, 대신 소스를 조용히 고쳐 놓았다(2026-09-14 에 음성 테스트가 잡았다).
    고치는 일은 `FormatModified.py` 가 `processFile(...)` 을 직접 불러서 한다 — 그쪽은 그대로다.
    """

    description = "Include 순서 검사"
    violationHeader = "Include 순서 규칙 위반"
    selfTestCases = [
        {
            "name": "Engine 이 Core 보다 앞",
            "files": {
                "Source/Engine/Probe/Probe.cpp": (
                    '#include "pch.h"\n\n'
                    '#include "Engine/Common/EngineServices.h"\n'
                    '#include "Core/File/FileUtil.h"\n'
                ),
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--fix", action="store_true", help="보고만 하지 않고 파일을 고칩니다")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        allFiles = collectSourceFiles(getLintSearchDirs(repositoryRoot))
        sourceHeaderMap, testHeaderMap, toolsHeaderMap = buildHeaderLookupMap(repositoryRoot)

        violations = flatMapConcurrent(
            lambda path: processFile(path, repositoryRoot, sourceHeaderMap, testHeaderMap, toolsHeaderMap,
                                     checkOnly=not args.fix),
            allFiles,
        )
        return GateResult(listViolation=violations, summary=f"{len(allFiles)} files scanned")


main = CheckIncludeOrderGate.run


if __name__ == "__main__":
    sys.exit(main())
