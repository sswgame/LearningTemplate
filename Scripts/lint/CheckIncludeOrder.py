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
from common import collectSourceFiles, getProjectRoot, kCppSourceExtensions

_kIncludeRe = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]', re.MULTILINE)


def processFile(filePath: Path, repositoryRoot: Path) -> list[str]:
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

    # 2. 경계선 아래에 있는 글로벌 인클루드(#if 블록 바깥)를 찾아내어 위로 올리기
    inIfDirectiveLevel = 0
    misplacedIncludesList = []
    bottomLines = []
    
    for lineIndex in range(boundaryIndex, len(lines)):
        line = lines[lineIndex]
        stripped = line.strip()
        if stripped.startswith('#if'):
            inIfDirectiveLevel += 1
        elif stripped.startswith('#endif'):
            inIfDirectiveLevel = max(0, inIfDirectiveLevel - 1)
            
        includeMatch = _kIncludeRe.match(line)
        if includeMatch and inIfDirectiveLevel == 0:
            misplacedIncludesList.append(line)
            violationsList.append(f"{relativeFilePath}: 글로벌 인클루드({includeMatch.group(2)})가 #if나 코드 영역 아래에 있어 상단으로 이동되었습니다.")
        else:
            bottomLines.append(line)

    topLines = lines[:boundaryIndex]
    if misplacedIncludesList:
        topLines.extend(misplacedIncludesList)
        topLines.append("")

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

    # 사전순 정렬
    localIncludesList.sort()
    systemIncludesList.sort()

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
    finalTopLines = nonIncludeLines[:firstIncludeIndex] + sortedIncludesList + nonIncludeLines[firstIncludeIndex:]
    
    # 4. .cpp 파일의 경우 첫 번째 인클루드가 "pch.h"인지 검사 (검사 결과만 리포트)
    if isCpp and not pchLine:
        if "ThirdParty" not in relativeFilePath and "Tools/vcpkg" not in relativeFilePath:
            violationsList.append(f'{relativeFilePath}: .cpp 파일에 "pch.h" 인클루드가 없거나 최상단이 아닙니다.')

    # 변경사항이 있다면 파일에 쓰기
    newText = "\n".join(finalTopLines + bottomLines)
    if newText != text:
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

    sourceDirs = [
        repo / "Source",
        repo / "Test"
    ]

    allFiles = collectSourceFiles(sourceDirs)

    violations: list[str] = []
    maxWorkers = min(32, (os.cpu_count() or 4) * 2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=maxWorkers) as executor:
        futures = [executor.submit(processFile, path, repo) for path in allFiles]
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
