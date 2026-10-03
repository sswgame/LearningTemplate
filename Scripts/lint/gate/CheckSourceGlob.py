#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CMake 소스 GLOB 누락 및 컴파일 데이터베이스 일치 검사.

현재 빌드 트리(compile_commands.json)와 디스크의 C++ 소스 파일 목록을 대조하여,
새로 추가된 .cpp/.c 파일이 빌드 타겟 및 LSP 인덱서에 정상 등록되었는지 검사합니다.

**RHI 백엔드 목록도 함께 본다** (`cmake/Engine/RhiBackendSources.cmake`).
그 파일은 백엔드 .cpp 를 손으로 나열하고, Dev 에서 어느 .cpp 가 `RHI_*` MODULE 로
가는지를 정한다. 목록에서 빠진 파일은 **컴파일이 안 되는 게 아니라 Engine 타겟의 glob 이 주워간다** —
즉 모듈이 아니라 Engine.dll 로 들어간다. 그래서 compile_commands 대조로는 절대 안 잡히고(실험으로 확인),
증상은 모듈의 미정의 심볼로 나온다(실제로 `VulkanRHIRenderPassCache.cpp` 가 빠져 `vkCreateRenderPass`
미정의가 났다). 목록과 디스크를 양방향으로 맞춘다.

**구성이 일부러 짓지 않는 소스는 CMake 가 적은 목록으로만 안다** (`<빌드>/generated/sw/config/UnbuiltSources.txt`).
배포 구성의 에디터 · 핫 리로드 · 고르지 않은 RHI 백엔드, 다른 OS 의 Core 소스, 짓지 않는 RHI 모듈 엔트리 · DX 모듈(윈도우 밖),
고르지 않은 게임 팩, 끈 GameFramework 가 그렇다. 무엇을 빼는지는 CMake 가 정하므로 빼는 자리가 직접 적고(`sw_declareUnbuiltSources` ·
`sw_excludeUnbuiltSources` · `sw_declareUnbuiltDirectory`, `cmake/Engine/TargetRules.cmake`), 여기서는 그 목록만 읽는다 — 이 게이트에는
플랫폼 · 게임 이름을 적은 무시 목록이 없다. 목록에 없는데 지어지지 않은 소스는 위반이다.

(Ninja 빌드는 SW_GLOB_CONFIGURE_DEPENDS로 자동 감지하지만,
 CI 파이프라인이나 CONFIGURE_DEPENDS=OFF 환경, pre-commit 단계에서
 전체 빌드 없이 빠른 소스 누락 방지 검증을 위해 사용됩니다.)

  python Scripts/lint/gate/CheckSourceGlob.py [--root <repo>] [--build <dir>]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import (  # noqa: E402
    collectSourceFiles,
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

#: CMake 가 "이 구성이 일부러 짓지 않는 소스" 를 적는 자리(빌드 트리 기준). `cmake/Engine/TargetRules.cmake` 의
#: `SW_UNBUILT_SOURCE_LIST` 와 같은 경로다. 한 줄에 저장소 기준 경로 하나.
_kUnbuiltSourceListPath = "generated/sw/config/UnbuiltSources.txt"


def readUnbuiltSourcesInternal(buildDir: Path) -> set[str] | None:
    """빌드 트리의 "짓지 않는 소스" 목록(저장소 기준 경로, 소문자). 목록 파일이 없으면 None — 이 변경 전에 구성한 트리다."""
    listPath = buildDir / _kUnbuiltSourceListPath
    if listPath.is_file() is False:
        return None
    lines = listPath.read_text(encoding="utf-8", errors="ignore").splitlines()
    return {line.strip().replace("\\", "/").lower() for line in lines if line.strip()}


def pickBuildDirInternal(repo: Path) -> Path | None:
    """
    compile_commands.json 을 읽을 빌드 트리를 고릅니다.

    예전엔 `sorted(..., reverse=True)[0]`, 즉 **역알파벳순 첫 번째**였다. 그건 아무 의미도 담지
    않은 순서라 build/ 에 디렉터리가 하나 늘어나는 것만으로 대상이 바뀐다. 실제로 Test-Unity 가
    생기자 그쪽을 읽어 210개를 "빠졌다"고 오탐했다(Unity 빌드에는 개별 .cpp 가 없다).
    이제 .clangd 가 가리키는 트리를 먼저 보고, 없으면 가장 최근에 갱신된 것을 쓴다.
    """
    rootDb = repo / "build" / "compile_commands.json"
    if rootDb.is_file():
        return rootDb.parent

    preferred = readClangdBuildDirInternal(repo)
    if preferred is not None and (preferred / "compile_commands.json").is_file():
        return preferred

    candidates = list((repo / "build").glob("*/compile_commands.json"))
    if not candidates:
        return None
    return max(candidates, key=lambda path: path.stat().st_mtime).parent


def readClangdBuildDirInternal(repo: Path) -> Path | None:
    """.clangd 의 CompilationDatabase 항목이 가리키는 빌드 트리 (없으면 None)."""
    clangdFile = repo / ".clangd"
    if not clangdFile.is_file():
        return None
    for line in clangdFile.read_text(encoding="utf-8", errors="ignore").splitlines():
        stripped = line.strip()
        if not stripped.startswith("CompilationDatabase:"):
            continue
        value = stripped.split(":", 1)[1].strip().strip('"').strip("'")
        if value:
            return (repo / value).resolve()
    return None


_kRhiBackendListFile = "cmake/Engine/RhiBackendSources.cmake"
_kRhiBackendRoot = "Source/Engine/Graphics/RHI"
_kRhiBackendDirs = ("DX11", "DX12", "GL", "Vulkan")
_kRhiListedRe = re.compile(r"\$\{swRhiRoot\}/([\w/]+\.cpp)")


def checkRhiBackendSourceListInternal(repo: Path) -> list[str]:
    """백엔드 .cpp 목록과 디스크를 양방향으로 맞춥니다 (파일 머리 주석 참고)."""
    listPath = repo / _kRhiBackendListFile
    if listPath.is_file() is False:
        return [f"{_kRhiBackendListFile}: 파일이 없습니다"]

    listed = set(_kRhiListedRe.findall(listPath.read_text(encoding="utf-8", errors="ignore")))
    if not listed:
        return [f"{_kRhiBackendListFile}: 백엔드 소스를 하나도 찾지 못했습니다 (검사가 헛돌고 있습니다)"]

    onDisk: set[str] = set()
    for backend in _kRhiBackendDirs:
        backendDir = repo / _kRhiBackendRoot / backend
        if backendDir.is_dir() is False:
            continue
        for path in backendDir.rglob("*.cpp"):
            onDisk.add(path.relative_to(repo / _kRhiBackendRoot).as_posix())

    errors: list[str] = []
    for relPath in sorted(onDisk - listed):
        errors.append(f"{_kRhiBackendRoot}/{relPath}: {_kRhiBackendListFile} 에 없습니다 — "
                      f"모듈이 아니라 Engine 타겟으로 들어갑니다(링크 시점에 미정의 심볼로 터집니다)")
    for relPath in sorted(listed - onDisk):
        errors.append(f"{_kRhiBackendListFile}: `{relPath}` 가 디스크에 없습니다 — 파일이 옮겨졌으면 경로를 고치세요")
    return errors


class CheckSourceGlobGate(LintGate):
    """
    빌드가 실제로 컴파일하는 목록과 디스크의 소스를 대조한다.

    본 검사는 **빌드 트리의 compile_commands.json** 과 대조한다(빌드 없이 돌리면 스스로 "소스 목록만 보고" 하고 0 을 돌려준다).
    자가 시험은 임시 트리에 `build/compile_commands.json` 을 직접 써서 돌린다 — "짓지 않는 소스" 목록이 있어도 거기 없는
    빠진 소스는 잡는지 본다(목록이 게이트를 눈멀게 하지 않는지). RHI 백엔드 목록 검사는 양방향 탐침으로 확인했다(2026-09-14 백로그 참고).
    """

    description = "소스 GLOB 누락 검사"
    buildComment = "Checking source GLOB coverage vs compile_commands..."
    timeoutSeconds = 15
    preCommitSkipReason = "빌드 디렉터리(--build)가 있어야 글롭과 대조할 수 있다 — 커밋 훅은 그것을 모른다"
    listCtestArgument = ("--build", "${CMAKE_BINARY_DIR}")
    maxViolationShown = 40
    hint = ("  reconfigure 가 필요하거나, RhiBackendSources.cmake 의 경로가 디스크와 어긋났습니다. 이 구성이 일부러 짓지 않는 소스라면\n"
            "  빼는 자리에서 sw_excludeUnbuiltSources · sw_declareUnbuiltSources 로 적으세요(cmake/Engine/TargetRules.cmake).")
    selfTestCases = [
        {
            "name": "짓지 않는 소스 목록에 없는데 compile_commands 에도 없는 소스",
            "files": {
                "cmake/Engine/RhiBackendSources.cmake": 'set(SW_RHI_VULKAN_DEVICE_SOURCES\n    "${swRhiRoot}/Vulkan/Probe.cpp"\n)\n',
                "Source/Engine/Graphics/RHI/Vulkan/Probe.cpp": "// 아무도 짓지 않는다\n",
                "Source/Editor/Declared.cpp": "// 이 구성이 짓지 않는다고 적혀 있다\n",
                "build/compile_commands.json": "[]\n",
                "build/generated/sw/config/UnbuiltSources.txt": "Source/Editor/Declared.cpp\n",
            },
        },
        {
            # 게이트에 플랫폼 · 게임 무시 목록이 없다 — 다른 OS 폴더 · 다른 게임 팩도 목록에 없으면 위반이다.
            "name": "다른 OS 폴더 · 다른 게임 팩의 소스도 목록에 없으면 위반",
            "files": {
                "cmake/Engine/RhiBackendSources.cmake": 'set(SW_RHI_VULKAN_DEVICE_SOURCES\n    "${swRhiRoot}/Vulkan/Built.cpp"\n)\n',
                # 백엔드 목록 검사를 맞추려고 둔 파일 — 짓지 않는 소스로 적혀 있어 위반이 아니다. 남는 위반은 아래 둘뿐이다.
                "Source/Engine/Graphics/RHI/Vulkan/Built.cpp": "// 짓지 않는 소스로 적혀 있다\n",
                "Source/Core/File/Linux/ProbeLinux.cpp": "// 이 구성이 짓지 않는데 아무도 적지 않았다\n",
                "Source/Games/Other/ProbeGame.cpp": "// 고르지 않은 게임 팩인데 아무도 적지 않았다\n",
                "build/compile_commands.json": "[]\n",
                "build/generated/sw/config/UnbuiltSources.txt": "Source/Engine/Graphics/RHI/Vulkan/Built.cpp\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--build", type=Path, default=None, help="compile_commands.json 이 있는 빌드 디렉터리")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 백엔드 목록 검사는 빌드 트리가 없어도 성립한다 — 아래 조기 반환들보다 먼저 본다.
        violations = checkRhiBackendSourceListInternal(repositoryRoot)

        scanDirs = [repositoryRoot / rel for rel in _kScanRoots]
        sources = collectSourceFiles(scanDirs, extensions=kCppSourceExtensions)
        summary = f"{len(sources)} sources scanned under Source/"

        buildDir = args.build.resolve() if args.build else pickBuildDirInternal(repositoryRoot)
        if buildDir is None or not (buildDir / "compile_commands.json").is_file():
            return GateResult(
                listViolation=violations,
                listNote=["compile_commands.json 없음 — 소스 목록만 보고합니다"],
                summary=summary,
            )

        data = json.loads((buildDir / "compile_commands.json").read_text(encoding="utf-8"))

        # Unity 빌드는 소스를 unity_N_cxx.cxx 로 묶어 컴파일하므로 개별 .cpp 가 DB 에 없다.
        # 그 빌드 트리에서는 이 검사가 성립하지 않는다 — 없다고 답하는 대신 성립하지 않는다고 말한다.
        if any("unity_" in entry.get("file", "") for entry in data):
            return GateResult(
                listViolation=violations,
                listNote=[f"{buildDir.name} 은 Unity 빌드라 개별 소스가 DB 에 없습니다 — 검사를 건너뜁니다"],
                summary=summary,
            )

        listNote: list[str] = []
        unbuiltSources = readUnbuiltSourcesInternal(buildDir)
        if unbuiltSources is None:
            listNote.append(f"{buildDir.name} 에 {_kUnbuiltSourceListPath} 가 없습니다 — 이 구성이 일부러 짓지 않는 소스를 모릅니다"
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
            summary=f"{len(sources)} sources referenced in {buildDir} ({len(unbuiltSources)} declared unbuilt by CMake), "
                    f"RHI backend list matches disk",
        )


main = CheckSourceGlobGate.run


if __name__ == "__main__":
    sys.exit(main())
