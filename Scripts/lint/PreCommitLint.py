#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommitLint.py

Git pre-commit 훅에서 호출되어, 
Git Staged 상태인 C++ 파일들에 대해서만 인클루드 순서, 코딩 컨벤션 및 clang-format 포맷팅을 검사합니다.
검사에 실패하면 커밋을 중단시킵니다.
"""

from __future__ import annotations

import sys
from pathlib import Path

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# 부모 경로들을 sys.path에 추가하여 공통 스크립트 모듈 로드
scriptDir = Path(__file__).resolve().parent
sys.path.insert(0, str(scriptDir))
sys.path.insert(0, str(scriptDir.parent))

import CheckCodeConventions
import CheckIncludeOrder
import CheckResourceCasing
from common import (
    getAllStagedFiles,
    getProjectRoot,
    getStagedCppFiles,
    runClangFormatBatch,
)


def main() -> int:
    projectRoot = getProjectRoot()
    allStagedFiles = getAllStagedFiles(projectRoot)

    if not allStagedFiles:
        return 0

    print(f"[PreCommitLint] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")
    hasErrors = False

    # 1. Resource 소문자 명명 규칙 검사 (모든 Staged 파일 대상)
    print("\n[1/4] Resource 소문자 명명 규칙 검사...")
    allStagedPathStrings = [str(f) for f in allStagedFiles]
    resourceViolations = CheckResourceCasing.checkResourceCasing(projectRoot, allStagedPathStrings)
    if resourceViolations:
        hasErrors = True
        print(f"  [Error] Resource 하위 소문자 규칙 위반 {len(resourceViolations)}건 발견:")
        for rv in resourceViolations:
            print(f"    - {rv}")
    else:
        print("  - Resource 소문자 규칙 OK")

    stagedCppFiles = getStagedCppFiles(projectRoot)
    if stagedCppFiles:
        # 2. Include 순서 및 중복 검사
        print("\n[2/4] Include 순서 및 중복 검사...")
        sourceHeaderMap, testHeaderMap, toolsHeaderMap = CheckIncludeOrder.buildHeaderLookupMap(projectRoot)
        for filePath in stagedCppFiles:
            try:
                violations = CheckIncludeOrder.processFile(filePath, projectRoot, sourceHeaderMap, testHeaderMap, toolsHeaderMap, checkOnly=True)
                if violations:
                    hasErrors = True
                    for violation in violations:
                        print(f"    - {violation}")
            except Exception as exception:
                print(f"  [Warning] {filePath.relative_to(projectRoot)} 처리 중 오류: {exception}")

        # 3. 코딩 컨벤션 검사
        print("\n[3/4] 코딩 컨벤션 검사...")
        stagedPathStrings = [str(f) for f in stagedCppFiles]
        violations = CheckCodeConventions.runConventionsCheck(projectRoot, stagedPathStrings)
        if violations:
            hasErrors = True
            print(f"  [Error] 코딩 컨벤션 위반 {len(violations)}건이 발견되었습니다:")
            categoryGroups = {}
            for violation in violations:
                categoryGroups.setdefault(violation.rule_category, []).append(violation)
            for category, items in sorted(categoryGroups.items()):
                print(f"    [{category}]")
                for item in items:
                    print(f"      {item.file_path}:{item.line_number} -> {item.message}")
        else:
            print("  - 코딩 컨벤션 OK")

        # 4. clang-format 검사 (Dry-run with Werror)
        print("\n[4/4] clang-format 포맷팅 검사...")
        formatResult = runClangFormatBatch(stagedCppFiles, checkOnly=True, cwd=projectRoot)
        if formatResult != 0:
            hasErrors = True
            print(
                "  [Error] 포맷팅 규칙에 어긋나는 파일이 있습니다. "
                "'python Scripts/lint/FormatModified.py'를 실행하여 자동 수정한 뒤 다시 git add 하세요."
            )
        else:
            print("  - 포맷팅 OK")
    else:
        print("\n  - 검사 대상 C++ 파일 없음 (Resource/데이터 파일만 변경됨)")

    if hasErrors:
        print("\n[PreCommitLint] [FAIL] 검사에 실패했습니다. 오류를 수정한 후 다시 git add 하고 커밋해주세요.")
        return 1

    print("\n[PreCommitLint] [OK] 모든 검사를 통과했습니다.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
