#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/asset_reference_field_list.py

리플렉션 파서가 대상마다 쓴 칸 목록(`AssetReferenceFields.list`, `<이름>\\t<대상>` 줄)을 저장소의 표 `Config/Dev/AssetReferenceFields.generated.json` 으로 모은다.

- 표를 저장소에 두는 까닭: 커밋 전 린트(`Scripts/lint/asset_references.py`)와 `move-asset` 은 빌드 없이 돈다.
- 표는 주인(`CONTENT_OWNER` — 엔진은 빈 글, 게임은 `game:<폴더>`)별이다. 한 구성은 게임 하나만 지으므로 이 구성이 짓는 주인의 줄만 갈아 끼우고 나머지는 그대로 둔다.
  저장소에서 지운 게임(`Source/Games/<폴더>` 없음)의 줄은 아무 구성도 다시 짓지 않으므로 여기서 뺀다.
- 낡음은 `App --check-asset-refs` 가 리플렉션과 표를 대조해 잡는다. 내용이 같으면 쓰지 않는다(빌드가 저장소를 더럽히지 않게).
- 여러 빌드 폴더가 동시에 갱신한다(구성 · 게임마다). 읽기 → 합치기 → 쓰기를 프로세스 사이 잠금 하나로 묶고 원자 교체로 쓴다(`common.shared_file_update`) —
  예전에는 둘이 같은 옛 표를 읽고 차례로 덮어 앞 빌드의 주인 줄을 잃었다. 회귀 검사: `Scripts/generate/tests/test_asset_reference_field_list.py`(CTest `Scripts.AssetReferenceFieldList`).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.exit_code import ExitCode  # noqa: E402
from common.shared_file_update import update_shared_text_file  # noqa: E402


GAME_OWNER_PREFIX = "game:"   # 게임 주인 `game:<폴더>`(CMake CONTENT_OWNER)


def read_built_owners(inputs_path: Path) -> dict[str, set[str]]:
    """이 구성이 짓는 주인 → 경로 칸 이름들입니다. `inputs_path` 의 줄은 `<주인>TAB<목록>`(CMake), 목록의 줄은 `<이름>TAB<대상>`(파서)입니다.
    목록이 없거나 줄 꼴이 틀리면 예외입니다 — 옛 파서의 세 열 줄을 짐작해 읽지 않는다(스탬프 머리가 바뀌어 다시 파싱된다)."""
    map_owner: dict[str, set[str]] = {}
    for input_line in inputs_path.read_text(encoding="utf-8").splitlines():
        if not input_line.strip():
            continue
        owner, separator, list_path = input_line.partition("\t")
        if not separator or not list_path:
            raise ValueError(f"{inputs_path}: '<주인>TAB<목록>' 꼴이 아닌 줄: {input_line!r}")
        set_field = map_owner.setdefault(owner, set())
        for field_line in Path(list_path).read_text(encoding="utf-8").splitlines():
            list_column = field_line.split("\t")
            if len(list_column) != 2 or not all(list_column):
                raise ValueError(f"{list_path}: '<이름>TAB<대상>' 꼴이 아닌 줄: {field_line!r}")
            set_field.add(list_column[0])
    return map_owner


def merge_owners(list_existing: list[dict], map_built: dict[str, set[str]]) -> list[dict]:
    """있던 표에 이 구성의 주인 줄을 갈아 끼웁니다(검사 · 옮기기가 읽는 `listField` 하나). 주인 순으로 정렬합니다. 칸이 없는 주인은 적지 않는다."""
    map_merged = {entry["owner"]: entry["listField"] for entry in list_existing} | {owner: sorted(set_field) for owner, set_field in map_built.items()}
    return [{"owner": owner, "listField": list_field} for owner, list_field in sorted(map_merged.items()) if list_field]


def drop_removed_games(list_owner: list[dict], set_game: set[str]) -> list[dict]:
    """지운 게임의 주인 줄을 뺍니다(@p set_game 은 저장소에 있는 게임 폴더 이름)."""
    return [entry for entry in list_owner if not entry["owner"].startswith(GAME_OWNER_PREFIX) or entry["owner"][len(GAME_OWNER_PREFIX):] in set_game]


def format_table(list_owner: list[dict]) -> str:
    return json.dumps({"listOwner": list_owner}, ensure_ascii=False, indent=2) + "\n"


def update_table(output_path: Path, map_built: dict[str, set[str]], set_game: set[str]) -> bool:
    """저장소의 표에 이 구성의 주인 줄을 잠금 안에서 갈아 끼우고 지운 게임의 줄을 뺍니다. 바뀌었으면 True."""
    def merge_text(current: str) -> str:
        return format_table(drop_removed_games(merge_owners(json.loads(current)["listOwner"], map_built), set_game))
    return update_shared_text_file(output_path, merge_text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="파서가 쓴 경로 칸 목록을 저장소의 표로 모은다")
    parser.add_argument("--inputs", type=Path, required=True, help="CMake 가 쓴 `<주인>\\t<목록>` 줄 파일")
    parser.add_argument("--output", type=Path, required=True, help="저장소의 표")
    parser.add_argument("--stamp", type=Path, required=True, help="마치면 건드리는 파일(빌드 의존)")
    arguments = parser.parse_args(argv)

    game_folder = Path(__file__).resolve().parents[2] / "Source" / "Games"
    update_table(arguments.output, read_built_owners(arguments.inputs), {path.name for path in game_folder.iterdir() if path.is_dir()})
    arguments.stamp.touch()
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
