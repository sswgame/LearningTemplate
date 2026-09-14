#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommitLint.py

Git pre-commit 훅에서 호출되어 staged 파일에 게이트·픽서·포맷 검사를 돌립니다.
검사에 실패하면 커밋을 중단시킵니다.

**게이트 목록을 들지 않는다.** `gate/` 폴더를 훑고, 무엇이 staged 되었을 때 도는지는 게이트가
`LintGate.preCommitPattern` 으로 스스로 답한다. 예전에는 여기서 게이트 여섯을 이름으로 import
했고, 그래서 (1) 게이트 열둘 중 여섯만 돌았으며 (2) 그 여섯조차 `if stagedCppFiles:` 안에 있어
**`.cmake` 나 `.py` 만 커밋하면 아무 게이트도 돌지 않았다.**
"""

from __future__ import annotations

import sys
from fnmatch import fnmatch
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

from LintCatalog import discoverLintScripts
from fixer import FormatBranchBraces
from fixer import FormatForwardDeclarations
from common import (
    findAppExecutable,
    findShaderCompileFailures,
    getAllStagedFiles,
    getProjectRoot,
    getStagedCppFiles,
    runClangFormatBatch,
    runShaderBake,
    useUtf8Stdout,
)


def listMatchingStagedInternal(listStaged: list[Path], projectRoot: Path,
                               listPattern: tuple[str, ...]) -> list[Path]:
    """패턴에 맞는 staged 파일만 고릅니다. 패턴이 비어 있으면 전부 (= 항상 도는 게이트)."""
    if not listPattern:
        return list(listStaged)

    listMatched: list[Path] = []
    for path in listStaged:
        try:
            relPath = path.relative_to(projectRoot).as_posix()
        except ValueError:
            relPath = path.as_posix()

        if any(fnmatch(relPath, pattern) for pattern in listPattern):
            listMatched.append(path)

    return listMatched


def runGatesInternal(projectRoot: Path, listStaged: list[Path]) -> bool:
    """
    `gate/` 에 있는 게이트를 **전부** 돌립니다 — 목록이 아니라 자리가 규칙이다.

    무엇이 staged 되었을 때 도는지, staged 부분집합을 어떻게 받는지는 게이트가 스스로 선언한다
    (`LintGate.preCommitPattern` · `preCommitFileArgument`). 여기에는 게이트 이름이 없다.
    """
    listScript = discoverLintScripts("gate")
    bFailed = False

    for index, script in enumerate(listScript, start=1):
        gateClass = script.gateClass
        if gateClass is None:
            continue

        head = f"[{index}/{len(listScript)}] {script.name}"

        if gateClass.preCommitSkipReason:
            print(f"\n{head} ... 건너뜀 ({gateClass.preCommitSkipReason})")
            continue

        listMatched = listMatchingStagedInternal(listStaged, projectRoot, gateClass.preCommitPattern)
        if gateClass.preCommitPattern and not listMatched:
            print(f"\n{head} ... 건너뜀 (해당 파일 변경 없음)")
            continue

        print(f"\n{head} ...")
        listArgument = ["--root", str(projectRoot)]
        if gateClass.preCommitFileArgument == "--files":
            listArgument += ["--files", *(str(path) for path in listMatched)]
        elif gateClass.preCommitFileArgument == "positional":
            listArgument += [str(path) for path in listMatched]

        if gateClass.run(listArgument) != 0:
            bFailed = True

    return bFailed


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

    # 후보 목록은 `common.AppBinary` 한 곳이다 — 예전에는 여기와 쿠커가 각자 들고 있었고,
    # 이쪽만 Ninja-Shipping 을 빼먹어서 Shipping 만 빌드해 둔 사람은 이 검증을 통째로 건너뛰었다.
    appExe = findAppExecutable(projectRoot)
    if appExe is None:
        print("  [Warning] App.exe를 찾을 수 없어 셰이더 베이킹 검증을 건너뜁니다. (빌드 후 다시 시도하세요)")
        return True

    res = runShaderBake(appExe, cwd=projectRoot, bCapture=True)

    if res.returncode != 0:
        print(f"  [Error] App.exe --bake-shaders 실행 실패 (종료 코드: {res.returncode})")
        if res.stdout:
            print(res.stdout)
        if res.stderr:
            print(res.stderr)
        return False

    listCompileError = findShaderCompileFailures(res.stdout or "")
    for line in listCompileError:
        print(f"  [Error] {line}")

    if listCompileError:
        print("  [Error] 하나 이상의 RHI 백엔드에서 셰이더 컴파일 실패가 발생했습니다. HLSL 문법을 수정하세요.")
        return False

    print("  - 모든 RHI 백엔드(DirectX 12, Vulkan, DirectX 11) 컴파일 검증 통과.")

    import subprocess
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
    useUtf8Stdout()

    projectRoot = getProjectRoot()
    allStagedFiles = getAllStagedFiles(projectRoot)

    if not allStagedFiles:
        return 0

    print(f"[PreCommitLint] {len(allStagedFiles)}개의 Staged 파일에 대해 검사를 시작합니다.")
    hasErrors = False

    # --- 게이트 — `gate/` 폴더가 목록이다 --------------------------------------
    #
    # 예전에는 여기서 게이트 여섯을 **이름으로 import** 했다. 게이트는 열둘이었고, 그중 셋은
    # 처음부터 빠져 있었으며(`CheckEngineLayers` · `CheckDataFileReferences` · `CheckSourceGlob`),
    # 나머지도 `if stagedCppFiles:` 안에 있어서 **`.cmake` 나 `.py` 만 커밋하면 아무 게이트도
    # 돌지 않았다.** 폴더를 훑고, 무엇이 staged 되었을 때 도는지는 게이트가 스스로 답한다.
    if runGatesInternal(projectRoot, allStagedFiles):
        hasErrors = True

    # --- staged HLSL 이 있으면 전 백엔드 컴파일 검증 ----------------------------
    print("\n[셰이더] Staged 셰이더 전 RHI 백엔드(DX12, Vulkan, DX11) 컴파일 검증...")
    if not checkStagedShadersInternal(projectRoot, allStagedFiles):
        hasErrors = True

    # --- 픽서 — 게이트가 아니라 고쳐 주는 쪽이라 `--check` 로만 부른다 -----------
    stagedCppFiles = getStagedCppFiles(projectRoot)
    if stagedCppFiles:
        print("\n[픽서] 전방 선언 순서 · 분기 중괄호 검사...")
        for filePath in stagedCppFiles:
            for processFile, label in (
                (FormatForwardDeclarations.processFile, "전방 선언"),
                (FormatBranchBraces.processFile, "분기 중괄호"),
            ):
                try:
                    listViolation = processFile(filePath, checkOnly=True)
                except Exception as exception:
                    print(f"  [Warning] {filePath.relative_to(projectRoot)} {label} 검사 중 오류: {exception}")
                    continue

                if listViolation:
                    hasErrors = True
                    for violation in listViolation:
                        print(f"    - {violation}")

        print("\n[포맷] clang-format 포맷팅 검사...")
        if runClangFormatBatch(stagedCppFiles, checkOnly=True, cwd=projectRoot) != 0:
            hasErrors = True
            print(
                "  [Error] 포맷팅 규칙에 어긋나는 파일이 있습니다. "
                "'python Scripts/lint/fixer/FormatModified.py'를 실행하여 자동 수정한 뒤 다시 git add 하세요."
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
