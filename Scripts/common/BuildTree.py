"""
빌드 폴더 하나(`build/<프리셋>`) — 거기서 읽는 것(컴파일 DB · CMakeCache · 짓지 않는 소스 목록 · Bin)은 전부 여기서 읽는다.

빌드 폴더를 고르는 규칙은 셋이다(스크립트마다 따로 정하지 않는다):
1. 사람이 고른다 — `--preset <이름>`(→ `build/<이름>`) 또는 `--build-dir <경로>`(CMake · CTest 가 `${CMAKE_BINARY_DIR}` 를 넘길 때.
   상대 경로는 저장소 루트 기준).
2. 기본 — `.clangd` 의 `CompilationDatabase` 가 가리키는 폴더(편집기가 보는 것과 같은 트리), 없으면 `Ninja-Debug`.
3. App 이 필요한 도구 — `CMakePresets.json` 의 configure 프리셋 순서대로 App 이 있는 첫 폴더(손 목록을 들지 않는다).
"""

from __future__ import annotations

import argparse
import json
from functools import cached_property
from pathlib import Path
from typing import Any, Iterator, Sequence

from .Paths import getProjectRoot

kDefaultPreset = "Ninja-Debug"
kCompileDatabaseFileName = "compile_commands.json"
kCMakeCacheFileName = "CMakeCache.txt"
#: CMake 가 "이 구성이 일부러 짓지 않는 소스" 를 적는 자리(빌드 폴더 기준) — CMake 의 `SW_UNBUILT_SOURCE_LIST`.
kUnbuiltSourceListRelPath = "generated/sw/config/UnbuiltSources.txt"
kAppExecutableNames: tuple[str, ...] = ("App.exe", "App")
#: 시험까지 짓는 타깃 — 기본 `all` 은 시험 실행 파일을 짓지 않는다(`Test/` 는 `EXCLUDE_FROM_ALL`, `cmake/Engine/TestTargets.cmake` 의 `AllTests`).
kTestBuildTargets: tuple[str, ...] = ("all", "AllTests")


class BuildTreeError(Exception):
    """빌드 폴더가 그 일을 할 수 없는 상태 — 무엇을 돌리면 되는지 문장에 든다."""


class BuildTree:
    """빌드 폴더 하나. 만들기만 해서는 디스크를 보지 않는다(`bConfigured` 로 묻는다)."""

    def __init__(self, path: Path) -> None:
        self.path = Path(path).resolve()

    # --- 고르기 --------------------------------------------------------------------

    @classmethod
    def fromPreset(cls, preset: str, repositoryRoot: Path | None = None) -> BuildTree:
        return cls((repositoryRoot or getProjectRoot()) / "build" / preset)

    @classmethod
    def fromArguments(cls, args: argparse.Namespace, repositoryRoot: Path | None = None) -> BuildTree:
        """`addBuildTreeArguments` 가 더한 인자에서. `--build-dir` 이 `--preset` 보다 앞선다."""
        root = repositoryRoot or getProjectRoot()
        buildDir = getattr(args, "build_dir", None)
        if buildDir:
            path = Path(buildDir)
            return cls(path if path.is_absolute() else root / path)
        preset = getattr(args, "preset", None)
        if isinstance(preset, list):
            preset = preset[0] if preset else None
        if preset:
            return cls.fromPreset(preset, root)
        return cls.findDefault(root)

    @classmethod
    def findDefault(cls, repositoryRoot: Path | None = None) -> BuildTree:
        """`.clangd` 가 가리키는 트리, 없으면 `build/Ninja-Debug`."""
        root = repositoryRoot or getProjectRoot()
        clangdFile = root / ".clangd"
        if clangdFile.is_file():
            for line in clangdFile.read_text(encoding="utf-8", errors="replace").splitlines():
                stripped = line.strip()
                if stripped.startswith("CompilationDatabase:"):
                    value = stripped.split(":", 1)[1].strip().strip("\"'")
                    if value:
                        return cls(root / value)
        return cls.fromPreset(kDefaultPreset, root)

    @classmethod
    def ofApp(cls, appPath: Path) -> BuildTree:
        """`<빌드>/Bin/App.exe` · `<빌드>/TestBin/…` → `<빌드>`(CMakeCache.txt 가 있는 가장 가까운 위 폴더)."""
        resolved = Path(appPath).resolve()
        for parent in resolved.parents:
            if (parent / kCMakeCacheFileName).is_file():
                return cls(parent)
        return cls(resolved.parent.parent)

    @classmethod
    def iterConfigured(cls, repositoryRoot: Path | None = None) -> Iterator[BuildTree]:
        """`CMakePresets.json` 의 configure 프리셋 순서대로, 구성된(CMakeCache.txt 가 있는) 폴더."""
        root = repositoryRoot or getProjectRoot()
        presetsPath = root / "CMakePresets.json"
        listName: list[str] = []
        if presetsPath.is_file():
            data = json.loads(presetsPath.read_text(encoding="utf-8"))
            listName = [preset["name"] for preset in data.get("configurePresets", []) if not preset.get("hidden")]
        for name in listName:
            tree = cls.fromPreset(name, root)
            if tree.bConfigured:
                yield tree

    @classmethod
    def findAppExecutable(cls, repositoryRoot: Path | None = None) -> Path | None:
        """App 이 있는 첫 구성된 폴더의 App(프리셋 순서). 없으면 None — 아직 빌드하지 않았다."""
        for tree in cls.iterConfigured(repositoryRoot):
            appPath = tree.appExecutable
            if appPath is not None:
                return appPath
        return None

    # --- 자리 ------------------------------------------------------------------------

    @property
    def name(self) -> str:
        return self.path.name

    @property
    def binDir(self) -> Path:
        """실행 파일 · 시험의 작업 폴더(시험 실행 파일은 모든 구성에서 `TestBin` 에 있다)."""
        return self.path / "Bin"

    @property
    def testBinDir(self) -> Path:
        """시험 실행 파일이 있는 폴더 — `TestBin`(없으면 `Bin` — 시험을 `Bin` 에 내던 옛 빌드 폴더)."""
        testBin = self.path / "TestBin"
        return testBin if testBin.is_dir() else self.binDir

    @property
    def bConfigured(self) -> bool:
        return (self.path / kCMakeCacheFileName).is_file()

    @property
    def appExecutable(self) -> Path | None:
        for name in kAppExecutableNames:
            candidate = self.binDir / name
            if candidate.is_file():
                return candidate
        return None

    # --- 읽기 ------------------------------------------------------------------------

    @cached_property
    def mapCacheValue(self) -> dict[str, str]:
        """CMakeCache.txt 의 `이름:종류=값` → {이름: 값}. 구성 안 된 폴더면 빈 dict."""
        cachePath = self.path / kCMakeCacheFileName
        if not cachePath.is_file():
            return {}
        mapValue: dict[str, str] = {}
        for line in cachePath.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.startswith(("#", "//")) or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if ":" not in key:
                continue
            mapValue[key.split(":", 1)[0]] = value.strip()
        return mapValue

    def readCacheValue(self, name: str) -> str | None:
        return self.mapCacheValue.get(name)

    @property
    def bShipping(self) -> bool:
        return (self.readCacheValue("SW_SHIPPING_BUILD") or "").upper() in ("ON", "TRUE", "1")

    @property
    def compileDatabasePath(self) -> Path:
        return self.path / kCompileDatabaseFileName

    @property
    def bHasCompileDatabase(self) -> bool:
        return self.compileDatabasePath.is_file()

    def readCompileDatabase(self) -> list[dict[str, Any]]:
        """컴파일 DB 항목. 없거나 깨졌으면 `BuildTreeError`(무엇을 돌리면 되는지 든다)."""
        if not self.bHasCompileDatabase:
            raise BuildTreeError(f"{self.compileDatabasePath.as_posix()} 가 없다 — `cmake --preset {self.name}` 를 먼저 돌린다")
        try:
            return json.loads(self.compileDatabasePath.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise BuildTreeError(f"{self.compileDatabasePath.as_posix()} 를 읽지 못했다: {error}") from error

    def readUnbuiltSources(self) -> set[str] | None:
        """이 구성이 일부러 짓지 않는 소스(저장소 기준 경로, 소문자). 목록 파일이 없으면 None — 그 목록 전에 구성한 트리다."""
        listPath = self.path / kUnbuiltSourceListRelPath
        if not listPath.is_file():
            return None
        lines = listPath.read_text(encoding="utf-8", errors="replace").splitlines()
        return {line.strip().replace("\\", "/").lower() for line in lines if line.strip()}

    def buildCommand(self, listTarget: Sequence[str] = kTestBuildTargets, jobCount: int | None = None) -> list[str]:
        """이 트리를 짓는 명령(`cmake --build <폴더> --target …`). 기본은 시험까지(`kTestBuildTargets`)."""
        command = ["cmake", "--build", str(self.path), "--target", *listTarget]
        if jobCount:
            command += ["-j", str(jobCount)]
        return command


def addBuildTreeArguments(parser: argparse.ArgumentParser, *, defaultPreset: str | None = kDefaultPreset, bMultiple: bool = False) -> None:
    """
    빌드 폴더 인자 둘 — 모든 스크립트가 같은 철자를 쓴다.

    - `--preset <이름>` : `build/<이름>`. `bMultiple` 이면 여러 번 줄 수 있다(구성마다 도는 보고서 — 기본값은 부르는 쪽이 정한다).
    - `--build-dir <경로>` : 빌드 폴더를 경로로(CMake · CTest 가 `${CMAKE_BINARY_DIR}` 를 넘길 때). 상대 경로는 저장소 루트 기준.
    `defaultPreset=None` 이면 둘 다 없을 때 `BuildTree.findDefault`(`.clangd`)를 쓴다.
    """
    if bMultiple:
        parser.add_argument("--preset", action="append", default=None, help="빌드 프리셋 — build/<이름> (여러 번 줄 수 있다)")
    else:
        parser.add_argument("--preset", default=defaultPreset,
                            help=f"빌드 프리셋 — build/<이름> (기본 {defaultPreset or '.clangd 가 가리키는 트리'})")
    parser.add_argument("--build-dir", default=None, help="빌드 폴더 경로(--preset 보다 앞선다, 상대 경로는 저장소 루트 기준)")
