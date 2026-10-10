#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MoveEngineFolders.py
@brief Engine 폴더 재배치(docs/plans/EnginePartition.md 0-3)의 이동 표와 include 치환 — 단계마다 다시 돌릴 수 있다.

    py -3 Scripts/dev/MoveEngineFolders.py --step 1          # 1 단계 이동 + 치환
    py -3 Scripts/dev/MoveEngineFolders.py --step 1 --dry-run
    py -3 Scripts/dev/MoveEngineFolders.py --all             # 표의 모든 단계

이동은 `git mv` 이고, 원본이 없고 대상이 있으면 이미 옮긴 것으로 보고 넘어간다(재실행해도 같은 결과).
치환은 추적되는 글 파일 전부에서 `Engine/<옛 경로>` 를 `Engine/<새 경로>` 로 바꾼다(앞뒤가 이름 글자가 아닐 때만).
파일 이동은 확장자 없는 줄기(`Resource/AnimationAssetCache`)로 적고 `.h` · `.cpp` · `.inl` 을 같이 옮긴다.
`Engine/` 접두가 없는 경로(CMake 의 상대 경로, 문서의 상대 링크)는 단계의 `listTextReplace` 로 파일을 집어 바꾼다.
옮긴 뒤에는 include 순서 수정기(`Scripts/lint/fixer/FormatIncludeOrder.py`)를 바뀐 파일에 돌리고, reconfigure 로 코드젠을 다시 만든다.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot  # noqa: E402
from common.Host import runGit  # noqa: E402

_kEngineRoot = "Source/Engine"

#: 줄기 이동에서 같이 옮기는 확장자.
_kTupleStemExtension = (".h", ".cpp", ".inl", ".hpp")

#: 치환할 글 파일의 확장자.
_kSetTextExtension = frozenset({".h", ".hpp", ".cpp", ".inl", ".c", ".xxx", ".cmake", ".txt", ".py", ".md", ".json", ".hlsl", ".hlsli", ".xml",
                                ".yml", ".yaml", ".in", ".natvis"})


@dataclass(frozen=True)
class MoveStep:
    """
    한 단계 — `listMove` 는 Source/Engine 기준 (옛 경로, 새 경로).
    `listTextReplace` 는 `Engine/` 접두 없는 글(문서의 상대 링크 · 주석의 짧은 경로)을 고치는 (정규식, 새 글)이다. 이력 파일(`_kTupleHistoryFile`)은 건드리지 않는다.
    `listTestMove` 는 저장소 기준 시험 파일 이동이다(시험은 소스의 최상위 폴더를 따른다 — Test/README.md). 경로 글은 그대로 치환한다.
    """

    title: str
    listMove: tuple[tuple[str, str], ...]
    listTextReplace: tuple[tuple[str, str], ...] = field(default_factory=tuple)
    listTestMove: tuple[tuple[str, str], ...] = field(default_factory=tuple)
    #: 참이면 `Engine/` 접두 없는 짧은 경로(모듈 문서의 `Physics/AABB`)도 이동 표대로 고친다.
    bLoosePath: bool = False


#: 옛 이름을 기록으로 남기는 파일 — 치환하지 않는다(이 스크립트의 표도).
_kTupleHistoryFile = ("docs/09_Decisions.md", "docs/plans/EnginePartition.md", "Scripts/dev/MoveEngineFolders.py")

_kMapStep: dict[int, MoveStep] = {
    1: MoveStep(
        "기능 캐시를 자기 폴더로",
        (
            ("Resource/AnimationAssetCache", "Animation/AnimationAssetCache"),
            ("Resource/SpriteClipCache", "Animation/SpriteClipCache"),
            ("Resource/LocalizationReloadCache", "Localization/LocalizationReloadCache"),
        ),
    ),
    2: MoveStep(
        "Graphics/Renderer 를 Engine/Renderer 로",
        (("Graphics/Renderer", "Renderer"),),
        ((r"(?<![\w])Graphics/Renderer(?![\w])", "Renderer"),),
    ),
    3: MoveStep(
        "애니메이션 알림 계약은 Animation/Notify, 캐릭터 포즈 수정은 Character/PoseModifier",
        (
            ("Animation/AnimNotifyPhase", "Animation/Notify/AnimNotifyPhase"),
            ("Object/Animation/AnimNotifyListener", "Animation/Notify/AnimNotifyListener"),
            ("Character/Pose", "Character/PoseModifier"),
        ),
        ((r"(?<![\w/])Character/Pose/", "Character/PoseModifier/"), (r"(?<![\w/])Object/Animation/AnimNotifyListener", "Animation/Notify/AnimNotifyListener")),
    ),
    4: MoveStep(
        "창 표면 계약은 RHI 로, 서버 설정은 Config/Server 로",
        (
            ("Common/IRenderSurface", "Graphics/RHI/IRenderSurface"),
            ("Config/ServerConfig", "Config/Server/ServerConfig"),
            ("Config/ServerSecret", "Config/Server/ServerSecret"),
        ),
        ((r"(?<![\w/])Common/IRenderSurface", "Graphics/RHI/IRenderSurface"),),
    ),
    5: MoveStep(
        "Utility 해체 — XML · Json 은 Serialization, TileMap · Console · Profiling 은 자기 최상위 폴더",
        (
            ("Utility/XML/TileMapXml", "TileMap/TileMapXml"),
            ("Utility/TileMap", "TileMap"),
            ("Utility/XML", "Serialization/XML"),
            ("Utility/Json", "Serialization/Json"),
            ("Utility/Console", "Console"),
            ("Utility/Profiling", "Profiling"),
        ),
        (
            (r"(?<![\w/])Utility/XML/TileMapXml", "TileMap/TileMapXml"),
            (r"(?<![\w/])Utility/(TileMap|Console|Profiling)(?![\w])", r"\1"),
            (r"(?<![\w/])Utility/(XML|Json)(?![\w])", r"Serialization/\1"),
            (r"(?<=\{CMAKE_CURRENT_SOURCE_DIR\}/)Utility/Profiling/", "Profiling/"),
        ),
        (
            ("Test/EngineTest/Utility/TestJsonDocument.cpp", "Test/EngineTest/Serialization/TestJsonDocument.cpp"),
            ("Test/EngineTest/Utility/TestXMLDocument.cpp", "Test/EngineTest/Serialization/TestXMLDocument.cpp"),
            ("Test/EngineTest/Utility/TestTileGridUtil.cpp", "Test/EngineTest/TileMap/TestTileGridUtil.cpp"),
            ("Test/EngineTest/Utility/TestTileMapXML.cpp", "Test/EngineTest/TileMap/TestTileMapXML.cpp"),
            ("Test/EngineTest/Utility/TestTileSet.cpp", "Test/EngineTest/TileMap/TestTileSet.cpp"),
            ("Test/EngineTest/Utility/TestFrameProfileSession.cpp", "Test/EngineTest/Profiling/TestFrameProfileSession.cpp"),
            ("Test/EngineTest/Utility/TestFrameProfiler.cpp", "Test/EngineTest/Profiling/TestFrameProfiler.cpp"),
            ("Test/EngineTest/Utility/TestMemoryBudgetMonitor.cpp", "Test/EngineTest/Profiling/TestMemoryBudgetMonitor.cpp"),
            ("Test/EngineTest/Utility/TestProfilerBackend.cpp", "Test/EngineTest/Profiling/TestProfilerBackend.cpp"),
        ),
    ),
    6: MoveStep(
        "이름 정리 — HTTP 는 Observability 한 곳, UI/Screens 를 UI/Screen 에, Core 와 겹치는 하위 폴더 이름은 Base",
        (
            ("Telemetry/HttpClient", "Observability/HttpClient"),
            ("UI/Screens", "UI/Screen"),
            ("Serialization/Core", "Serialization/Base"),
            ("UI/Core", "UI/Base"),
        ),
        bLoosePath=True,
    ),
    7: MoveStep(
        "큰 평평한 폴더를 하위 폴더로 — Physics · Resource · Animation · Input",
        (
            ("Physics/AABB", "Physics/Collision/AABB"),
            ("Physics/CollisionLayers", "Physics/Collision/CollisionLayers"),
            ("Physics/ContinuousCollision", "Physics/Collision/ContinuousCollision"),
            ("Physics/PhysicsContact", "Physics/Collision/PhysicsContact"),
            ("Physics/PhysicsPairFilter", "Physics/Collision/PhysicsPairFilter"),
            ("Physics/PhysicsQuery", "Physics/Collision/PhysicsQuery"),
            ("Physics/PhysicsShape", "Physics/Collision/PhysicsShape"),
            ("Physics/PhysicsAsset", "Physics/Asset/PhysicsAsset"),
            ("Physics/PhysicsRagdoll", "Physics/Asset/PhysicsRagdoll"),
            ("Resource/PackCompressionUtil", "Resource/Pack/PackCompressionUtil"),
            ("Resource/ResourcePackManager", "Resource/Pack/ResourcePackManager"),
            ("Resource/ResourcePackReader", "Resource/Pack/ResourcePackReader"),
            ("Resource/ResourcePackTypes", "Resource/Pack/ResourcePackTypes"),
            ("Resource/DDSFormat", "Resource/Image/DDSFormat"),
            ("Resource/DDSLoader", "Resource/Image/DDSLoader"),
            ("Resource/ImageFileWriter", "Resource/Image/ImageFileWriter"),
            ("Resource/IAssetCache", "Resource/Cache/IAssetCache"),
            ("Resource/SharedAssetTable", "Resource/Cache/SharedAssetTable"),
            ("Resource/WeakInternCache", "Resource/Cache/WeakInternCache"),
            ("Resource/WeakInternTable", "Resource/Cache/WeakInternTable"),
            ("Animation/SpriteClipAsset", "Animation/Sprite/SpriteClipAsset"),
            ("Animation/SpriteClipCache", "Animation/Sprite/SpriteClipCache"),
            ("Animation/SpriteClipPlayable", "Animation/Sprite/SpriteClipPlayable"),
            ("Animation/AnimGraphAsset", "Animation/Graph/AnimGraphAsset"),
            ("Animation/AnimGraphPlayer", "Animation/Graph/AnimGraphPlayer"),
            ("Animation/BlendSpace", "Animation/Graph/BlendSpace"),
            ("Animation/BlendCurve", "Animation/Graph/BlendCurve"),
            ("Animation/Skeleton", "Animation/Skeletal/Skeleton"),
            ("Animation/SkeletonBoneLOD", "Animation/Skeletal/SkeletonBoneLOD"),
            ("Animation/Pose", "Animation/Skeletal/Pose"),
            ("Animation/DualQuaternion", "Animation/Skeletal/DualQuaternion"),
            ("Input/InputMap", "Input/Map/InputMap"),
            ("Input/InputMapCombo", "Input/Map/InputMapCombo"),
            ("Input/InputMapEvaluate", "Input/Map/InputMapEvaluate"),
            ("Input/InputMapGlyph", "Input/Map/InputMapGlyph"),
            ("Input/InputMapSerialization", "Input/Map/InputMapSerialization"),
            ("Input/InputKeyMap", "Input/Map/InputKeyMap"),
            ("Input/IVirtualInputSource", "Input/Virtual/IVirtualInputSource"),
            ("Input/VirtualInputScript", "Input/Virtual/VirtualInputScript"),
            ("Input/VirtualJoystick", "Input/Virtual/VirtualJoystick"),
            ("Input/InputReplay", "Input/Virtual/InputReplay"),
        ),
        bLoosePath=True,
    ),
}


def isStemInternal(engineRoot: Path, relativePath: str) -> bool:
    """확장자 없는 줄기 이동인가(같은 이름 폴더가 없고 확장자 붙은 파일이 있는 경우)."""
    return any((engineRoot / (relativePath + extension)).is_file() for extension in _kTupleStemExtension)


def listPhysicalMoveInternal(engineRoot: Path, oldPath: str, newPath: str) -> list[tuple[Path, Path]]:
    """한 줄의 이동을 실제 파일 · 폴더 쌍으로 푼다. 이미 옮긴 것(원본 없음)은 빈 목록."""
    oldFull = engineRoot / oldPath
    newFull = engineRoot / newPath
    if oldFull.is_dir():
        return [(oldFull, newFull)]
    if isStemInternal(engineRoot, oldPath):
        return [(engineRoot / (oldPath + extension), engineRoot / (newPath + extension)) for extension in _kTupleStemExtension
                if (engineRoot / (oldPath + extension)).is_file()]
    return []


def moveInternal(root: Path, source: Path, target: Path, bDryRun: bool) -> None:
    print(f"{'옮길 것' if bDryRun else '옮김'}: {source.relative_to(root).as_posix()} -> {target.relative_to(root).as_posix()}")
    if bDryRun:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir() and target.is_dir():
        # 대상 폴더가 이미 있으면 안의 항목을 하나씩 옮긴다(병합).
        for child in sorted(source.iterdir()):
            moveInternal(root, child, target / child.name, bDryRun)
        source.rmdir()
        return
    result = runGit(["mv", source.relative_to(root).as_posix(), target.relative_to(root).as_posix()], cwd=root)
    if result.returnCode != 0:
        raise SystemExit(f"git mv 실패: {source} -> {target}\n{result.stderr}")


def buildPatternInternal(oldPath: str, bStem: bool) -> re.Pattern[str]:
    tail = r"(?=\.)" if bStem else r"(?![\w])"
    return re.compile(r"(?<![\w])Engine/" + re.escape(oldPath) + tail)


def listTrackedTextFileInternal(root: Path) -> list[Path]:
    result = runGit(["ls-files", "-z"], cwd=root)
    if result.returnCode != 0:
        raise SystemExit(f"git ls-files 실패: {result.stderr}")
    listFile = []
    for name in result.stdout.split("\0"):
        if not name:
            continue
        path = root / name
        if (path.suffix.lower() in _kSetTextExtension or path.name == "CMakeLists.txt") and path.is_file():
            listFile.append(path)
    return listFile


def rewriteInternal(root: Path, listPattern: list[tuple[re.Pattern[str], str]], listTextReplace: tuple[tuple[str, str], ...],
                    bDryRun: bool) -> list[Path]:
    listChanged: list[Path] = []
    for path in listTrackedTextFileInternal(root):
        try:
            with path.open("r", encoding="utf-8", newline="") as stream:
                text = stream.read()
        except (UnicodeDecodeError, OSError):
            continue
        if path.relative_to(root).as_posix() in _kTupleHistoryFile:
            continue
        newText = text
        for pattern, replacement in listPattern:
            newText = pattern.sub(replacement, newText)
        for patternText, replaceText in listTextReplace:
            newText = re.sub(patternText, replaceText, newText)
        if newText != text:
            listChanged.append(path)
            if not bDryRun:
                with path.open("w", encoding="utf-8", newline="") as stream:
                    stream.write(newText)
    return listChanged


def runStepInternal(root: Path, stepNumber: int, bDryRun: bool) -> list[Path]:
    step = _kMapStep[stepNumber]
    engineRoot = root / _kEngineRoot
    print(f"[MoveEngineFolders] {stepNumber}. {step.title}")
    listPattern: list[tuple[re.Pattern[str], str]] = []
    for oldPath, newPath in step.listMove:
        bStem = isStemInternal(engineRoot, oldPath) or isStemInternal(engineRoot, newPath)
        for source, target in listPhysicalMoveInternal(engineRoot, oldPath, newPath):
            moveInternal(root, source, target, bDryRun)
        listPattern.append((buildPatternInternal(oldPath, bStem), "Engine/" + newPath))
        if step.bLoosePath:
            listPattern.append((re.compile(r"(?<![\w/])" + re.escape(oldPath) + r"(?![\w])"), newPath))
            listPattern.append((re.compile(r"(?<![\w])Engine/" + re.escape(oldPath) + r"(?![\w])"), "Engine/" + newPath))
    for oldPath, newPath in step.listTestMove:
        if (root / oldPath).is_file():
            moveInternal(root, root / oldPath, root / newPath, bDryRun)
        listPattern.append((re.compile(re.escape(oldPath) + r"(?![\w])"), newPath))
    listChanged = rewriteInternal(root, listPattern, step.listTextReplace, bDryRun)
    listChanged += fixMarkdownLinkInternal(root, step, bDryRun)
    for path in sorted(set(listChanged)):
        print(f"  치환: {path.relative_to(root).as_posix()}")
    return listChanged


_kMarkdownLinkRe = re.compile(r"\]\(([^)\s#]+)(#[^)\s]*)?\)")


def mapOldToNewInternal(listMovePair: list[tuple[str, str]], posixPath: str) -> str | None:
    """Source/Engine 기준 옛 경로(파일 · 폴더 · 줄기) → 새 경로. 이동 표에 걸리지 않으면 None."""
    for oldPath, newPath in listMovePair:
        if posixPath == oldPath or posixPath.startswith(oldPath + "/"):
            return newPath + posixPath[len(oldPath):]
        if posixPath.startswith(oldPath + ".") and "/" not in posixPath[len(oldPath):]:
            return newPath + posixPath[len(oldPath):]
    return None


def fixMarkdownLinkInternal(root: Path, step: MoveStep, bDryRun: bool) -> list[Path]:
    """
    문서의 상대 링크를 옮긴 자리에 맞춘다. 문서가 옮겨졌으면 옛 자리에서, 아니면 제자리에서 링크를 풀고,
    그 대상이 이동 표에 걸리면 새 대상으로 바꾼 뒤 문서의 새 자리 기준 상대 경로로 다시 적는다. 지금 자리에서 이미 풀리는 링크는 그대로 둔다.
    """
    engineRoot = (root / _kEngineRoot).resolve()
    listMovePair = list(step.listMove)
    listInverse = [(newPath, oldPath) for oldPath, newPath in listMovePair]
    listChanged: list[Path] = []
    for path in listTrackedTextFileInternal(root):
        if path.suffix.lower() != ".md" or path.relative_to(root).as_posix() in _kTupleHistoryFile:
            continue
        with path.open("r", encoding="utf-8", newline="") as stream:
            text = stream.read()
        newDir = path.parent.resolve()
        oldDir = newDir
        try:
            engineRelative = newDir.relative_to(engineRoot).as_posix()
            oldRelative = mapOldToNewInternal(listInverse, engineRelative)
            if oldRelative is not None:
                oldDir = engineRoot / oldRelative
        except ValueError:
            pass

        def replaceLink(match: re.Match[str]) -> str:
            target = match.group(1)
            if "://" in target or target.startswith("mailto:") or (newDir / target).exists():
                return match.group(0)
            oldResolved = Path(os.path.normpath(oldDir / target))
            try:
                oldEngineRelative = oldResolved.relative_to(engineRoot).as_posix()
            except ValueError:
                oldEngineRelative = None
            mapped = mapOldToNewInternal(listMovePair, oldEngineRelative) if oldEngineRelative is not None else None
            newTarget = engineRoot / mapped if mapped is not None else oldResolved
            if not newTarget.exists():
                return match.group(0)
            relativeText = Path(os.path.relpath(newTarget, newDir)).as_posix()
            return f"]({relativeText}{match.group(2) or ''})"

        newText = _kMarkdownLinkRe.sub(replaceLink, text)
        if newText != text:
            listChanged.append(path)
            if not bDryRun:
                with path.open("w", encoding="utf-8", newline="") as stream:
                    stream.write(newText)
    return listChanged


_kTierEntryRe = re.compile(r'^\s*(?:"([^"]+)"|(_k\w+))\s*:\s*(\d+),\s*$')


def computeLayerTierInternal(root: Path) -> dict[str, int]:
    """`RunEngineLayerGraph` 와 같은 계산(Kahn 최장 경로). 묶음(SCC) 안의 층과 그 위는 의존 대상의 최대 + 1 로 채운다."""
    sys.path.insert(0, str(root / "Scripts/lint"))
    sys.path.insert(0, str(root / "Scripts/lint/gate"))
    sys.path.insert(0, str(root / "Scripts/lint/report"))
    import RunEngineLayerGraph as graph  # noqa: E402

    mapEdge = graph.collectEdgesInternal(root)
    mapTier = graph.computeTiersInternal(mapEdge)
    uniqueNode = set(mapEdge)
    for source in list(mapEdge):
        uniqueNode.update(mapEdge[source])
    uniqueRemaining = uniqueNode - set(mapTier)

    def tierAboveInternal(listMember: list[str]) -> int:
        return max((mapTier[dep] for member in listMember for dep in mapEdge.get(member, {}) if dep in mapTier), default=-1) + 1

    while uniqueRemaining:
        listReady = sorted(node for node in uniqueRemaining if all(dep in mapTier for dep in mapEdge.get(node, {})))
        if listReady:
            for node in listReady:
                mapTier[node] = tierAboveInternal([node])
            uniqueRemaining -= set(listReady)
            continue
        # 묶음은 한 덩어리로 놓는다 — 바깥 의존이 다 놓인 묶음을 같은 티어에.
        mapSubEdge = {node: {dep: [] for dep in mapEdge.get(node, {}) if dep in uniqueRemaining} for node in uniqueRemaining}
        listCycle = [component for component in graph.findComponentsInternal(mapSubEdge)
                     if all(dep in mapTier or dep in component for member in component for dep in mapEdge.get(member, {}))]
        if not listCycle:
            break
        tier = tierAboveInternal(listCycle[0])
        for node in listCycle[0]:
            mapTier[node] = tier
        uniqueRemaining -= set(listCycle[0])
    return mapTier


def syncTierTableInternal(root: Path, bDryRun: bool) -> None:
    """게이트의 `_kEngineTier` 숫자를 계산값으로 바꾸고 (티어, 원래 순서)로 다시 줄 세운다. 항목 = 바로 위 주석 줄 + 키 줄."""
    gatePath = root / "Scripts/lint/gate/CheckEngineLayers.py"
    sys.path.insert(0, str(root / "Scripts/lint/gate"))
    import CheckEngineLayers as gate  # noqa: E402

    mapTier = computeLayerTierInternal(root)
    with gatePath.open("r", encoding="utf-8", newline="") as stream:
        text = stream.read()
    newline = "\r\n" if "\r\n" in text else "\n"
    listLine = text.split(newline)
    beginIndex = next(index for index, line in enumerate(listLine) if line.startswith("_kEngineTier: dict[str, int] = {"))
    endIndex = next(index for index in range(beginIndex, len(listLine)) if listLine[index] == "}")
    listEntry: list[tuple[int, int, str, list[str]]] = []
    listComment: list[str] = []
    for line in listLine[beginIndex + 1 : endIndex]:
        match = _kTierEntryRe.match(line)
        if not match:
            listComment.append(line)
            continue
        layerName = match.group(1) or getattr(gate, match.group(2))
        tier = mapTier.get(layerName, int(match.group(3)))
        if layerName == gate._kRootLayerName and layerName not in mapTier:
            tier = max(mapTier.values(), default=0) + 1
        keyText = f'"{match.group(1)}"' if match.group(1) else match.group(2)
        listEntry.append((tier, len(listEntry), layerName, listComment + [f"    {keyText}: {tier},"]))
        listComment = []
    listEntry.sort(key=lambda entry: (entry[0], entry[1]))
    listBody = [line for entry in listEntry for line in entry[3]] + listComment
    newText = newline.join(listLine[: beginIndex + 1] + listBody + listLine[endIndex:])
    print("[MoveEngineFolders] 티어 표:")
    mapRow: dict[int, list[str]] = {}
    for tier, _, layerName, _ in listEntry:
        mapRow.setdefault(tier, []).append(layerName)
    for tier in sorted(mapRow):
        print(f"  | {tier} | " + ", ".join(f"`{name}`" for name in mapRow[tier]) + " |")
    if newText != text and not bDryRun:
        with gatePath.open("w", encoding="utf-8", newline="") as stream:
            stream.write(newText)
        print("  게이트 표를 고쳤다")

    # Source/Engine/README.md "머릿속 그림" 의 `| 티어 | 폴더 |` 표를 같은 값으로 다시 쓴다.
    readmePath = root / _kEngineRoot / "README.md"
    with readmePath.open("r", encoding="utf-8", newline="") as stream:
        readmeText = stream.read()
    listReadmeLine = readmeText.split(newline)
    headerIndex = next((index for index, line in enumerate(listReadmeLine) if line.startswith("| 티어 | 폴더 |")), None)
    if headerIndex is None:
        return
    endRowIndex = headerIndex + 2
    while endRowIndex < len(listReadmeLine) and listReadmeLine[endRowIndex].startswith("|"):
        endRowIndex += 1
    listRow = []
    for tier in sorted(mapRow):
        listName = ["`EngineLoop` 등 루트 파일" if name == gate._kRootLayerName else f"`{name}`" for name in mapRow[tier]]
        listRow.append(f"| {tier} | {', '.join(listName)} |")
    listReadmeLine[headerIndex : endRowIndex] = ["| 티어 | 폴더 |", "|---|---|"] + listRow
    newReadmeText = newline.join(listReadmeLine)
    if newReadmeText != readmeText and not bDryRun:
        with readmePath.open("w", encoding="utf-8", newline="") as stream:
            stream.write(newReadmeText)
        print("  README 티어 표를 고쳤다")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Engine 폴더 재배치의 이동 표를 적용한다(git mv + 경로 치환).")
    parser.add_argument("--step", type=int, action="append", default=[], help="적용할 단계(여러 번 줄 수 있다)")
    parser.add_argument("--all", action="store_true", help="표의 모든 단계")
    parser.add_argument("--sync-tier", action="store_true", help="게이트의 티어 표를 include 그래프 계산값으로 맞춘다(이동 뒤)")
    parser.add_argument("--dry-run", action="store_true", help="옮기지 않고 할 일만 보인다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    root = (args.root or getProjectRoot()).resolve()
    listStep = sorted(_kMapStep) if args.all else args.step
    if args.sync_tier and not listStep:
        syncTierTableInternal(root, args.dry_run)
        return 0
    if not listStep:
        parser.error("--step 이나 --all 을 주십시오")
    for stepNumber in listStep:
        if stepNumber not in _kMapStep:
            parser.error(f"표에 없는 단계: {stepNumber} (있는 것: {sorted(_kMapStep)})")
        runStepInternal(root, stepNumber, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
