#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/FormatModified.py

Git 작업 트리에서 수정되거나 새로 추가된(Untracked 포함) C++ 파일들에 대해서만
인클루드 순서 정리 및 clang-format 자동 포맷팅을 적용합니다.
"""

from __future__ import annotations

import sys
from pathlib import Path

# 부모 경로들을 sys.path에 추가하여 공통 스크립트 모듈 로드
scriptDir = Path(__file__).resolve().parent
sys.path.insert(0, str(scriptDir))
sys.path.insert(0, str(scriptDir.parent))

import CheckIncludeOrder
import FormatForwardDeclarations
from common import getModifiedCppFiles, getProjectRoot, runClangFormatBatch


def main() -> int:
    projectRoot = getProjectRoot()
    modifiedFiles = getModifiedCppFiles(projectRoot)

    if not modifiedFiles:
        print("[FormatModified] 변경된 C++ 소스 파일이 없습니다.")
        return 0

    print(f"[FormatModified] {len(modifiedFiles)}개의 수정된 파일 발견.")

    # 1. CheckIncludeOrder 실행 (중복 제거 및 순서 자동 수정)
    print("\n[1/3] Include 순서 및 중복 검사 실행 중...")
    sourceHeaderMap, testHeaderMap, toolsHeaderMap = CheckIncludeOrder.buildHeaderLookupMap(projectRoot)
    allViolations = []
    for filePath in modifiedFiles:
        violations = CheckIncludeOrder.processFile(filePath, projectRoot, sourceHeaderMap, testHeaderMap, toolsHeaderMap)
        if violations:
            allViolations.extend(violations)

    if allViolations:
        for violation in allViolations:
            print(f"  - {violation}")
    else:
        print("  - Include 검사 OK")

    # 2. Forward Declaration 정렬 (enum -> struct -> class 및 그룹 간 빈 줄 삽입)
    print("\n[2/3] Forward Declaration 순서 및 그룹 정렬 중...")
    fwdResults = FormatForwardDeclarations.formatForwardDeclarationsBatch(modifiedFiles, checkOnly=False)
    if fwdResults:
        for msg in fwdResults:
            print(f"  - {msg}")
    else:
        print("  - Forward Declaration 검사 OK")

    # 3. clang-format 배치 실행 (in-place 포맷팅)
    print("\n[3/3] clang-format 실행 중...")
    resultCode = runClangFormatBatch(modifiedFiles, checkOnly=False, cwd=projectRoot)
    if resultCode != 0:
        print(f"\n[FormatModified] clang-format 실행 중 오류 발생 (exit {resultCode})")
        return resultCode

    print("\n[FormatModified] 성공적으로 모든 수정된 파일의 포맷팅을 완료했습니다!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
