#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatModified.py

Git 작업 트리에서 수정되거나 새로 추가된(Untracked 포함) C++ 파일들에 대해서만
인클루드 순서 정리, 분기 중괄호 정리, clang-format 자동 포맷팅을 적용합니다.
"""

from __future__ import annotations

import sys
from pathlib import Path

# 부모 경로들을 sys.path에 추가하여 공통 스크립트 모듈 로드
scriptDir = Path(__file__).resolve().parent
sys.path.insert(0, str(scriptDir.parent))       # Scripts/lint — 사촌 린트 패키지
sys.path.insert(0, str(scriptDir.parents[1]))   # Scripts — common

from fixer import FormatBranchBraces
from fixer import FormatForwardDeclarations
from fixer import FormatIncludeOrder
from fixer import FormatNamespaceBlocks
from common import getModifiedCppFiles, getProjectRoot, runClangFormatBatch

#: 이 파일은 `LintFixer` 가 아니다 — 자기 변환이 없고, 다른 픽서 셋과 clang-format 을 **순서대로**
#: 부르는 조율자다. `CheckFixersAreAlive` 가 이 이유를 읽고 건너뛴다 (이유 없는 예외는 없다).
kFixerSkipReason = "픽서가 아니라 픽서 넷(include 순서 · namespace 블록 · 전방 선언 · 분기 중괄호)과 clang-format 을 순서대로 부르는 조율자다"


def main() -> int:

    projectRoot = getProjectRoot()
    modifiedFiles = getModifiedCppFiles(projectRoot)

    if not modifiedFiles:
        print("[FormatModified] 변경된 C++ 소스 파일이 없습니다.")
        return 0

    print(f"[FormatModified] {len(modifiedFiles)}개의 수정된 파일 발견.")

    # 1. include 순서 · 중복 정리(규칙은 gate/CheckIncludeOrder.py)
    print("\n[1/4] Include 순서 및 중복 정리 중...")
    includeResults = FormatIncludeOrder.FormatIncludeOrderFixer().processFiles(modifiedFiles, checkOnly=False)
    for msg in includeResults:
        print(f"  - {msg}")
    if not includeResults:
        print("  - Include 검사 OK")

    # 1b. 한 namespace 블록에 클래스 · 구조체 정의가 여럿이면 정의마다 블록을 나눈다(규칙은 gate/CheckNamespaceBlocks.py)
    for msg in FormatNamespaceBlocks.FormatNamespaceBlocksFixer().processFiles(modifiedFiles, checkOnly=False):
        print(f"  - {msg}")

    # 2. Forward Declaration 정렬 (enum -> struct -> class 및 그룹 간 빈 줄 삽입)
    print("\n[2/4] Forward Declaration 순서 및 그룹 정렬 중...")
    fwdResults = FormatForwardDeclarations.FormatForwardDeclarationsFixer().processFiles(modifiedFiles, checkOnly=False)
    if fwdResults:
        for msg in fwdResults:
            print(f"  - {msg}")
    else:
        print("  - Forward Declaration 검사 OK")

    # 3. 한 줄짜리 if 본문의 중괄호 제거 (clang-format 이 되돌리지 않는다)
    print("\n[3/4] 한 줄짜리 if 본문의 중괄호 정리 중...")
    braceResults = FormatBranchBraces.FormatBranchBracesFixer().processFiles(modifiedFiles, checkOnly=False)
    if braceResults:
        for msg in braceResults:
            print(f"  - {msg}")
    else:
        print("  - 분기 중괄호 검사 OK")

    # 4. clang-format 배치 실행 (in-place 포맷팅)
    print("\n[4/4] clang-format 실행 중...")
    resultCode = runClangFormatBatch(modifiedFiles, checkOnly=False, cwd=projectRoot)
    if resultCode != 0:
        print(f"\n[FormatModified] clang-format 실행 중 오류 발생 (exit {resultCode})")
        return resultCode

    print("\n[FormatModified] 성공적으로 모든 수정된 파일의 포맷팅을 완료했습니다!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
