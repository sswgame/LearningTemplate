#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CMake 소스 GLOB 누락 및 컴파일 데이터베이스 일치 검사.

현재 빌드 트리(compile_commands.json)와 디스크의 C++ 소스 파일 목록을 대조하여,
새로 추가된 .cpp/.c 파일이 빌드 타겟 및 LSP 인덱서에 정상 등록되었는지 검사합니다.

**구성이 일부러 짓지 않는 소스는 CMake 가 적은 목록으로만 안다** (`<빌드>/generated/sw/config/UnbuiltSources.txt`).
배포 구성의 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드, 다른 OS 의 Core 소스, 짓지 않는 RHI 모듈 엔트리 · DX 모듈(윈도우 밖),
고르지 않은 게임 팩, 끈 GameFramework 가 그렇다. 무엇을 빼는지는 CMake 가 정하므로 빼는 자리가 직접 적고(`sw_declareUnbuiltSources` ·
`sw_excludeUnbuiltSources` · `sw_declareUnbuiltDirectory`, `cmake/Engine/TargetRules.cmake`), 여기서는 그 목록만 읽는다 — 이 게이트에는
플랫폼 · 게임 이름을 적은 무시 목록이 없다. 목록에 없는데 지어지지 않은 소스는 위반이다.

(Ninja 빌드는 소스 GLOB 의 `CONFIGURE_DEPENDS` 로 추가 · 삭제를 감지하지만,
 이 게이트는 전체 빌드 없이 빠르게 소스 누락을 막는 데 씁니다.)

  python Scripts/lint/gate/CheckSourceGlob.py [--root <repo>] [--preset <이름> | --build-dir <dir>]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import (  # noqa: E402
    BuildTree,
    BuildTreeError,
    addBuildTreeArguments,
    collectSourceFiles,
    kUnbuiltSourceListRelPath,
    kCppSourceExtensions,
    kDirSourceApp,
    kDirSourceCore,
    kDirSourceEditor,
    kDirSourceEngine,
    kDirSourceGameFramework,
    kDirSourceGames,
)
from LintGate import GateResult, LintGate  # noqa: E402

_kScanRoots = (
    kDirSourceEngine,
    kDirSourceApp,
    kDirSourceEditor,
    kDirSourceGameFramework,
    kDirSourceGames,
    kDirSourceCore,
)

class CheckSourceGlobGate(LintGate):
    """
    빌드가 실제로 컴파일하는 목록과 디스크의 소스를 대조한다.

    본 검사는 **빌드 트리의 compile_commands.json** 과 대조한다(빌드 없이 돌리면 스스로 "소스 목록만 보고" 하고 0 을 돌려준다).
    고르는 빌드 폴더는 `--build-dir`(CTest 가 `${CMAKE_BINARY_DIR}` 를 넘긴다) · `--preset`, 없으면 `.clangd` 가 가리키는 트리다(`BuildTree`).
    자가 시험은 임시 트리에 `build/compile_commands.json` 을 직접 써서 `--build-dir build` 로 돌린다 — "짓지 않는 소스" 목록이 있어도 거기 없는
    빠진 소스는 잡는지 본다(목록이 게이트를 눈멀게 하지 않는지).
    """

    description = "소스 GLOB 누락 검사"
    buildComment = "Checking source GLOB coverage vs compile_commands..."
    timeoutSeconds = 15
    preCommitSkipReason = "빌드 디렉터리(--build-dir)가 있어야 글롭과 대조할 수 있다 — 커밋 훅은 그것을 모른다"
    listCtestArgument = ("--build-dir", "${CMAKE_BINARY_DIR}")
    maxViolationShown = 40
    hint = ("  reconfigure 가 필요합니다. 이 구성이 일부러 짓지 않는 소스라면\n"
            "  빼는 자리에서 sw_excludeUnbuiltSources · sw_declareUnbuiltSources 로 적으세요(cmake/Engine/TargetRules.cmake).")
    selfTestCases = [
        {
            "name": "짓지 않는 소스 목록에 없는데 compile_commands 에도 없는 소스",
            "files": {
                "Source/Engine/Graphics/RHI/Vulkan/Probe.cpp": "// 아무도 짓지 않는다\n",
                "Source/Editor/Declared.cpp": "// 이 구성이 짓지 않는다고 적혀 있다\n",
                "build/compile_commands.json": "[]\n",
                "build/generated/sw/config/UnbuiltSources.txt": "Source/Editor/Declared.cpp\n",
            },
            "args": ["--build-dir", "build"],
        },
        {
            # 게이트에 플랫폼 · 게임 무시 목록이 없다 — 다른 OS 폴더 · 다른 게임 팩도 목록에 없으면 위반이다.
            "name": "다른 OS 폴더 · 다른 게임 팩의 소스도 목록에 없으면 위반",
            "files": {
                # 짓지 않는 소스로 적혀 있어 위반이 아니다. 남는 위반은 아래 둘뿐이다.
                "Source/Engine/Graphics/RHI/Vulkan/Built.cpp": "// 짓지 않는 소스로 적혀 있다\n",
                "Source/Core/File/Linux/ProbeLinux.cpp": "// 이 구성이 짓지 않는데 아무도 적지 않았다\n",
                "Source/Games/Other/ProbeGame.cpp": "// 고르지 않은 게임 팩인데 아무도 적지 않았다\n",
                "build/compile_commands.json": "[]\n",
                "build/generated/sw/config/UnbuiltSources.txt": "Source/Engine/Graphics/RHI/Vulkan/Built.cpp\n",
            },
            "args": ["--build-dir", "build"],
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        addBuildTreeArguments(parser, defaultPreset=None)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations: list[str] = []

        scanDirs = [repositoryRoot / rel for rel in _kScanRoots]
        sources = collectSourceFiles(scanDirs, extensions=kCppSourceExtensions)
        summary = f"{len(sources)} sources scanned under Source/"

        tree = BuildTree.fromArguments(args, repositoryRoot)
        try:
            data = tree.readCompileDatabase()
        except BuildTreeError:
            return GateResult(
                listViolation=violations,
                listNote=["compile_commands.json 없음 — 소스 목록만 보고합니다"],
                summary=summary,
            )

        # Unity 빌드는 소스를 unity_N_cxx.cxx 로 묶어 컴파일하므로 개별 .cpp 가 DB 에 없다.
        # 그 빌드 트리에서는 이 검사가 성립하지 않는다 — 없다고 답하는 대신 성립하지 않는다고 말한다.
        if any("unity_" in entry.get("file", "") for entry in data):
            return GateResult(
                listViolation=violations,
                listNote=[f"{tree.name} 은 Unity 빌드라 개별 소스가 DB 에 없습니다 — 검사를 건너뜁니다"],
                summary=summary,
            )

        listNote: list[str] = []
        unbuiltSources = tree.readUnbuiltSources()
        if unbuiltSources is None:
            listNote.append(f"{tree.name} 에 {kUnbuiltSourceListRelPath} 가 없습니다 — 이 구성이 일부러 짓지 않는 소스를 모릅니다"
                            f"(다시 구성하면 생깁니다)")
            unbuiltSources = set()

        compiledFiles: set[str] = set()
        for entry in data:
            filePath = Path(entry.get("file", "")).resolve()
            try:
                compiledFiles.add(filePath.relative_to(repositoryRoot).as_posix().lower())
            except ValueError:
                compiledFiles.add(filePath.as_posix().lower())

        for sourcePath in sources:
            relativeSourcePath = sourcePath.resolve().relative_to(repositoryRoot).as_posix()
            if relativeSourcePath.lower() in unbuiltSources:
                continue
            if relativeSourcePath.lower() not in compiledFiles:
                violations.append(f"compile_commands 에 없음: {relativeSourcePath}")

        return GateResult(
            listViolation=violations,
            listNote=listNote,
            summary=f"{len(sources)} sources referenced in {tree.path} ({len(unbuiltSources)} declared unbuilt by CMake)",
        )


main = CheckSourceGlobGate.run


if __name__ == "__main__":
    sys.exit(main())
