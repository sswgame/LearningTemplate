#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/PreCommitLint.py

Git pre-commit 훅에서 호출되어 staged 파일에 게이트·픽서·포맷 검사를 돌립니다.
검사에 실패하면 커밋을 중단시킵니다.

**게이트 목록을 들지 않는다.** `gate/` 폴더를 훑고, 무엇이 staged 되었을 때 도는지는 게이트가
`LintGate.preCommitPattern` 으로 스스로 답한다. 게이트를 이름으로 import 하면 새 게이트가 빠지고,
C++ 파일이 staged 됐을 때만 돌리면 **`.cmake` 나 `.py` 만 커밋할 때 아무 게이트도 돌지 않는다.**

**병합 커밋은 새 내용만 파일 단위로 본다.** 병합 중(`MERGE_HEAD` 가 있다)이면 병합으로 바뀐 파일이 전부 staged 로 잡히는데,
그 대부분은 한쪽 부모와 바이트가 같다 — 그 부모 커밋을 만들 때 이 훅이 같은 내용을 이미 검사했다. 그래서 파일 단위 검사
(`--files` · 위치 인자를 받는 게이트, 픽서, clang-format)에는 **staged 내용이 어느 부모의 같은 경로 blob 과도 다른 파일**만
넘긴다(`listFilesUnlikeEveryParent`) — 충돌 해결 · 자동 병합으로 내용이 새로 생긴 파일이다. 파일 인자 없이 트리 전체를 보는
게이트와 셰이더 쿠킹 검증은 staged 전체를 기준으로 그대로 돈다. 두 부모에서 따로 온 파일끼리의 관계(헤더는 한쪽, 짝 `.cpp` 는
다른 쪽에서 온 생성자 초기화 순서 같은 것)는 파일 단위 검사가 원래 못 보는 것이라, 병합 뒤 `ctest -L lint`(CI 도 같다)가 트리
전체로 다시 본다.
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
    getMergeHeadRevisions,
    getProjectRoot,
    getStagedCppFiles,
    listFilesUnlikeEveryParent,
    runClangFormatBatch,
    runShaderCook,
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


def selectFileScopedStagedInternal(projectRoot: Path, listStaged: list[Path]) -> list[Path]:
    """
    파일 단위 검사에 넘길 staged 파일. 병합 중이 아니면 staged 전체, 병합 중이면 어느 부모와도 내용이 다른 파일만(모듈 머리말).
    """
    listMergeHead = getMergeHeadRevisions(projectRoot)
    if not listMergeHead:
        return list(listStaged)

    listScoped = listFilesUnlikeEveryParent(listStaged, ["HEAD", *listMergeHead], projectRoot)
    print(f"[PreCommitLint] 병합 커밋: staged {len(listStaged)}개 중 {len(listScoped)}개만 파일 단위로 검사합니다 "
          f"(나머지 {len(listStaged) - len(listScoped)}개는 한쪽 부모와 내용이 같아 그 부모 커밋 때 검사됐습니다 — "
          f"트리 전체 게이트는 그대로 돌고, 병합 뒤 `ctest -L lint` 가 전체를 다시 봅니다).")
    return listScoped


def runGatesInternal(projectRoot: Path, listStaged: list[Path], listFileScoped: list[Path]) -> bool:
    """
    `gate/` 에 있는 게이트를 **전부** 돌립니다 — 목록이 아니라 자리가 규칙이다.

    무엇이 staged 되었을 때 도는지, staged 부분집합을 어떻게 받는지는 게이트가 스스로 선언한다
    (`LintGate.preCommitPattern` · `preCommitFileArgument`). 여기에는 게이트 이름이 없다.

    파일을 인자로 받는 게이트는 `listFileScoped`(병합 커밋이면 새 내용인 파일만)에서, 트리 전체 게이트는 `listStaged` 에서 고른다.
    파일 인자 게이트에 넘길 파일이 하나도 없으면 돌리지 않는다 — 빈 `--files` 는 "전체를 훑어라" 로 읽힌다.
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

        if gateClass.preCommitFileArgument:
            listMatched = listMatchingStagedInternal(listFileScoped, projectRoot, gateClass.preCommitPattern)
            if not listMatched:
                print(f"\n{head} ... 건너뜀 (병합 커밋: 부모와 내용이 다른 해당 파일 없음)")
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

    # 후보 목록은 `common.AppBinary` 한 곳이다 — 여기서 따로 들면 프리셋 하나를 빼먹어(예: Ninja-Shipping)
    # 그 프리셋만 빌드해 둔 사람은 이 검증을 통째로 건너뛴다.
    appExe = findAppExecutable(projectRoot)
    if appExe is None:
        print("  [Warning] App.exe를 찾을 수 없어 셰이더 쿠킹 검증을 건너뜁니다. (빌드 후 다시 시도하세요)")
        return True

    res = runShaderCook(appExe, cwd=projectRoot, bCapture=True)

    if res.returncode != 0:
        print(f"  [Error] App.exe --cook-shaders 실행 실패 (종료 코드: {res.returncode})")
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
    # 폴더를 훑고, 무엇이 staged 되었을 때 도는지는 게이트가 스스로 답한다(모듈 독스트링 참고).
    listFileScoped = selectFileScopedStagedInternal(projectRoot, allStagedFiles)
    if runGatesInternal(projectRoot, allStagedFiles, listFileScoped):
        hasErrors = True

    # --- staged HLSL 이 있으면 전 백엔드 컴파일 검증 ----------------------------
    print("\n[셰이더] Staged 셰이더 전 RHI 백엔드(DX12, Vulkan, DX11) 컴파일 검증...")
    if not checkStagedShadersInternal(projectRoot, allStagedFiles):
        hasErrors = True

    # --- 픽서 — 게이트가 아니라 고쳐 주는 쪽이라 `--check` 로만 부른다 -----------
    setFileScoped = set(listFileScoped)
    stagedCppFiles = [path for path in getStagedCppFiles(projectRoot) if path in setFileScoped]
    if stagedCppFiles:
        print("\n[픽서] 전방 선언 순서 · 분기 중괄호 검사...")
        listFixer = (
            (FormatForwardDeclarations.FormatForwardDeclarationsFixer(), "전방 선언"),
            (FormatBranchBraces.FormatBranchBracesFixer(), "분기 중괄호"),
        )
        for filePath in stagedCppFiles:
            for fixer, label in listFixer:
                try:
                    listViolation = fixer.processFile(filePath, checkOnly=True)
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
