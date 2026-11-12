#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/LayerIncludes.py

층 표(`Config/Rules/Layers/{Core,Engine,GameFramework,Editor}Layers.toml`)에서 아래 층 파일이 위 층 헤더를 include 하는지 본다.

**왜 린트인가.** 막는 것은 빌드다 — 층마다 부분을 짓고 위 층 헤더 자리에 `#error` 껍데기를 둔다. 그런데 껍데기는 그 부분을 컴파일할 때만
드러나서, 층 위반이 커밋을 지나 Debug 빌드에서야 멈췄다(아무도 include 하지 않는 헤더는 빌드도 보지 않는다). 같은 표로 커밋 때 직접 include 를 먼저 본다.
헤더마다 자기 층에서 검사되므로 직접 include 만 보아도 전이 include 가 덮인다 — 예외는 어느 층에서나 쓰는 에디터 `[prelude]` 헤더라, 그 헤더를 거친 include 는 따라간다.

파일의 층은 생성기 · CMake 와 같은 규칙이다(가장 긴 키가 이긴다):
- Core: `[tier]` · `[subtier]` 키. 단위 테스트(`/Tests/`)와 `Source/Core` 바로 아래 파일은 맨 위 티어 부분이 짓는다(헤더는 껍데기가 없다).
- Engine · GameFramework(`Runtime`): `[tier]` 키(파일 또는 폴더 접두어).
- Editor: 층 이름(`Common/<하위>` · `Common` · 최상위 폴더 · `<root>`)의 `[tier]`.

  py -3 -m Scripts lint layer-includes [--self-check]
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Mapping, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint.LintScope import LintScope, listRepoFiles, makeWholeScope  # noqa: E402

kExtensions = (".h", ".hpp", ".inl", ".cpp", ".c", ".xxx")
kSourceExtensions = (".cpp", ".c")
kInclude = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


@dataclass(frozen=True)
class LayerModule:
    """층 표 하나 — include 줄 머리(`Source/` 기준 폴더)와 파일 → 층 함수(`Source/<root>` 기준 경로, 없으면 None)."""
    _name: str
    _root: str
    _findTier: Callable[[str, bool], int | None]
    _setPrelude: frozenset[str]


def findLongestKey(mapTier: dict[str, int], relative: str) -> int | None:
    best: str | None = None
    for key in mapTier:
        if (relative == key or relative.startswith(key + "/")) and (best is None or len(key) > len(best)):
            best = key
    return mapTier[best] if best is not None else None


def makeCoreTier(table: dict) -> Callable[[str, bool], int | None]:
    mapTier = {**table["tier"], **table.get("subtier", {})}
    topTier = max(mapTier.values())

    def findTier(relative: str, isIncluder: bool) -> int | None:
        if "/" not in relative:
            return topTier if isIncluder else None   # 바로 아래 파일: 맨 위 부분이 짓고, 헤더는 껍데기가 없다
        if "/Tests/" in f"/{relative}":
            return topTier                           # 테스트는 맨 위 부분이 짓는다
        return findLongestKey(mapTier, relative)
    return findTier


def makeModuleTier(table: dict) -> Callable[[str, bool], int | None]:
    mapTier: dict[str, int] = table["tier"]
    return lambda relative, isIncluder: findLongestKey(mapTier, relative)


def getEditorLayerName(relative: str) -> str:
    listPart = relative.split("/")
    if len(listPart) == 1:
        return "<root>"
    if listPart[0] != "Common":
        return listPart[0]
    return "Common" if len(listPart) == 2 else f"Common/{listPart[1]}"


def makeEditorTier(table: dict) -> Callable[[str, bool], int | None]:
    mapTier: dict[str, int] = table["tier"]
    return lambda relative, isIncluder: mapTier.get(getEditorLayerName(relative))


def findProblems(module: LayerModule, mapSource: Mapping[str, str], listCheck: Sequence[str] | None = None) -> list[str]:
    """`mapSource` 는 `Source/<root>` 기준 경로 → 글이다. `listCheck`(없으면 모두)의 파일만 보고, 거쳐 가는 prelude 헤더는 `mapSource` 에서 찾는다."""
    head = module._root + "/"
    listProblem: list[str] = []
    for relative in sorted(mapSource) if listCheck is None else listCheck:
        text = mapSource[relative]
        tier = module._findTier(relative, True)
        if tier is None:
            continue
        listPending = [(include, None) for include in kInclude.findall(text)]
        setSeen: set[str] = set()
        while listPending:
            include, via = listPending.pop()
            if include in setSeen or not include.startswith(head):
                continue
            setSeen.add(include)
            target = include[len(head):]
            if target not in mapSource:
                continue
            if include in module._setPrelude:
                listPending.extend((nested, include) for nested in kInclude.findall(mapSource[target]))
                continue
            targetTier = module._findTier(target, False)
            if targetTier is not None and targetTier > tier:
                route = f" (`{via}` 를 거쳐)" if via else ""
                listProblem.append(f"Source/{module._root}/{relative}: {module._name} 층 {tier} 파일이 층 {targetTier} 헤더 `{include}` 를 include 한다{route} — 빌드의 #error 껍데기가 멈춘다. 필요한 것을 아래로 내리거나 의존을 뒤집는다")
    return listProblem


def readTable(projectRoot: Path, name: str) -> dict:
    return tomllib.loads((projectRoot / "Config" / "Rules" / "Layers" / f"{name}Layers.toml").read_text(encoding="utf-8"))


def listModules(projectRoot: Path) -> list[LayerModule]:
    editor = readTable(projectRoot, "Editor")
    return [
        LayerModule("Core", "Core", makeCoreTier(readTable(projectRoot, "Core")), frozenset()),
        LayerModule("Engine", "Engine", makeModuleTier(readTable(projectRoot, "Engine")), frozenset()),
        LayerModule("GameFramework", "GameFramework/Runtime", makeModuleTier(readTable(projectRoot, "GameFramework")), frozenset()),
        LayerModule("Editor", "Editor", makeEditorTier(editor), frozenset(editor.get("prelude", {}))),
    ]


class ModuleTexts(dict):
    """`Source/<root>` 기준 경로 → 글. 처음 찾을 때 읽는다 — 커밋 훅은 바뀐 파일과 그것이 거쳐 가는 prelude 헤더만 읽는다."""

    def __init__(self, moduleDir: Path, setRelative: set[str]) -> None:
        super().__init__()
        self._moduleDir = moduleDir
        self._setRelative = setRelative

    def __contains__(self, relative: object) -> bool:
        return relative in self._setRelative

    def __missing__(self, relative: str) -> str:
        text = (self._moduleDir / relative).read_text(encoding="utf-8", errors="replace")
        self[relative] = text
        return text


def checkLayerIncludes(projectRoot: Path, scope: LintScope) -> list[str]:
    listProblem: list[str] = []
    for module in listModules(projectRoot):
        head = f"Source/{module._root}/"
        mapSource = ModuleTexts(projectRoot / "Source" / module._root,
                                {path[len(head):] for path in listRepoFiles(projectRoot) if path.startswith(head) and path.endswith(kExtensions)})
        listCheck = sorted(path[len(head):] for path in scope.select((head,), kExtensions))
        listProblem.extend(findProblems(module, mapSource, listCheck))
    return listProblem


def runSelfCheck() -> None:
    engine = LayerModule("Engine", "Engine", makeModuleTier({"tier": {"Low": 0, "High": 2, "Low/Up.h": 3}}), frozenset())
    mapSource = {
        "Low/a.cpp": '#include "Engine/Low/b.h"\n#include "Core/x.h"\n',
        "Low/b.h": "",
        "High/c.h": '#include "Engine/Low/b.h"\n',
        "Low/Up.h": "",
    }
    assert findProblems(engine, mapSource) == [], "아래 · 같은 층 · 다른 모듈은 통과한다"
    upward = dict(mapSource, **{"Low/a.cpp": '#include "Engine/High/c.h"\n'})
    assert len(findProblems(engine, upward)) == 1, "위 층 include"
    fileKey = dict(mapSource, **{"Low/b.h": '#include "Engine/Low/Up.h"\n'})
    assert len(findProblems(engine, fileKey)) == 1, "더 긴 파일 키가 층을 정한다"

    core = LayerModule("Core", "Core", makeCoreTier({"tier": {"Math": 1, "Testing": 10}}), frozenset())
    coreSource = {"Math/Tests/TestX.cpp": '#include "Core/Testing/T.h"\n', "Testing/T.h": "", "pch.h": "", "Math/m.h": '#include "Core/pch.h"\n'}
    assert findProblems(core, coreSource) == [], "Core 테스트는 맨 위 부분, 바로 아래 헤더는 껍데기가 없다"

    editor = LayerModule("Editor", "Editor", makeEditorTier({"tier": {"Common": 0, "Common/Workspace": 2, "Panels": 6}}),
                         frozenset({"Editor/Common/Workspace/Leaf.h"}))
    editorSource = {"Common/u.cpp": '#include "Editor/Common/Workspace/Leaf.h"\n', "Common/Workspace/Leaf.h": "", "Common/Workspace/W.h": "", "Panels/P.h": ""}
    assert findProblems(editor, editorSource) == [], "prelude 헤더는 어느 층에서나"
    viaPrelude = dict(editorSource, **{"Common/Workspace/Leaf.h": '#include "Editor/Common/Workspace/W.h"\n'})
    listViaPrelude = findProblems(editor, viaPrelude)
    assert len(listViaPrelude) == 1 and "Leaf.h" in listViaPrelude[0], "prelude 를 거친 위 층 include"
    assert len(findProblems(editor, dict(editorSource, **{"Common/u.cpp": '#include "Editor/Panels/P.h"\n'}))) == 1, "에디터 위 층"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="아래 층 파일이 위 층 헤더를 include 하는지 본다(Core · Engine · GameFramework · Editor 층 표)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[layer-includes] self-check OK")
        return ExitCode.Passed
    listProblem = checkLayerIncludes(getProjectRoot(), makeWholeScope(getProjectRoot()))
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[layer-includes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
