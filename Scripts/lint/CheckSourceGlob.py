#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CMake 소스 GLOB 누락 및 컴파일 데이터베이스 일치 검사.

현재 빌드 트리(compile_commands.json)와 디스크의 C++ 소스 파일 목록을 대조하여,
새로 추가된 .cpp/.c 파일이 빌드 타겟 및 LSP 인덱서에 정상 등록되었는지 검사합니다.

(Ninja 빌드는 SW_GLOB_CONFIGURE_DEPENDS로 자동 감지하지만,
 CI 파이프라인이나 CONFIGURE_DEPENDS=OFF 환경, pre-commit 단계에서
 전체 빌드 없이 빠른 소스 누락 방지 검증을 위해 사용됩니다.)

  python Scripts/lint/CheckSourceGlob.py [--root <repo>] [--build <dir>]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import (
    collectSourceFiles,
    getProjectRoot,
    kCppSourceExtensions,
    kDirSourceApp,
    kDirSourceCore,
    kDirSourceEditor,
    kDirSourceEngine,
    kDirSourceGameFramework,
    kDirSourceGames,
    startsWithPathComponent,
    useUtf8Stdout,
)

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
_kIgnoreSubdirsNonLinux = ("/Linux/", "/X11")
_kIgnoreSubdirsNonMac = ("/Mac/", "/Cocoa")


def buildIgnoreSubdirsInternal() -> tuple[str, ...]:
    """호스트 플랫폼에서 빌드되지 않는 것이 정상인 경로 조각들을 모읍니다."""
    ignores = list(_kIgnoreSubdirsAlways)
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


def main() -> int:
    useUtf8Stdout()

    parser = argparse.ArgumentParser(description="소스 GLOB 누락 검사")
    parser.add_argument("--root", type=Path, default=None)
    parser.add_argument("--build", type=Path, default=None, help="compile_commands.json 이 있는 빌드 디렉터리")
    parser.add_argument("--active-game", default="Empty", help="SW_ACTIVE_GAME 팩 이름")
    args = parser.parse_args()
    repo = (args.root or getProjectRoot()).resolve()

    scanDirs = [repo / rel for rel in _kScanRoots]
    sources = collectSourceFiles(scanDirs, extensions=kCppSourceExtensions)

    # 지연 로딩 훅 및 모듈 엔트리는 의도된 특수 케이스이므로 검사 대상에 포함합니다.
    buildDir = args.build
    if buildDir is None:
        buildDir = pickBuildDirInternal(repo)
    else:
        buildDir = buildDir.resolve()

    if buildDir is None or not (buildDir / "compile_commands.json").is_file():
        print("[CheckSourceGlob] compile_commands.json 없음 — 소스 목록만 보고합니다.")
        print(f"[CheckSourceGlob] scanned {len(sources)} translation units under Source/")
        return 0

    compiledFiles: set[str] = set()
    data = json.loads((buildDir / "compile_commands.json").read_text(encoding="utf-8"))

    # Unity 빌드는 소스를 unity_N_cxx.cxx 로 묶어 컴파일하므로 개별 .cpp 가 DB 에 없다.
    # 그 빌드 트리에서는 이 검사가 성립하지 않는다 — 없다고 답하는 대신 성립하지 않는다고 말한다.
    if any("unity_" in entry.get("file", "") for entry in data):
        print(f"[CheckSourceGlob] {buildDir.name} 은 Unity 빌드라 개별 소스가 DB 에 없습니다 — 검사를 건너뜁니다.")
        print(f"[CheckSourceGlob] scanned {len(sources)} translation units under Source/")
        return 0

    for entry in data:
        filePath = Path(entry.get("file", "")).resolve()
        try:
            compiledFiles.add(filePath.relative_to(repo).as_posix().lower())
        except ValueError:
            compiledFiles.add(filePath.as_posix().lower())

    missingSources: list[str] = []
    ignoreSubdirs = buildIgnoreSubdirsInternal()
    for sourcePath in sources:
        relativeSourcePath = sourcePath.resolve().relative_to(repo).as_posix()
        # MODULE entries / inactive packs / other-OS sources are expected absences.
        if any(ignore in relativeSourcePath for ignore in ignoreSubdirs):
            continue
        if startsWithPathComponent(relativeSourcePath, kDirSourceGames):
            active = f"/{args.active_game}/"
            if active not in relativeSourcePath:
                continue
        if relativeSourcePath.lower() not in compiledFiles:
            missingSources.append(relativeSourcePath)

    if missingSources:
        print(f"[CheckSourceGlob] compile_commands에 없는 소스 {len(missingSources)}개 (reconfigure 필요할 수 있음):")
        for line in missingSources[:40]:
            print(f"  - {line}")
        if len(missingSources) > 40:
            print(f"  ... +{len(missingSources) - 40} more")
        return 1

    print(f"[CheckSourceGlob] OK ({len(sources)} sources referenced in {buildDir})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
