#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/ContentModules.py

콘텐츠(씬 · 프리팹)가 쓰는 리플렉션 형식의 모듈을 그 도메인이 가질 수 있는지 빌드 단계(`ContentModuleCheck`)가 본다 — `game/<팩>` 은 그 게임
매니페스트의 의존(따라 닿는 것 포함), `common/` 은 모든 게임이 가진 것, `editor/` 는 엔진 · EditorModule, `engine/` 은 엔진만.

**왜 하는가.** 키트는 게임 매니페스트의 의존으로만 링크된다(`sw_addGameModule`). 의존에 없는 키트의 컴포넌트를 쓰는 씬은 그 게임의 어떤 실행 ·
쿠킹 · 배포에서도 서지 못하는데, 예전에는 헤드리스 쿠킹이 "등록하는 모듈 없음" 으로 멈출 때야 드러났다(Empty 의 넷 씬 둘, 2026-10-31). 씬은 데이터라
형식으로 막을 수 없어서 콘텐츠와 매니페스트를 맞댄다. 상용 엔진: 언리얼은 콘텐츠가 쓰는 플러그인을 `.uproject` 에 켜야 하고, 켜지 않으면 에셋 로드가 실패한다.

  - 형식 → 모듈: 리플렉션 대상마다 파서가 쓴 `ReflectedTypes.list`(`<FQN>\t<모듈>`)와 CMake 가 적은 그 대상의 주인(`--inputs` 파일의
    `<주인>\t<목록>` 줄, 주인이 비면 엔진 · 실행 파일이라 늘 있다)만 본다 — 헤더를 글자로 훑지 않는다(예전 정규식은 주석 · 템플릿의 REFLECT 도 셌다).
    이름이 여러 모듈에 있으면 그 가운데 하나라도 게임이 가지면 된다.
  - 게임 → 팩: 프리셋 `Config/Game/<게임>.json` 의 `_packRoot`(엔진이 쓰는 값 그대로). 없으면 멈춘다.
  - 게임이 가진 모듈: 자기 폴더, `_listDependency` 를 따라 닿는 모듈, `_listModuleOverride` 로 켠 모듈.
  - 타입 목록이 바뀔 때(또는 다시 구성할 때) 돈다 — 씬마다 의존을 걸면 저장할 때마다 돈다.

  python Scripts/generate/ContentModules.py --inputs <ContentModuleInputs.list> --stamp <ContentModules.stamp>
"""

from __future__ import annotations

import json
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
kScopeSeparator = "::"


def readManifest(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exception:
        raise GeneratorError(f"{path}: 매니페스트를 읽지 못했다 ({exception!r})") from exception


def collectManifests(sourceRoot: Path) -> dict[str, dict]:
    """게임 아닌 모듈 이름 → 매니페스트(게임 의존을 따라가는 데 쓴다)."""
    mapManifestByName: dict[str, dict] = {}
    for manifestPath in sourceRoot.rglob("*.module.json"):
        if kSkipSourceFolder.intersection(manifestPath.parts):
            continue
        manifest = readManifest(manifestPath)
        if manifest.get("_kind") != "Game":
            mapManifestByName[manifest["_name"]] = manifest
    return mapManifestByName


def readTypeOwners(inputsPath: Path) -> dict[str, set[str | None]]:
    """리플렉션 형식의 짧은 이름(씬 요소 이름) → 주인 키 집합. 주인 None 은 늘 있는 것(엔진 · 실행 파일)이다.

    `inputsPath` 의 줄은 `<주인>\t<ReflectedTypes.list>`(CMake `sw_addContentModuleCheck`), 목록의 줄은 `<FQN>\t<모듈>`(파서)이다.
    목록 파일이 없거나 줄 꼴이 틀리면 멈춘다 — 빠진 목록의 타입을 "엔진 것" 으로 보면 검사가 조용히 통과한다.
    """
    mapOwner: dict[str, set[str | None]] = {}
    try:
        listInputLine = inputsPath.read_text(encoding="utf-8").splitlines()
    except OSError as exception:
        raise GeneratorError(f"{inputsPath}: 입력 목록을 읽지 못했다 ({exception!r})") from exception
    for inputLine in listInputLine:
        if not inputLine.strip():
            continue
        ownerText, separator, listPathText = inputLine.partition("\t")
        if not separator or not listPathText:
            raise GeneratorError(f"{inputsPath}: '<주인>\\t<목록>' 꼴이 아닌 줄: {inputLine!r}")
        owner = ownerText or None
        listPath = Path(listPathText)
        try:
            listTypeLine = listPath.read_text(encoding="utf-8").splitlines()
        except OSError as exception:
            raise GeneratorError(f"{listPath}: 타입 목록을 읽지 못했다 ({exception!r}) — 리플렉션 단계가 먼저 돌아야 한다") from exception
        for typeLine in listTypeLine:
            if not typeLine.strip():
                continue
            fqn, separator, module = typeLine.partition("\t")
            if not separator or not fqn or not module:
                raise GeneratorError(f"{listPath}: '<FQN>\\t<모듈>' 꼴이 아닌 줄: {typeLine!r}")
            mapOwner.setdefault(fqn.rsplit(kScopeSeparator, 1)[-1], set()).add(owner)
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


def findProblems(packRoot: str, packFolder: Path, setModule: set[str], mapOwner: dict[str, set[str | None]], fix: str) -> list[str]:
    """팩 하나가 `setModule` 밖 모듈의 형식을 쓰는 자리. 엔진 형식(주인 None)은 늘 된다."""
    listProblem: list[str] = []
    for typeName, listFile in sorted(collectContentTypes(packFolder).items()):
        setOwner = mapOwner.get(typeName)
        if setOwner is None or None in setOwner or setOwner & setModule:
            continue
        listOwner = ", ".join(sorted(owner.removeprefix("game:") + " 게임" if owner.startswith("game:") else owner for owner in setOwner))
        listProblem.append(f"{packRoot}/{listFile[0]}{' 외 ' + str(len(listFile) - 1) + ' 파일' if len(listFile) > 1 else ''} 이 쓰는 {typeName}({listOwner})")
    if listProblem:
        listProblem.append(f"  → {fix}")
    return listProblem


def checkContentModules(projectRoot: Path, inputsPath: Path) -> int:
    """어긋남이 있으면 `GeneratorError`. 본 팩 수를 돌려줍니다.

    도메인마다 쓸 수 있는 모듈: `game/<팩>` 은 그 게임이 가진 것, `common/` 은 모든 게임이 가진 것(어느 게임이 쓸지 정적으로 알 수 없다 — 기믹 프리팹은
    코드가 이름으로 찾는다), `editor/` 는 엔진과 `EditorModule`, `engine/` 은 엔진만(모든 호스트 — 서버 · 도구도 읽는다).
    """
    sourceRoot = projectRoot / "Source"
    contentRoot = projectRoot / "Content"
    mapManifestByName = collectManifests(sourceRoot)
    mapOwner = readTypeOwners(inputsPath)

    listProblem: list[str] = []
    setEveryGame: set[str] | None = None
    listGameFolder = sorted(path.parent for path in (sourceRoot / "Games").glob("*/SWGame.module.json"))
    for gameFolder in listGameFolder:
        presetPath = projectRoot / kDirRuntimeGamePreset / f"{gameFolder.name}.json"
        try:
            packRoot = json.loads(presetPath.read_text(encoding="utf-8"))["_packRoot"]
        except (OSError, ValueError, KeyError) as exception:
            raise GeneratorError(f"{presetPath}: 게임 {gameFolder.name} 의 _packRoot 를 읽지 못했다 ({exception!r})") from exception
        packFolder = contentRoot / packRoot
        if not packFolder.is_dir():
            raise GeneratorError(f"{presetPath}: _packRoot '{packRoot}' 폴더가 없다")
        setModule = collectGameModules(gameFolder, readManifest(gameFolder / "SWGame.module.json"), mapManifestByName)
        setShared = setModule - {f"game:{gameFolder.name}"}
        setEveryGame = setShared if setEveryGame is None else setEveryGame & setShared
        listProblem += findProblems(packRoot, packFolder, setModule, mapOwner,
                                    f"Source/Games/{gameFolder.name}/SWGame.module.json 의 _listDependency 에 그 모듈을 더하거나 콘텐츠에서 빼세요")
    listProblem += findProblems("common", contentRoot / "common", setEveryGame or set(), mapOwner,
                                "common/ 은 모든 게임이 가진 모듈(" + ", ".join(sorted(setEveryGame or set())) + ")의 형식만 쓴다 — 그 게임의 팩으로 옮기세요")
    listProblem += findProblems("editor", contentRoot / "editor", {"EditorModule"}, mapOwner, "editor/ 는 엔진 · EditorModule 형식만 쓴다")
    listProblem += findProblems("engine", contentRoot / "engine", set(), mapOwner, "engine/ 은 엔진 형식만 쓴다(모든 호스트가 읽는다)")
    if listProblem:
        raise GeneratorError("콘텐츠가 그 도메인이 가질 수 없는 모듈의 형식을 쓴다:\n  " + "\n  ".join(listProblem))
    return len(listGameFolder)


def main(argv: Sequence[str] | None = None) -> int:
    def addArguments(parser) -> None:
        parser.add_argument("--inputs", type=Path, required=True, help="CMake 가 쓴 `<주인>\\t<ReflectedTypes.list>` 줄 파일")
        parser.add_argument("--stamp", type=Path, required=True, help="검사가 통과하면 쓰는 스탬프(빌드 단계의 산출물)")

    def generate(args) -> None:
        packCount = checkContentModules(getProjectRoot(), args.inputs)
        args.stamp.write_text(f"{packCount} game packs checked\n", encoding="utf-8")
        print(f"[{kTag}] {packCount} game packs · common · editor · engine: content uses only modules its domain has")
    return runGenerator(argv, tag=kTag, description="게임 콘텐츠가 쓰는 형식의 모듈 ⊂ 게임 매니페스트 의존", addArguments=addArguments,
                        generate=generate)


if __name__ == "__main__":
    sys.exit(main())
