#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Include 순서 및 스타일 검사 린터.

규칙:
1. .cpp 파일의 첫 번째 include는 무조건 "pch.h" 이어야 합니다.
2. 프로젝트 헤더 ("...") 인클루드가 시스템/외부 헤더 (<...>) 인클루드보다 먼저 와야 합니다.

사용법:
  python Scripts/lint/CheckIncludeOrder.py [--root <repo>]
"""

import argparse
import concurrent.futures
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import collectSourceFiles, getLintSearchDirs, getProjectRoot, kCppSourceExtensions

_kIncludeRe = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]', re.MULTILINE)


def buildHeaderLookupMap(repositoryRoot: Path) -> tuple[dict[str, str], dict[str, str], dict[str, str]]:
    sourceMap: dict[str, str] = {}
    sourceCounts: dict[str, int] = {}
    for headerPath in (repositoryRoot / "Source").glob("**/*.h"):
        rel = headerPath.relative_to(repositoryRoot / "Source").as_posix()
        sourceCounts[headerPath.name] = sourceCounts.get(headerPath.name, 0) + 1
        sourceMap[headerPath.name] = rel

    # Remove ambiguous duplicates (like pch.h)
    sourceMap = {k: v for k, v in sourceMap.items() if sourceCounts[k] == 1}

    testMap: dict[str, str] = {}
    testCounts: dict[str, int] = {}
    for headerPath in (repositoryRoot / "Test").glob("**/*.h"):
        rel = headerPath.relative_to(repositoryRoot / "Test").as_posix()
        testCounts[headerPath.name] = testCounts.get(headerPath.name, 0) + 1
        testMap[headerPath.name] = rel
    testMap = {k: v for k, v in testMap.items() if testCounts[k] == 1}

    toolsMap: dict[str, str] = {}
    toolsCounts: dict[str, int] = {}
    for headerPath in (repositoryRoot / "Tools").glob("**/*.h"):
        rel = headerPath.relative_to(repositoryRoot / "Tools").as_posix()
        toolsCounts[headerPath.name] = toolsCounts.get(headerPath.name, 0) + 1
        toolsMap[headerPath.name] = rel
    toolsMap = {k: v for k, v in toolsMap.items() if toolsCounts[k] == 1}

    return sourceMap, testMap, toolsMap


def processFile(filePath: Path, repositoryRoot: Path,
                sourceHeaderMap: dict[str, str] | None = None,
                testHeaderMap: dict[str, str] | None = None,
                toolsHeaderMap: dict[str, str] | None = None,
                checkOnly: bool = False) -> list[str]:
    relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="strict")
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

        # 상대 경로 정규화: "/" 가 없고 pch.h / .xxx 가 아닌 경우 Source/, Test/, Tools/ 상대 경로로 복원
        if includeType == '"' and "/" not in includeName and not includeName.endswith(".xxx") and includeName != "pch.h":
            if sourceHeaderMap and includeName in sourceHeaderMap:
                normalizedPath = sourceHeaderMap[includeName]
                line = f'#include "{normalizedPath}"'
                includeName = normalizedPath
            elif testHeaderMap and "Test" in relativeFilePath and includeName in testHeaderMap:
                normalizedPath = testHeaderMap[includeName]
                line = f'#include "{normalizedPath}"'
                includeName = normalizedPath
            elif toolsHeaderMap and "Tools" in relativeFilePath and includeName in toolsHeaderMap:
                normalizedPath = toolsHeaderMap[includeName]
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Include 순서 검사")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    args = parser.parse_args()
    repo = (args.root or getProjectRoot()).resolve()

    sourceDirs = getLintSearchDirs(repo)

    allFiles = collectSourceFiles(sourceDirs)
    sourceHeaderMap, testHeaderMap, toolsHeaderMap = buildHeaderLookupMap(repo)

    violations: list[str] = []
    maxWorkers = min(32, (os.cpu_count() or 4) * 2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(processFile, path, repo, sourceHeaderMap, testHeaderMap, toolsHeaderMap) for path in allFiles]
        for future in concurrent.futures.as_completed(futures):
            violations.extend(future.result())

    if violations:
        print("[CheckIncludeOrder] Include 순서 규칙 위반:")
        for violation in violations:
            print(f"  - {violation}")

    print(f"[CheckIncludeOrder] OK ({len(allFiles)} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
