#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/TargetLayers.py

타깃 경계 껍데기를 만든다. `Source/` 가 모든 타깃의 include 루트라 Core 가 `Engine/...` 을, 키트가 매니페스트에 적지 않은 키트를 include 해도 헤더만 쓰면
컴파일 · 링크가 된다. 표(`Scripts/generate/rules/TargetLayers.toml`)의 게이트마다, 그리고 모듈(키트 · 에디터 확장 · 게임)마다 보면 안 되는 헤더와 같은 경로의 `#error`
파일을 `<빌드>/generated/sw/TargetLayerStubs/<게이트>/` 에 놓고, CMake(`sw_applyTargetLayerGate`)가 그 폴더를 타깃의 include 자리 맨 앞에 둔다.
위반하면(헤더를 거쳐 닿는 전이 include 도) 컴파일이 멈춘다 — `EngineLayers.py` · `EditorLayers.py` 와 같은 틀이고, 그쪽이 폴더 층을 보면 이쪽은 타깃 경계를 본다.

파일 수가 많아(키트 60 여 개 × 헤더 수백) CMake 대신 여기서 쓴다. 게이트마다 목록 스탬프가 같으면 파일을 건드리지 않아 configure 가 파일 하나만 읽는다.

  python Scripts/generate/TargetLayers.py <출력 .cmake 파일>
"""

from __future__ import annotations

import hashlib
import json
import shutil
import sys
import tomllib
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.Paths import getProjectRoot  # noqa: E402
from generate.GeneratorOutput import GeneratorError, runGenerator, writeGeneratedFile  # noqa: E402

kTag = "TargetLayers"
kRuleFile = Path(__file__).resolve().parent / "rules" / "TargetLayers.toml"
kHeaderSuffix = {".h", ".hpp", ".inl", ".xxx"}
kModuleKinds = {"Kit", "EditorExtension", "Game"}   # 모듈별 게이트를 받는 종류
kStubFolderName = "TargetLayerStubs"


class ModuleInfo:
    """매니페스트 하나 — 모듈 폴더 · 종류 · 직접 의존."""

    def __init__(self, gateName: str, name: str, kind: str, folder: str, listDependency: list[str]) -> None:
        self.gateName = gateName              # 모듈별 게이트 이름. 게임은 이름이 모두 SWGame 이라 폴더로 가른다
        self.name = name
        self.kind = kind
        self.folder = folder                  # `Source/` 기준 모듈 폴더(런타임 · 서버 · 확장이 나뉜 키트는 각자의 폴더)
        self.listDependency = listDependency


def makeGameGateName(folder: str) -> str:
    """게임 모듈의 게이트 이름 — `Games/<폴더>` 의 폴더 이름으로 가른다."""
    return f"SWGame_{folder.rsplit('/', 1)[-1]}"


def readModules(sourceDir: Path) -> list[ModuleInfo]:
    """`Source/` 의 매니페스트를 모두 읽는다."""
    listModule: list[ModuleInfo] = []
    for path in sorted(sourceDir.rglob("*.module.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            name, kind = data["_name"], data["_kind"]
            listDependency = [entry["_name"] for entry in data.get("_listDependency", [])]
        except (OSError, ValueError, KeyError, TypeError) as exception:
            raise GeneratorError(f"{path}: 매니페스트를 읽지 못했다 ({exception!r})") from exception
        folder = path.parent.relative_to(sourceDir).as_posix()
        gateName = makeGameGateName(folder) if kind == "Game" else name
        listModule.append(ModuleInfo(gateName, name, kind, folder, listDependency))
    return listModule


def computeClosure(module: ModuleInfo, mapByName: dict[str, ModuleInfo]) -> set[str]:
    """@p module 이 볼 수 있는 모듈 폴더 — 자기와 `_listDependency` 의 전이 폐쇄."""
    listVisible = [module.folder]
    setSeen = {module.name}
    listPending = list(module.listDependency)
    while listPending:
        name = listPending.pop()
        if name in setSeen:
            continue
        setSeen.add(name)
        dependency = mapByName.get(name)
        if dependency is None:
            continue   # 없는 모듈은 매니페스트 해석(`sw_resolveModuleManifests`)이 멈춘다 — 여기서 같은 말을 또 하지 않는다
        listVisible.append(dependency.folder)
        listPending.extend(dependency.listDependency)
    return set(listVisible)


def findOwner(header: str, listFolder: list[str]) -> str | None:
    """헤더를 가진 모듈 폴더 — 가장 긴 접두어가 이긴다(`NetSimulation/Editor` 는 `NetSimulation` 안이지만 따로다). @p listFolder 는 긴 것부터 정렬돼 있다."""
    for folder in listFolder:
        if header.startswith(folder + "/"):
            return folder
    return None


def writeGateStubs(stubDir: Path, gateName: str, listHeader: list[str], message: str) -> None:
    """게이트 하나의 껍데기를 쓴다. 목록이 스탬프와 같으면 건드리지 않는다. 다르면 폴더째 다시 쓴다(사라진 헤더의 껍데기가 옮겨 온 헤더를 가리지 않게)."""
    stubRoot = stubDir / gateName
    stampPath = stubDir / f"{gateName}.stamp"
    content = f"#error \"{message}\"\n"
    digest = hashlib.sha256("\n".join([content, *listHeader]).encode("utf-8")).hexdigest()
    if stampPath.is_file() and stubRoot.is_dir() and stampPath.read_text(encoding="utf-8") == digest:
        return
    if stubRoot.exists():
        shutil.rmtree(stubRoot)
    stubRoot.mkdir(parents=True)
    setFolder: set[Path] = set()
    for header in listHeader:
        path = stubRoot / header
        if path.parent not in setFolder:
            path.parent.mkdir(parents=True, exist_ok=True)
            setFolder.add(path.parent)
        path.write_text(content, encoding="utf-8")
    stampPath.parent.mkdir(parents=True, exist_ok=True)
    stampPath.write_text(digest, encoding="utf-8")


def collectGates(sourceDir: Path, mapRule: dict[str, list[str]]) -> dict[str, tuple[list[str], str]]:
    """게이트 이름 → (막는 헤더, 오류 문구). 파일을 쓰지 않는다 — 표를 코드와 대조하는 쪽도 이 함수를 쓴다."""
    listHeader = sorted(path.relative_to(sourceDir).as_posix() for path in sourceDir.rglob("*") if path.is_file() and path.suffix in kHeaderSuffix)
    listStale = [prefix for listPrefix in mapRule.values() for prefix in listPrefix if not (sourceDir / prefix).exists()]
    if listStale:
        raise GeneratorError(f"폴더가 없는 게이트 줄: {', '.join(sorted(set(listStale)))} — {kRuleFile.name} 에서 지우세요")
    mapGate: dict[str, tuple[list[str], str]] = {}
    for gateName, listPrefix in mapRule.items():
        listDenied = [header for header in listHeader if any(header.startswith(prefix + "/") for prefix in listPrefix)]
        mapGate[gateName] = (listDenied, f"Target gate '{gateName}' forbids this header (Scripts/generate/rules/TargetLayers.toml): a lower target must not see an upper one. Move what you need down, or invert the dependency.")

    listModule = readModules(sourceDir)
    mapByName = {module.name: module for module in listModule if module.kind != "Game"}
    listGated = [module for module in listModule if module.kind in kModuleKinds]
    listFolder = sorted((module.folder for module in listGated), key=len, reverse=True)
    mapFolderHeader: dict[str, list[str]] = {}
    for header in listHeader:
        owner = findOwner(header, listFolder)
        if owner is not None:
            mapFolderHeader.setdefault(owner, []).append(header)
    for module in listGated:
        setVisible = computeClosure(module, mapByName)
        listDenied = sorted(header for folder, listOwned in mapFolderHeader.items() if folder not in setVisible for header in listOwned)
        mapGate[f"module/{module.gateName}"] = (listDenied, f"Module '{module.name}' ({module.folder}) cannot include this header: its owner module is not in the _listDependency closure of {module.name}.module.json. Add the dependency, or move the shared type down.")
    return mapGate


def generateTargetLayers(outputPath: Path) -> None:
    try:
        mapRule: dict[str, list[str]] = tomllib.loads(kRuleFile.read_text(encoding="utf-8"))["gate"]
    except (OSError, tomllib.TOMLDecodeError, KeyError) as exception:
        raise GeneratorError(f"{kRuleFile.name}: 읽지 못했다 ({exception!r}) — [gate] 표가 있어야 한다") from exception
    sourceDir = getProjectRoot() / "Source"
    mapGate = collectGates(sourceDir, mapRule)
    stubDir = outputPath.parent.parent / kStubFolderName
    for gateName, (listDenied, message) in mapGate.items():
        writeGateStubs(stubDir, gateName, listDenied, message)
    listLine = [
        "# AUTO-GENERATED by Scripts/generate/TargetLayers.py — do not edit. 쓰는 곳: sw_applyTargetLayerGate(cmake/Engine/TargetCompileRules.cmake).",
        f'set(SW_TARGET_LAYER_STUB_DIR "{stubDir.as_posix()}")',
        f'set(SW_TARGET_LAYER_GATES "{";".join(sorted(mapGate))}")',
        "",
    ]
    writeGeneratedFile(outputPath, "\n".join(listLine), tag=kTag, summary=f"{len(mapGate)} gates · {sum(len(entry[0]) for entry in mapGate.values())} stubs")


def main(argv: Sequence[str] | None = None) -> int:
    return runGenerator(argv, tag=kTag, description="타깃 경계 표 → #error 껍데기",
                        addArguments=lambda parser: parser.add_argument("output", type=Path, help="쓸 .cmake 파일"),
                        generate=lambda args: generateTargetLayers(args.output))


if __name__ == "__main__":
    sys.exit(main())
