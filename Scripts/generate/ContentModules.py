#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/ContentModules.py

게임 콘텐츠(`Content/game/<팩>` 의 씬 · 프리팹)가 쓰는 리플렉션 형식의 모듈이 그 게임 매니페스트의 의존(따라 닿는 것 포함)에 있는지 configure 가 본다.

**왜 configure 인가.** 키트는 게임 매니페스트의 의존으로만 링크된다(`sw_addGameModule`). 의존에 없는 키트의 컴포넌트를 쓰는 씬은 그 게임의 어떤 실행 ·
쿠킹 · 배포에서도 서지 못하는데, 예전에는 헤드리스 쿠킹이 "등록하는 모듈 없음" 으로 멈출 때야 드러났다(Empty 의 넷 씬 둘, 2026-10-31). 씬은 데이터라
형식으로 막을 수 없어서, 모듈을 정하는 자리(configure)에서 콘텐츠와 매니페스트를 맞댄다. 상용 엔진: 언리얼은 콘텐츠가 쓰는 플러그인을 `.uproject` 에
켜야 하고, 켜지 않으면 에셋 로드가 실패한다.

  - 형식 → 모듈: `Source` 헤더의 `REFLECT(...) class|struct 이름` 을 훑고, 가장 가까운 상위 폴더의 `*.module.json` 이 주인이다. 주인이 없으면
    Engine · Core 라 늘 있다. 이름이 여러 모듈에 있으면 그 가운데 하나라도 게임이 가지면 된다.
  - 게임 → 팩: 프리셋 `Config/Game/<게임>.json` 의 `_packRoot`(엔진이 쓰는 값 그대로). 없으면 멈춘다.
  - 게임이 가진 모듈: 자기 폴더, `_listDependency` 를 따라 닿는 모듈, `_listModuleOverride` 로 켠 모듈.
  - configure 가 돌 때만 본다 — 씬을 고친 뒤에는 다음 configure 가 본다(씬마다 configure 의존을 걸면 저장할 때마다 configure 가 돈다).

  python Scripts/generate/ContentModules.py
"""

from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.Paths import getProjectRoot  # noqa: E402
from common.ProjectConstants import kDirRuntimeGamePreset  # noqa: E402
from generate.GeneratorOutput import GeneratorError, runGenerator  # noqa: E402

kTag = "ContentModules"
kContentSuffix = (".scene.xml", ".prefab.xml")
kSkipSourceFolder = {"graphify-out"}
kReflectPattern = re.compile(r"\bREFLECT\s*\(")
kDeclarationPattern = re.compile(r"\s*(?:class|struct)\s+(?:[A-Z][A-Z0-9_]*_API\s+)?([A-Za-z_]\w*)")


def skipReflectArguments(text: str, openIndex: int) -> int:
    """`REFLECT(` 의 여는 괄호 자리에서 닫는 괄호 다음 자리를 돌려줍니다. 따옴표 안의 괄호는 세지 않습니다. 닫히지 않으면 -1."""
    depth = 0
    index = openIndex
    while index < len(text):
        character = text[index]
        if character == '"':
            index = text.find('"', index + 1)
            if index < 0:
                return -1
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    return -1


def readManifest(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exception:
        raise GeneratorError(f"{path}: 매니페스트를 읽지 못했다 ({exception!r})") from exception


def collectManifests(sourceRoot: Path) -> tuple[dict[Path, str], dict[str, dict]]:
    """(모듈 폴더 → 주인 키, 게임 아닌 모듈 이름 → 매니페스트). 게임은 이름이 모두 `SWGame` 이라 주인 키를 `game:<폴더>` 로 가른다."""
    mapFolder: dict[Path, str] = {}
    mapManifestByName: dict[str, dict] = {}
    for manifestPath in sourceRoot.rglob("*.module.json"):
        if kSkipSourceFolder.intersection(manifestPath.parts):
            continue
        manifest = readManifest(manifestPath)
        if manifest.get("_kind") == "Game":
            mapFolder[manifestPath.parent] = f"game:{manifestPath.parent.name}"
            continue
        mapFolder[manifestPath.parent] = manifest["_name"]
        mapManifestByName[manifest["_name"]] = manifest
    return mapFolder, mapManifestByName


def findOwner(path: Path, mapFolder: dict[Path, str], sourceRoot: Path) -> str | None:
    """가장 가까운 상위 모듈 폴더의 주인 키. 없으면 None(Engine · Core — 늘 있다)."""
    folder = path.parent
    while folder != sourceRoot and folder != folder.parent:
        owner = mapFolder.get(folder)
        if owner is not None:
            return owner
        folder = folder.parent
    return None


def collectTypeOwners(sourceRoot: Path, mapFolder: dict[Path, str]) -> dict[str, set[str | None]]:
    """리플렉션 형식 이름 → 주인 키 집합."""
    mapOwner: dict[str, set[str | None]] = {}
    for headerPath in sourceRoot.rglob("*.h"):
        if kSkipSourceFolder.intersection(headerPath.parts):
            continue
        text = headerPath.read_text(encoding="utf-8", errors="replace")
        if "REFLECT" not in text:
            continue
        owner = findOwner(headerPath, mapFolder, sourceRoot)
        for match in kReflectPattern.finditer(text):
            end = skipReflectArguments(text, match.end() - 1)
            declaration = kDeclarationPattern.match(text, end) if end >= 0 else None
            if declaration is not None:
                mapOwner.setdefault(declaration.group(1), set()).add(owner)
    return mapOwner


def collectGameModules(gameFolder: Path, manifest: dict, mapManifestByName: dict[str, dict]) -> set[str]:
    """게임이 가진 모듈 키 — 자기 폴더, 의존을 따라 닿는 것, 오버라이드로 켠 것."""
    listPending = [entry["_name"] for entry in manifest.get("_listDependency", [])]
    listPending += [entry["_name"] for entry in manifest.get("_listModuleOverride", []) if entry.get("_bEnabled")]
    setReached: set[str] = {f"game:{gameFolder.name}"}
    while listPending:
        name = listPending.pop()
        if name in setReached:
            continue
        setReached.add(name)
        dependency = mapManifestByName.get(name)
        if dependency is None:
            raise GeneratorError(f"{gameFolder.name}: 의존 '{name}' 의 매니페스트가 없다")
        listPending += [entry["_name"] for entry in dependency.get("_listDependency", [])]
    return setReached


def collectContentTypes(packFolder: Path) -> dict[str, list[str]]:
    """팩의 씬 · 프리팹이 쓰는 요소 이름 → 쓰는 파일(팩 기준)."""
    mapUse: dict[str, list[str]] = {}
    for path in sorted(packFolder.rglob("*.xml")):
        if not path.name.endswith(kContentSuffix):
            continue
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError as exception:
            raise GeneratorError(f"{path}: XML 을 읽지 못했다 ({exception})") from exception
        relative = path.relative_to(packFolder).as_posix()
        for tag in {element.tag for element in root.iter()}:
            mapUse.setdefault(tag, []).append(relative)
    return mapUse


def checkContentModules(projectRoot: Path) -> int:
    """어긋남이 있으면 `GeneratorError`. 본 팩 수를 돌려줍니다."""
    sourceRoot = projectRoot / "Source"
    mapFolder, mapManifestByName = collectManifests(sourceRoot)
    mapOwner = collectTypeOwners(sourceRoot, mapFolder)

    listProblem: list[str] = []
    listGameFolder = sorted(path.parent for path in (sourceRoot / "Games").glob("*/SWGame.module.json"))
    for gameFolder in listGameFolder:
        presetPath = projectRoot / kDirRuntimeGamePreset / f"{gameFolder.name}.json"
        try:
            packRoot = json.loads(presetPath.read_text(encoding="utf-8"))["_packRoot"]
        except (OSError, ValueError, KeyError) as exception:
            raise GeneratorError(f"{presetPath}: 게임 {gameFolder.name} 의 _packRoot 를 읽지 못했다 ({exception!r})") from exception
        packFolder = projectRoot / "Content" / packRoot
        if not packFolder.is_dir():
            raise GeneratorError(f"{presetPath}: _packRoot '{packRoot}' 폴더가 없다")
        setModule = collectGameModules(gameFolder, readManifest(gameFolder / "SWGame.module.json"), mapManifestByName)
        for typeName, listFile in sorted(collectContentTypes(packFolder).items()):
            setOwner = mapOwner.get(typeName)
            if setOwner is None or None in setOwner or setOwner & setModule:
                continue
            listOwner = ", ".join(sorted(owner.removeprefix("game:") + " 게임" if owner.startswith("game:") else owner for owner in setOwner))
            listProblem.append(f"{packRoot}/{listFile[0]}{' 외 ' + str(len(listFile) - 1) + ' 파일' if len(listFile) > 1 else ''} 이 쓰는 {typeName}({listOwner})")
        if listProblem and listProblem[-1].startswith(packRoot):
            listProblem.append(f"  → Source/Games/{gameFolder.name}/SWGame.module.json 의 _listDependency 에 그 모듈을 더하거나 콘텐츠에서 빼세요")
    if listProblem:
        raise GeneratorError("게임 매니페스트에 없는 모듈의 형식을 콘텐츠가 쓴다:\n  " + "\n  ".join(listProblem))
    return len(listGameFolder)


def main(argv: Sequence[str] | None = None) -> int:
    def generate(_args) -> None:
        packCount = checkContentModules(getProjectRoot())
        print(f"[{kTag}] {packCount} game packs: content uses only modules their manifests list")
    return runGenerator(argv, tag=kTag, description="게임 콘텐츠가 쓰는 형식의 모듈 ⊂ 게임 매니페스트 의존", addArguments=lambda parser: None,
                        generate=generate)


if __name__ == "__main__":
    sys.exit(main())
