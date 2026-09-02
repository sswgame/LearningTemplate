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
import FormatForwardDeclarations
from common import (
    getAllStagedFiles,
    getProjectRoot,
    getStagedCppFiles,
    runClangFormatBatch,
)


def checkStagedShadersInternal(projectRoot: Path, stagedFiles: list[Path]) -> bool:
    """Staged HLSL/HLSLI 파일이 있을 경우 전 RHI 백엔드(DX12, Vulkan, DX11) 컴파일 검증 및 바이너리 갱신을 확인합니다."""
    stagedShaders = [f for f in stagedFiles if f.suffix.lower() in (".hlsl", ".hlsli")]
    if not stagedShaders:
        print("  - Staged 셰이더 없음 (HLSL 검증 생략)")
        return True

    print(f"  - {len(stagedShaders)}개의 Staged 셰이더 소스 발견. 전 RHI 백엔드(DX12, Vulkan, DX11) 컴파일 검증 진행...")
    for s in stagedShaders:
        try:
            rel = s.relative_to(projectRoot)
        except ValueError:
            rel = s
        print(f"    * {rel}")

    candidates = [
        projectRoot / "build/Ninja-Debug/Bin/App.exe",
        projectRoot / "build/Ninja-Release/Bin/App.exe",
        projectRoot / "Bin/App.exe",
    ]
    appExe = None
    for c in candidates:
        if c.is_file():
            appExe = c
            break

    if not appExe:
        print("  [Warning] App.exe를 찾을 수 없어 셰이더 베이킹 검증을 건너뜁니다. (빌드 후 다시 시도하세요)")
        return True

    import subprocess
    res = subprocess.run(
        [str(appExe), "--bake-shaders"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        cwd=str(projectRoot),
    )

    if res.returncode != 0:
        print(f"  [Error] App.exe --bake-shaders 실행 실패 (종료 코드: {res.returncode})")
        if res.stdout:
            print(res.stdout)
        if res.stderr:
            print(res.stderr)
        return False

    hasCompileError = False
    for line in res.stdout.splitlines():
        if "Failed to compile shader" in line:
            hasCompileError = True
            print(f"  [Error] {line}")

    if hasCompileError:
        print("  [Error] 하나 이상의 RHI 백엔드에서 셰이더 컴파일 실패가 발생했습니다. HLSL 문법을 수정하세요.")
        return False

    print("  - 모든 RHI 백엔드(DirectX 12, Vulkan, DirectX 11) 컴파일 검증 통과.")

    gitCmd = subprocess.run(
        ["git", "status", "--porcelain", "Resource/engine/shaders/bin/", "Resource/common/shaders/bin/"],
        capture_output=True,
        text=True,
        cwd=str(projectRoot),
    )
    if gitCmd.returncode == 0 and gitCmd.stdout.strip():
        subprocess.run(["git", "add", "Resource/engine/shaders/bin/", "Resource/common/shaders/bin/"], cwd=str(projectRoot))
        print("  - 갱신된 RHI별 바이너리(.dxil, .spv, .dxbc)를 자동으로 Git Stage에 추가했습니다.")

    return True


def main() -> int:
    projectRoot = getProjectRoot()
    allStagedFiles = getAllStagedFiles(projectRoot)

    if not allStagedFiles:
        return 0

    print(f"[PreCommitLint] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")
    hasErrors = False

    # 1. Resource 소문자 명명 규칙 검사 (모든 Staged 파일 대상)
    print("\n[1/5] Resource 소문자 명명 규칙 검사...")
    allStagedPathStrings = [str(f) for f in allStagedFiles]
    resourceViolations = CheckResourceCasing.checkResourceCasing(projectRoot, allStagedPathStrings)
    if resourceViolations:
        hasErrors = True
        print(f"  [Error] Resource 하위 소문자 규칙 위반 {len(resourceViolations)}건 발견:")
        for rv in resourceViolations:
            print(f"    - {rv}")
    else:
        print("  - Resource 소문자 규칙 OK")

    # 2. Staged HLSL 셰이더 전 RHI 백엔드 컴파일 검증 및 바이너리 자동 스테이징
    print("\n[2/5] Staged 셰이더 전 RHI 백엔드(DX12, Vulkan, DX11) 컴파일 검증...")
    if not checkStagedShadersInternal(projectRoot, allStagedFiles):
        hasErrors = True

    stagedCppFiles = getStagedCppFiles(projectRoot)
    if stagedCppFiles:
        # 3. Include 순서 및 중복 검사
        print("\n[3/5] Include 순서 및 중복 검사...")
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

        # Forward Declaration 순서 및 그룹 간 빈 줄 검사
        for filePath in stagedCppFiles:
            try:
                fwdViolations = FormatForwardDeclarations.processFile(filePath, checkOnly=True)
                if fwdViolations:
                    hasErrors = True
                    for violation in fwdViolations:
                        print(f"    - {violation}")
            except Exception as exception:
                print(f"  [Warning] {filePath.relative_to(projectRoot)} Forward Declaration 검사 중 오류: {exception}")

        # 4. 코딩 컨벤션 검사
        print("\n[4/5] 코딩 컨벤션 검사...")
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

        # 5. clang-format 검사 (Dry-run with Werror)
        print("\n[5/5] clang-format 포맷팅 검사...")
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
