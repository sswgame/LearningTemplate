#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/layer_includes.py

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

from common.exit_code import ExitCode  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from lint.lint_scope import LintScope, list_repo_files, make_whole_scope, read_text  # noqa: E402

EXTENSIONS = (".h", ".hpp", ".inl", ".cpp", ".c", ".xxx")
SOURCE_EXTENSIONS = (".cpp", ".c")
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


@dataclass(frozen=True)
class LayerModule:
    """층 표 하나 — include 줄 머리(`Source/` 기준 폴더)와 파일 → 층 함수(`Source/<root>` 기준 경로, 없으면 None)."""
    _name: str
    _root: str
    _find_tier: Callable[[str, bool], int | None]
    _set_prelude: frozenset[str]


def find_longest_key(map_tier: dict[str, int], relative: str) -> int | None:
    best: str | None = None
    for key in map_tier:
        if (relative == key or relative.startswith(key + "/")) and (best is None or len(key) > len(best)):
            best = key
    return map_tier[best] if best is not None else None


def make_core_tier(table: dict) -> Callable[[str, bool], int | None]:
    map_tier = {**table["tier"], **table.get("subtier", {})}
    top_tier = max(map_tier.values())

    def find_tier(relative: str, is_includer: bool) -> int | None:
        if "/" not in relative:
            return top_tier if is_includer else None   # 바로 아래 파일: 맨 위 부분이 짓고, 헤더는 껍데기가 없다
        if "/Tests/" in f"/{relative}":
            return top_tier                           # 테스트는 맨 위 부분이 짓는다
        return find_longest_key(map_tier, relative)
    return find_tier


def make_module_tier(table: dict) -> Callable[[str, bool], int | None]:
    map_tier: dict[str, int] = table["tier"]
    return lambda relative, is_includer: find_longest_key(map_tier, relative)


def get_editor_layer_name(relative: str) -> str:
    list_part = relative.split("/")
    if len(list_part) == 1:
        return "<root>"
    if list_part[0] != "Common":
        return list_part[0]
    return "Common" if len(list_part) == 2 else f"Common/{list_part[1]}"


def make_editor_tier(table: dict) -> Callable[[str, bool], int | None]:
    map_tier: dict[str, int] = table["tier"]
    return lambda relative, is_includer: map_tier.get(get_editor_layer_name(relative))


def find_problems(module: LayerModule, map_source: Mapping[str, str], list_check: Sequence[str] | None = None) -> list[str]:
    """`mapSource` 는 `Source/<root>` 기준 경로 → 글이다. `list_check`(없으면 모두)의 파일만 보고, 거쳐 가는 prelude 헤더는 `mapSource` 에서 찾는다."""
    head = module._root + "/"
    list_problem: list[str] = []
    for relative in sorted(map_source) if list_check is None else list_check:
        text = map_source[relative]
        tier = module._find_tier(relative, True)
        if tier is None:
            continue
        list_pending = [(include, None) for include in INCLUDE.findall(text)]
        set_seen: set[str] = set()
        while list_pending:
            include, via = list_pending.pop()
            if include in set_seen or not include.startswith(head):
                continue
            set_seen.add(include)
            target = include[len(head):]
            if target not in map_source:
                continue
            if include in module._set_prelude:
                list_pending.extend((nested, include) for nested in INCLUDE.findall(map_source[target]))
                continue
            target_tier = module._find_tier(target, False)
            if target_tier is not None and target_tier > tier:
                route = f" (`{via}` 를 거쳐)" if via else ""
                list_problem.append(f"Source/{module._root}/{relative}: {module._name} 층 {tier} 파일이 층 {target_tier} 헤더 `{include}` 를 include 한다{route} — 빌드의 #error 껍데기가 멈춘다. 필요한 것을 아래로 내리거나 의존을 뒤집는다")
    return list_problem


def read_table(project_root: Path, name: str) -> dict:
    return tomllib.loads((project_root / "Config" / "Rules" / "Layers" / f"{name}Layers.toml").read_text(encoding="utf-8"))


def list_modules(project_root: Path) -> list[LayerModule]:
    editor = read_table(project_root, "Editor")
    return [
        LayerModule("Core", "Core", make_core_tier(read_table(project_root, "Core")), frozenset()),
        LayerModule("Engine", "Engine", make_module_tier(read_table(project_root, "Engine")), frozenset()),
        LayerModule("GameFramework", "GameFramework/Runtime", make_module_tier(read_table(project_root, "GameFramework")), frozenset()),
        LayerModule("Editor", "Editor", make_editor_tier(editor), frozenset(editor.get("prelude", {}))),
    ]


class ModuleTexts(dict):
    """`Source/<root>` 기준 경로 → 글. 처음 찾을 때 읽는다 — 커밋 훅은 바뀐 파일과 그것이 거쳐 가는 prelude 헤더만 읽는다."""

    def __init__(self, module_dir: Path, set_relative: set[str]) -> None:
        super().__init__()
        self._module_dir = module_dir
        self._set_relative = set_relative

    def __contains__(self, relative: object) -> bool:
        return relative in self._set_relative

    def __missing__(self, relative: str) -> str:
        text = read_text(self._module_dir / relative, errors="replace")
        if text is None:
            raise FileNotFoundError(self._module_dir / relative)   # git 이 아는 파일만 찾는다 — 예전 read_text 처럼 멈춘다
        self[relative] = text
        return text


def check_layer_includes(project_root: Path, scope: LintScope) -> list[str]:
    list_problem: list[str] = []
    for module in list_modules(project_root):
        head = f"Source/{module._root}/"
        map_source = ModuleTexts(project_root / "Source" / module._root,
                                {path[len(head):] for path in list_repo_files(project_root) if path.startswith(head) and path.endswith(EXTENSIONS)})
        list_check = sorted(path[len(head):] for path in scope.select((head,), EXTENSIONS))
        list_problem.extend(find_problems(module, map_source, list_check))
    return list_problem


def run_self_check() -> None:
    engine = LayerModule("Engine", "Engine", make_module_tier({"tier": {"Low": 0, "High": 2, "Low/Up.h": 3}}), frozenset())
    map_source = {
        "Low/a.cpp": '#include "Engine/Low/b.h"\n#include "Core/x.h"\n',
        "Low/b.h": "",
        "High/c.h": '#include "Engine/Low/b.h"\n',
        "Low/Up.h": "",
    }
    assert find_problems(engine, map_source) == [], "아래 · 같은 층 · 다른 모듈은 통과한다"
    upward = dict(map_source, **{"Low/a.cpp": '#include "Engine/High/c.h"\n'})
    assert len(find_problems(engine, upward)) == 1, "위 층 include"
    file_key = dict(map_source, **{"Low/b.h": '#include "Engine/Low/Up.h"\n'})
    assert len(find_problems(engine, file_key)) == 1, "더 긴 파일 키가 층을 정한다"

    core = LayerModule("Core", "Core", make_core_tier({"tier": {"Math": 1, "Testing": 10}}), frozenset())
    core_source = {"Math/Tests/TestX.cpp": '#include "Core/Testing/T.h"\n', "Testing/T.h": "", "pch.h": "", "Math/m.h": '#include "Core/pch.h"\n'}
    assert find_problems(core, core_source) == [], "Core 테스트는 맨 위 부분, 바로 아래 헤더는 껍데기가 없다"

    editor = LayerModule("Editor", "Editor", make_editor_tier({"tier": {"Common": 0, "Common/Workspace": 2, "Panels": 6}}),
                         frozenset({"Editor/Common/Workspace/Leaf.h"}))
    editor_source = {"Common/u.cpp": '#include "Editor/Common/Workspace/Leaf.h"\n', "Common/Workspace/Leaf.h": "", "Common/Workspace/W.h": "", "Panels/P.h": ""}
    assert find_problems(editor, editor_source) == [], "prelude 헤더는 어느 층에서나"
    via_prelude = dict(editor_source, **{"Common/Workspace/Leaf.h": '#include "Editor/Common/Workspace/W.h"\n'})
    list_via_prelude = find_problems(editor, via_prelude)
    assert len(list_via_prelude) == 1 and "Leaf.h" in list_via_prelude[0], "prelude 를 거친 위 층 include"
    assert len(find_problems(editor, dict(editor_source, **{"Common/u.cpp": '#include "Editor/Panels/P.h"\n'}))) == 1, "에디터 위 층"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="아래 층 파일이 위 층 헤더를 include 하는지 본다(Core · Engine · GameFramework · Editor 층 표)")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        run_self_check()
        print("[layer-includes] self-check OK")
        return ExitCode.PASSED
    list_problem = check_layer_includes(get_project_root(), make_whole_scope(get_project_root()))
    for problem in list_problem:
        print(f"  - {problem}")
    print(f"[layer-includes] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
    return ExitCode.VIOLATION if list_problem else ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
