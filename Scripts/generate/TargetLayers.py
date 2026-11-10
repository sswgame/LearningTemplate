#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/TargetLayers.py

타깃 경계 껍데기를 만든다. `Source/` 가 모든 타깃의 include 루트라 Core 가 `Engine/...` 을, 키트가 매니페스트에 적지 않은 키트를 include 해도 헤더만 쓰면
컴파일 · 링크가 된다. 표(`Config/Rules/Layers/TargetLayers.toml`)의 게이트마다, 그리고 모듈(키트 · 에디터 확장 · 게임)마다 보면 안 되는 헤더와 같은 경로의 `#error`
파일을 `<빌드>/generated/sw/TargetLayerStubs/<게이트>/` 에 놓고, CMake(`sw_applyTargetLayerGate`)가 그 폴더를 타깃의 include 자리 맨 앞에 둔다.
위반하면(헤더를 거쳐 닿는 전이 include 도) 컴파일이 멈춘다 — `ModuleLayers.py` · `EditorLayers.py` 와 같은 틀이고, 그쪽이 폴더 층을 보면 이쪽은 타깃 경계를 본다.

`[gatefolder]` 는 경로 접두어 대신 폴더 이름으로 막는다 — 서버 구성이 짓지 않는 `Client/`, 클라이언트 구성이 짓지 않는 `Server/` 가 모듈 · 게임마다 있어서다.
`serverbuild` 는 서버가 짓지 않는 엔진 폴더(`cmake/Engine/BuildLayout.cmake` 의 `SW_UNBUILT_ENGINE_FOLDERS`)를 모두 막아야 한다 — 대조해 빠지면 멈춘다.

파일 수가 많아(키트 60 여 개 × 헤더 수백) CMake 대신 여기서 쓴다. 게이트마다 목록 스탬프가 같으면 파일을 건드리지 않아 configure 가 파일 하나만 읽는다.

  python Scripts/generate/TargetLayers.py <출력 .cmake 파일>
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import sys
import tomllib
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.Paths import getProjectRoot  # noqa: E402
from generate.GeneratorOutput import GeneratorError, runGenerator, writeGeneratedFile  # noqa: E402

kTag = "TargetLayers"
kRuleFile = Path(__file__).resolve().parents[2] / "Config" / "Rules" / "Layers" / "TargetLayers.toml"
kHeaderSuffix = {".h", ".hpp", ".inl", ".xxx"}
kModuleKinds = {"Kit", "EditorExtension", "Game"}   # 모듈별 게이트를 받는 종류
kStubFolderName = "TargetLayerStubs"
kQuotedInclude = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
kServerGateName = "serverbuild"
kClientGateName = "clientbuild"
kUnbuiltFolderLine = re.compile(r'list\(APPEND SW_UNBUILT_ENGINE_FOLDERS ([^)]*)\)')


def readServerUnbuiltFolders(projectRoot: Path) -> list[str]:
    """전용 서버가 짓지 않는 엔진 폴더(`Source/Engine/` 기준) — 정본은 `cmake/Engine/BuildLayout.cmake` 의 `SW_UNBUILT_ENGINE_FOLDERS` 한 곳이다."""
    text = (projectRoot / "cmake/Engine/BuildLayout.cmake").read_text(encoding="utf-8")
    listFolder: list[str] = []
    for match in kUnbuiltFolderLine.finditer(text):
        listFolder.extend(name.strip('"') for name in match.group(1).split())
    if not listFolder:
        raise GeneratorError("cmake/Engine/BuildLayout.cmake: SW_UNBUILT_ENGINE_FOLDERS 를 읽지 못했다")
    return listFolder


def checkServerGateCoversUnbuiltFolders(listPrefix: list[str], listUnbuiltFolder: list[str]) -> None:
    """서버 게이트가 서버가 짓지 않는 엔진 폴더를 모두 막는지 본다 — 빠지면 서버 구성이 짓지 않는 헤더를 읽어 링크에서야 드러난다."""
    listMissing = [f"Engine/{folder}" for folder in listUnbuiltFolder if f"Engine/{folder}" not in listPrefix]
    if listMissing:
        raise GeneratorError(f"[gate] {kServerGateName} 에 서버가 짓지 않는 엔진 폴더가 빠졌다: {', '.join(listMissing)} — {kRuleFile.name} 에 더하세요(SW_UNBUILT_ENGINE_FOLDERS)")


def isUnderFolderName(header: str, setFolderName: set[str]) -> bool:
    """@p header 의 폴더 마디 중 하나가 @p setFolderName 에 있으면 참이다(파일 이름은 보지 않는다)."""
    return any(segment in setFolderName for segment in header.split("/")[:-1])


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
    """게이트 하나의 껍데기를 맞춘다. 스탬프(문구 해시 + 지난 헤더 목록)와 대조해 **바뀐 껍데기만** 더하고 지운다.

    헤더 하나가 늘거나 옮겨질 때마다 그 폴더를 막는 게이트 전부가 폴더째 지우고 다시 쓰면(게이트 101 개 · 껍데기 5 만여 개) 재구성 46 초 중 31 초가
    이 생성기였다. 문구가 바뀌었거나 스탬프 · 폴더가 없을 때만 통째로 다시 쓴다(사라진 헤더의 껍데기가 옮겨 온 헤더를 가리지 않게 지우는 것은 같다).
    """
    stubRoot = stubDir / gateName
    stampPath = stubDir / f"{gateName}.stamp"
    content = f"#error \"{message}\"\n"
    contentDigest = hashlib.sha256(content.encode("utf-8")).hexdigest()
    setPrevious: set[str] | None = None
    if stampPath.is_file() and stubRoot.is_dir():
        listStampLine = stampPath.read_text(encoding="utf-8").splitlines()
        if listStampLine and listStampLine[0] == contentDigest:
            setPrevious = set(listStampLine[1:])
    if setPrevious is None:
        if stubRoot.exists():
            shutil.rmtree(stubRoot)
        stubRoot.mkdir(parents=True)
        setPrevious = set()
    setCurrent = set(listHeader)
    if setCurrent == setPrevious:
        return
    for header in setPrevious - setCurrent:
        (stubRoot / header).unlink(missing_ok=True)
    setFolder: set[Path] = set()
    for header in sorted(setCurrent - setPrevious):
        path = stubRoot / header
        if path.parent not in setFolder:
            path.parent.mkdir(parents=True, exist_ok=True)
            setFolder.add(path.parent)
        path.write_text(content, encoding="utf-8")
    stampPath.parent.mkdir(parents=True, exist_ok=True)
    stampPath.write_text("\n".join([contentDigest, *sorted(setCurrent)]) + "\n", encoding="utf-8")


def checkAllowClosure(sourceDir: Path, gateName: str, listAllowed: list[str], setDenied: set[str]) -> None:
    """연 헤더가 막힌 헤더를 include 하면 멈춘다 — 컴파일 오류가 아니라 표를 고칠 자리로 알린다. 연 헤더마다 보므로 전이도 잡힌다."""
    listProblem: list[str] = []
    for header in listAllowed:
        for included in kQuotedInclude.findall((sourceDir / header).read_text(encoding="utf-8")):
            if included in setDenied:
                listProblem.append(f"{header} -> {included}")
    if listProblem:
        raise GeneratorError(f"[allow] {gateName}: 연 헤더가 막힌 헤더를 include 한다 — 그 헤더도 [allow] 에 더하거나 include 를 끊으세요: {'; '.join(listProblem)}")


def collectGates(sourceDir: Path, mapRule: dict[str, list[str]], mapAllow: dict[str, list[str]], mapFolder: dict[str, list[str]]) -> dict[str, tuple[list[str], str]]:
    """게이트 이름 → (막는 헤더, 오류 문구). 파일을 쓰지 않는다 — 표를 코드와 대조하는 쪽도 이 함수를 쓴다."""
    listHeader = sorted(path.relative_to(sourceDir).as_posix() for path in sourceDir.rglob("*") if path.is_file() and path.suffix in kHeaderSuffix)
    listStale = [prefix for listPrefix in mapRule.values() for prefix in listPrefix if not (sourceDir / prefix).exists()]
    if listStale:
        raise GeneratorError(f"폴더가 없는 게이트 줄: {', '.join(sorted(set(listStale)))} — {kRuleFile.name} 에서 지우세요")
    listUnknownAllow = [gateName for gateName in mapAllow if gateName not in mapRule]
    if listUnknownAllow:
        raise GeneratorError(f"[allow] 에 [gate] 에 없는 게이트: {', '.join(sorted(listUnknownAllow))}")
    listMissingAllow = [header for listAllowed in mapAllow.values() for header in listAllowed if not (sourceDir / header).is_file()]
    if listMissingAllow:
        raise GeneratorError(f"[allow] 의 헤더가 없다: {', '.join(sorted(set(listMissingAllow)))} — {kRuleFile.name} 를 고치세요")
    listUnknownFolderGate = [gateName for gateName in mapFolder if gateName not in mapRule]
    if listUnknownFolderGate:
        raise GeneratorError(f"[gatefolder] 에 [gate] 에 없는 게이트: {', '.join(sorted(listUnknownFolderGate))}")
    setPresentFolder = {segment for header in listHeader for segment in header.split("/")[:-1]}
    listStaleFolder = [name for listName in mapFolder.values() for name in listName if name not in setPresentFolder]
    if listStaleFolder:
        raise GeneratorError(f"[gatefolder] 의 폴더 이름을 가진 헤더가 없다: {', '.join(sorted(set(listStaleFolder)))} — {kRuleFile.name} 에서 지우세요")
    mapGate: dict[str, tuple[list[str], str]] = {}
    for gateName, listPrefix in mapRule.items():
        setAllowed = set(mapAllow.get(gateName, []))
        setFolderName = set(mapFolder.get(gateName, []))
        listDenied = [header for header in listHeader
                      if (any(header.startswith(prefix + "/") for prefix in listPrefix) or isUnderFolderName(header, setFolderName)) and header not in setAllowed]
        if setAllowed:
            checkAllowClosure(sourceDir, gateName, sorted(setAllowed), set(listDenied))
        if gateName == kServerGateName:
            message = f"Target gate '{gateName}' forbids this header (Config/Rules/Layers/TargetLayers.toml): a dedicated server does not build client-only code (Engine Audio/Renderer/UI, Client/ folders). Call it through a seam (GameSound, IHUDViewModelBinder, IGameClientPart, IEngineRenderDriver ...) or move the caller into a Client/ folder."
        elif gateName == kClientGateName:
            message = f"Target gate '{gateName}' forbids this header (Config/Rules/Layers/TargetLayers.toml): a client build does not build server-only code (Server/ folders). Call it through a seam (IOnlineLocalServerFactory ...), move a shared type out of the Server/ folder, or move the caller into a Server/ folder."
        else:
            message = f"Target gate '{gateName}' forbids this header (Config/Rules/Layers/TargetLayers.toml): a lower target must not see an upper one. Move what you need down, or invert the dependency."
        mapGate[gateName] = (listDenied, message)

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
        table = tomllib.loads(kRuleFile.read_text(encoding="utf-8"))
        mapRule: dict[str, list[str]] = table["gate"]
        mapAllow: dict[str, list[str]] = table["allow"]
        mapFolder: dict[str, list[str]] = table["gatefolder"]
        listServerPrefix = mapRule[kServerGateName]
        if kClientGateName not in mapRule:   # CMake(`sw_applyTargetLayerGate`)가 Client 구성에 거는 게이트
            raise KeyError(kClientGateName)
    except (OSError, tomllib.TOMLDecodeError, KeyError) as exception:
        raise GeneratorError(f"{kRuleFile.name}: 읽지 못했다 ({exception!r}) — [gate] · [gatefolder] · [allow] 표와 [gate] {kServerGateName} · {kClientGateName} 가 있어야 한다") from exception
    projectRoot = getProjectRoot()
    checkServerGateCoversUnbuiltFolders(listServerPrefix, readServerUnbuiltFolders(projectRoot))
    sourceDir = projectRoot / "Source"
    mapGate = collectGates(sourceDir, mapRule, mapAllow, mapFolder)
    stubDir = outputPath.parent.parent / kStubFolderName
    for gateName, (listDenied, message) in mapGate.items():
        writeGateStubs(stubDir, gateName, listDenied, message)
    listLine = [
        "# AUTO-GENERATED by Scripts/generate/TargetLayers.py — do not edit. 쓰는 곳: sw_applyTargetLayerGate(cmake/Engine/TargetCompileRules.cmake).",
        f'set(SW_TARGET_LAYER_STUB_DIR "{stubDir.as_posix()}")',
        f'set(SW_TARGET_LAYER_GATES "{";".join(sorted(mapGate))}")',
        *(f'set(SW_TARGET_LAYER_ALLOW_{gateName} "{";".join(sorted(listAllowed))}")' for gateName, listAllowed in sorted(mapAllow.items())),
        "",
    ]
    writeGeneratedFile(outputPath, "\n".join(listLine), tag=kTag, summary=f"{len(mapGate)} gates · {sum(len(entry[0]) for entry in mapGate.values())} stubs")


def main(argv: Sequence[str] | None = None) -> int:
    return runGenerator(argv, tag=kTag, description="타깃 경계 표 → #error 껍데기",
                        addArguments=lambda parser: parser.add_argument("output", type=Path, help="쓸 .cmake 파일"),
                        generate=lambda args: generateTargetLayers(args.output))


if __name__ == "__main__":
    sys.exit(main())
