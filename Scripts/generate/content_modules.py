#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/content_modules.py

콘텐츠(씬 · 프리팹)가 쓰는 리플렉션 형식의 모듈을 그 도메인이 가질 수 있는지 빌드 단계(`ContentModuleCheck`)가 본다 — `game/<팩>` 은 그 게임
매니페스트의 의존(따라 닿는 것 포함), `common/` 은 모든 게임이 가진 것, `editor/` 는 엔진 · EditorModule, `engine/` 은 엔진만.

**왜 하는가.** 키트는 게임 매니페스트의 의존으로만 링크된다(`sw_add_game_module`). 의존에 없는 키트의 컴포넌트를 쓰는 씬은 그 게임의 어떤 실행 ·
쿠킹 · 배포에서도 서지 못하는데, 예전에는 헤드리스 쿠킹이 "등록하는 모듈 없음" 으로 멈출 때야 드러났다(Empty 의 넷 씬 둘, 2026-10-31). 씬은 데이터라
형식으로 막을 수 없어서 콘텐츠와 매니페스트를 맞댄다. 상용 엔진: 언리얼은 콘텐츠가 쓰는 플러그인을 `.uproject` 에 켜야 하고, 켜지 않으면 에셋 로드가 실패한다.

  - 형식 → 모듈: 리플렉션 대상마다 파서가 쓴 `ReflectedTypes.list`(`<FQN>\t<모듈>`)와 CMake 가 적은 그 대상의 주인(`--inputs` 파일의
    `<주인>\t<목록>` 줄, 주인이 비면 엔진 · 실행 파일이라 늘 있다)만 본다 — 헤더를 글자로 훑지 않는다(예전 정규식은 주석 · 템플릿의 REFLECT 도 셌다).
    이름이 여러 모듈에 있으면 그 가운데 하나라도 게임이 가지면 된다.
  - 게임 → 팩: 프리셋 `Config/Game/<게임>.json` 의 `_packRoot`(엔진이 쓰는 값 그대로). 없으면 멈춘다.
  - 게임이 가진 모듈: 자기 폴더, `_listDependency` 를 따라 닿는 모듈, `_listModuleOverride` 로 켠 모듈.
  - 타입 목록이 바뀔 때(또는 다시 구성할 때) 돈다 — 씬마다 의존을 걸면 저장할 때마다 돈다.

  python Scripts/generate/content_modules.py --inputs <ContentModuleInputs.list> --stamp <ContentModules.stamp>
"""

from __future__ import annotations

import json
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.paths import get_project_root  # noqa: E402
from common.project_constants import DIR_RUNTIME_GAME_PRESET  # noqa: E402
from generate.generator_output import GeneratorError, run_generator  # noqa: E402

TAG = "ContentModules"
CONTENT_SUFFIX = (".scene.xml", ".prefab.xml")
SCOPE_SEPARATOR = "::"


def read_manifest(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exception:
        raise GeneratorError(f"{path}: 매니페스트를 읽지 못했다 ({exception!r})") from exception


def collect_manifests(source_root: Path) -> dict[str, dict]:
    """게임 아닌 모듈 이름 → 매니페스트(게임 의존을 따라가는 데 쓴다)."""
    map_manifest_by_name: dict[str, dict] = {}
    for manifest_path in source_root.rglob("*.module.json"):
        manifest = read_manifest(manifest_path)
        if manifest.get("_kind") != "Game":
            map_manifest_by_name[manifest["_name"]] = manifest
    return map_manifest_by_name


def read_type_owners(inputs_path: Path) -> dict[str, set[str | None]]:
    """리플렉션 형식의 짧은 이름(씬 요소 이름) → 주인 키 집합. 주인 None 은 늘 있는 것(엔진 · 실행 파일)이다.

    `inputs_path` 의 줄은 `<주인>\t<ReflectedTypes.list>`(CMake `sw_add_content_module_check`), 목록의 줄은 `<FQN>\t<모듈>`(파서)이다.
    목록 파일이 없거나 줄 꼴이 틀리면 멈춘다 — 빠진 목록의 타입을 "엔진 것" 으로 보면 검사가 조용히 통과한다.
    """
    map_owner: dict[str, set[str | None]] = {}
    try:
        list_input_line = inputs_path.read_text(encoding="utf-8").splitlines()
    except OSError as exception:
        raise GeneratorError(f"{inputs_path}: 입력 목록을 읽지 못했다 ({exception!r})") from exception
    for input_line in list_input_line:
        if not input_line.strip():
            continue
        owner_text, separator, list_path_text = input_line.partition("\t")
        if not separator or not list_path_text:
            raise GeneratorError(f"{inputs_path}: '<주인>\\t<목록>' 꼴이 아닌 줄: {input_line!r}")
        owner = owner_text or None
        list_path = Path(list_path_text)
        try:
            list_type_line = list_path.read_text(encoding="utf-8").splitlines()
        except OSError as exception:
            raise GeneratorError(f"{list_path}: 타입 목록을 읽지 못했다 ({exception!r}) — 리플렉션 단계가 먼저 돌아야 한다") from exception
        for type_line in list_type_line:
            if not type_line.strip():
                continue
            fqn, separator, module = type_line.partition("\t")
            if not separator or not fqn or not module:
                raise GeneratorError(f"{list_path}: '<FQN>\\t<모듈>' 꼴이 아닌 줄: {type_line!r}")
            map_owner.setdefault(fqn.rsplit(SCOPE_SEPARATOR, 1)[-1], set()).add(owner)
    return map_owner


def collect_game_modules(game_folder: Path, manifest: dict, map_manifest_by_name: dict[str, dict]) -> set[str]:
    """게임이 가진 모듈 키 — 자기 폴더, 의존을 따라 닿는 것, 오버라이드로 켠 것."""
    list_pending = [entry["_name"] for entry in manifest.get("_listDependency", [])]
    list_pending += [entry["_name"] for entry in manifest.get("_listModuleOverride", []) if entry.get("_bEnabled")]
    set_reached: set[str] = {f"game:{game_folder.name}"}
    while list_pending:
        name = list_pending.pop()
        if name in set_reached:
            continue
        set_reached.add(name)
        dependency = map_manifest_by_name.get(name)
        if dependency is None:
            raise GeneratorError(f"{game_folder.name}: 의존 '{name}' 의 매니페스트가 없다")
        list_pending += [entry["_name"] for entry in dependency.get("_listDependency", [])]
    return set_reached


def collect_content_types(pack_folder: Path) -> dict[str, list[str]]:
    """팩의 씬 · 프리팹이 쓰는 요소 이름 → 쓰는 파일(팩 기준)."""
    map_use: dict[str, list[str]] = {}
    for path in sorted(pack_folder.rglob("*.xml")):
        if not path.name.endswith(CONTENT_SUFFIX):
            continue
        try:
            root = ElementTree.parse(path).getroot()
        except ElementTree.ParseError as exception:
            raise GeneratorError(f"{path}: XML 을 읽지 못했다 ({exception})") from exception
        relative = path.relative_to(pack_folder).as_posix()
        for tag in {element.tag for element in root.iter()}:
            map_use.setdefault(tag, []).append(relative)
    return map_use


def find_problems(pack_root: str, pack_folder: Path, set_module: set[str], map_owner: dict[str, set[str | None]], fix: str) -> list[str]:
    """팩 하나가 `set_module` 밖 모듈의 형식을 쓰는 자리. 엔진 형식(주인 None)은 늘 된다."""
    list_problem: list[str] = []
    for type_name, list_file in sorted(collect_content_types(pack_folder).items()):
        set_owner = map_owner.get(type_name)
        if set_owner is None or None in set_owner or set_owner & set_module:
            continue
        list_owner = ", ".join(sorted(owner.removeprefix("game:") + " 게임" if owner.startswith("game:") else owner for owner in set_owner))
        list_problem.append(f"{pack_root}/{list_file[0]}{' 외 ' + str(len(list_file) - 1) + ' 파일' if len(list_file) > 1 else ''} 이 쓰는 {type_name}({list_owner})")
    if list_problem:
        list_problem.append(f"  → {fix}")
    return list_problem


def check_content_modules(project_root: Path, inputs_path: Path) -> int:
    """어긋남이 있으면 `GeneratorError`. 본 팩 수를 돌려줍니다.

    도메인마다 쓸 수 있는 모듈: `game/<팩>` 은 그 게임이 가진 것, `common/` 은 모든 게임이 가진 것(어느 게임이 쓸지 정적으로 알 수 없다 — 기믹 프리팹은
    코드가 이름으로 찾는다), `editor/` 는 엔진과 `EditorModule`, `engine/` 은 엔진만(모든 호스트 — 서버 · 도구도 읽는다).
    """
    source_root = project_root / "Source"
    content_root = project_root / "Content"
    map_manifest_by_name = collect_manifests(source_root)
    map_owner = read_type_owners(inputs_path)

    list_problem: list[str] = []
    set_every_game: set[str] | None = None
    list_game_folder = sorted(path.parent for path in (source_root / "Games").glob("*/SWGame.module.json"))
    for game_folder in list_game_folder:
        preset_path = project_root / DIR_RUNTIME_GAME_PRESET / f"{game_folder.name}.json"
        try:
            pack_root = json.loads(preset_path.read_text(encoding="utf-8"))["_packRoot"]
        except (OSError, ValueError, KeyError) as exception:
            raise GeneratorError(f"{preset_path}: 게임 {game_folder.name} 의 _packRoot 를 읽지 못했다 ({exception!r})") from exception
        pack_folder = content_root / pack_root
        if not pack_folder.is_dir():
            raise GeneratorError(f"{preset_path}: _packRoot '{pack_root}' 폴더가 없다")
        set_module = collect_game_modules(game_folder, read_manifest(game_folder / "SWGame.module.json"), map_manifest_by_name)
        set_shared = set_module - {f"game:{game_folder.name}"}
        set_every_game = set_shared if set_every_game is None else set_every_game & set_shared
        list_problem += find_problems(pack_root, pack_folder, set_module, map_owner,
                                    f"Source/Games/{game_folder.name}/SWGame.module.json 의 _listDependency 에 그 모듈을 더하거나 콘텐츠에서 빼세요")
    list_problem += find_problems("common", content_root / "common", set_every_game or set(), map_owner,
                                "common/ 은 모든 게임이 가진 모듈(" + ", ".join(sorted(set_every_game or set())) + ")의 형식만 쓴다 — 그 게임의 팩으로 옮기세요")
    list_problem += find_problems("editor", content_root / "editor", {"EditorModule"}, map_owner, "editor/ 는 엔진 · EditorModule 형식만 쓴다")
    list_problem += find_problems("engine", content_root / "engine", set(), map_owner, "engine/ 은 엔진 형식만 쓴다(모든 호스트가 읽는다)")
    if list_problem:
        raise GeneratorError("콘텐츠가 그 도메인이 가질 수 없는 모듈의 형식을 쓴다:\n  " + "\n  ".join(list_problem))
    return len(list_game_folder)


def main(argv: Sequence[str] | None = None) -> int:
    def add_arguments(parser) -> None:
        parser.add_argument("--inputs", type=Path, required=True, help="CMake 가 쓴 `<주인>\\t<ReflectedTypes.list>` 줄 파일")
        parser.add_argument("--stamp", type=Path, required=True, help="검사가 통과하면 쓰는 스탬프(빌드 단계의 산출물)")

    def generate(args) -> None:
        pack_count = check_content_modules(get_project_root(), args.inputs)
        args.stamp.write_text(f"{pack_count} game packs checked\n", encoding="utf-8")
        print(f"[{TAG}] {pack_count} game packs · common · editor · engine: content uses only modules its domain has")
    return run_generator(argv, tag=TAG, description="게임 콘텐츠가 쓰는 형식의 모듈 ⊂ 게임 매니페스트 의존", add_arguments=add_arguments,
                        generate=generate)


if __name__ == "__main__":
    sys.exit(main())
