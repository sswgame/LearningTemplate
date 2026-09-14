#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CMake 소스 GLOB 누락 및 컴파일 데이터베이스 일치 검사.

현재 빌드 트리(compile_commands.json)와 디스크의 C++ 소스 파일 목록을 대조하여,
새로 추가된 .cpp/.c 파일이 빌드 타겟 및 LSP 인덱서에 정상 등록되었는지 검사합니다.

**RHI 백엔드 목록도 함께 본다** (`cmake/Engine/RhiBackendSources.cmake`).
그 파일은 백엔드 .cpp 를 손으로 나열하고, `SW_RHI_AS_MODULES=ON` 일 때 어느 .cpp 가 `RHI_*` MODULE 로
가는지를 정한다. 목록에서 빠진 파일은 **컴파일이 안 되는 게 아니라 Engine 타겟의 glob 이 주워간다** —
즉 모듈이 아니라 Engine.dll 로 들어간다. 그래서 compile_commands 대조로는 절대 안 잡히고(실험으로 확인),
증상은 모듈의 미정의 심볼로 나온다(실제로 `VulkanRHIRenderPassCache.cpp` 가 빠져 `vkCreateRenderPass`
미정의가 났다). 목록과 디스크를 양방향으로 맞춘다.

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
    startsWithPathComponent,
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

# 어느 플랫폼에서도 빌드에 안 들어가는 자리 (MODULE 정의 디렉터리).
_kIgnoreSubdirsAlways = ("Graphics/RHI/Modules/",)

# OS 전용 소스는 **그 OS 가 아닐 때만** 빠져 있는 게 정상이다. 예전엔 이 목록이 Windows 기준으로
# 고정돼 있어서(리눅스/맥 것만 무시) 리눅스 빌드에서 DX11/DX12/Windows 소스 23개가 통째로
# "빠졌다"고 잡혔다. 반대로 리눅스에서 빌드하면서 /Linux/ 를 무시하면 진짜 누락도 놓친다 —
# 그래서 호스트에 따라 반대편만 무시한다.
_kIgnoreSubdirsNonWindows = ("/Windows/", "/DX11/", "/DX12/", "DelayLoadNotifyHook")
# Linux 와 macOS 가 함께 쓰는 POSIX 소스 — Windows 에서만 빠져 있는 것이 정상이다.
_kIgnoreSubdirsWindowsOnly = ("/Posix/",)
_kIgnoreSubdirsNonLinux = ("/Linux/", "/X11")
_kIgnoreSubdirsNonMac = ("/Mac/", "/Cocoa")


def buildIgnoreSubdirsInternal() -> tuple[str, ...]:
    """호스트 플랫폼에서 빌드되지 않는 것이 정상인 경로 조각들을 모읍니다."""
    ignores = list(_kIgnoreSubdirsAlways)
    if sys.platform.startswith("win"):
        ignores.extend(_kIgnoreSubdirsWindowsOnly)
    if not sys.platform.startswith("win"):
        ignores.extend(_kIgnoreSubdirsNonWindows)
    if not sys.platform.startswith("linux"):
        ignores.extend(_kIgnoreSubdirsNonLinux)
    if sys.platform != "darwin":
        ignores.extend(_kIgnoreSubdirsNonMac)
    return tuple(ignores)


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

    본 검사는 **빌드 트리의 compile_commands.json** 과 대조하는 것이라 임시 트리로는 성립하지
    않는다(빌드 없이 돌리면 스스로 "소스 목록만 보고" 하고 0 을 돌려준다).
    RHI 백엔드 목록 검사는 양방향 탐침으로 확인했다(2026-09-14 백로그 참고).
    """

    description = "소스 GLOB 누락 검사"
    buildComment = "Checking source GLOB coverage vs compile_commands..."
    timeoutSeconds = 15
    listCtestArgument = ("--build", "${CMAKE_BINARY_DIR}", "--active-game", "${SW_ACTIVE_GAME}")
    maxViolationShown = 40
    hint = "  reconfigure 가 필요하거나, RhiBackendSources.cmake 의 경로가 디스크와 어긋났습니다."
    selfTestSkipReason = "compile_commands.json 이 있는 실제 빌드 트리가 필요하다"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--build", type=Path, default=None, help="compile_commands.json 이 있는 빌드 디렉터리")
        parser.add_argument("--active-game", default="Empty", help="SW_ACTIVE_GAME 팩 이름")

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

        compiledFiles: set[str] = set()
        for entry in data:
            filePath = Path(entry.get("file", "")).resolve()
            try:
                compiledFiles.add(filePath.relative_to(repositoryRoot).as_posix().lower())
            except ValueError:
                compiledFiles.add(filePath.as_posix().lower())

        # 지연 로딩 훅 및 모듈 엔트리는 의도된 특수 케이스이므로 검사 대상에 포함합니다.
        ignoreSubdirs = buildIgnoreSubdirsInternal()
        for sourcePath in sources:
            relativeSourcePath = sourcePath.resolve().relative_to(repositoryRoot).as_posix()
            # MODULE entries / inactive packs / other-OS sources are expected absences.
            if any(ignore in relativeSourcePath for ignore in ignoreSubdirs):
                continue
            if startsWithPathComponent(relativeSourcePath, kDirSourceGames):
                if f"/{args.active_game}/" not in relativeSourcePath:
                    continue
            if relativeSourcePath.lower() not in compiledFiles:
                violations.append(f"compile_commands 에 없음: {relativeSourcePath}")

        return GateResult(
            listViolation=violations,
            summary=f"{len(sources)} sources referenced in {buildDir}, RHI backend list matches disk",
        )


main = CheckSourceGlobGate.run


if __name__ == "__main__":
    sys.exit(main())
