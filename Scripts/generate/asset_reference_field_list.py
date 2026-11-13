#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/asset_reference_field_list.py

리플렉션 파서가 대상마다 쓴 경로 칸 목록(`AssetReferenceFields.list`, `<이름>\\t<대상>` 줄)을 저장소의 표 `Config/Dev/AssetReferenceFields.generated.json` 으로 모은다.

- 표를 저장소에 두는 까닭: 커밋 전 린트(`Scripts/lint/asset_references.py`)와 `move-asset` 은 빌드 없이 돈다.
- 표는 주인(`CONTENT_OWNER` — 엔진은 빈 글, 게임은 `game:<폴더>`)별이다. 한 구성은 게임 하나만 지으므로 이 구성이 짓는 주인의 줄만 갈아 끼우고 나머지는 그대로 둔다.
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


def read_built_owners(inputs_path: Path) -> dict[str, set[str]]:
    """이 구성이 짓는 주인 → 칸 이름들입니다. `inputs_path` 의 줄은 `<주인>\\t<목록>`(CMake), 목록의 줄은 `<이름>\\t<대상>`(파서)입니다. 목록이 없으면 예외입니다."""
    map_owner: dict[str, set[str]] = {}
    for input_line in inputs_path.read_text(encoding="utf-8").splitlines():
        if not input_line.strip():
            continue
        owner, separator, list_path = input_line.partition("\t")
        if not separator or not list_path:
            raise ValueError(f"{inputs_path}: '<주인>\\t<목록>' 꼴이 아닌 줄: {input_line!r}")
        set_field = map_owner.setdefault(owner, set())
        for field_line in Path(list_path).read_text(encoding="utf-8").splitlines():
            name, separator, target = field_line.partition("\t")
            if not separator or not name or not target:
                raise ValueError(f"{list_path}: '<이름>\\t<대상>' 꼴이 아닌 줄: {field_line!r}")
            set_field.add(name)
    return map_owner


def merge_owners(list_existing: list[dict], map_built: dict[str, set[str]]) -> list[dict]:
    """있던 표에 이 구성의 주인 줄을 갈아 끼웁니다. 주인 순으로 정렬합니다."""
    map_merged = {entry["owner"]: sorted(entry["listField"]) for entry in list_existing}
    for owner, set_field in map_built.items():
        map_merged[owner] = sorted(set_field)
    # 칸이 없는 주인은 적지 않는다 — 칸을 모두 잃은 주인도 이 구성이 지었으면 빈 줄로 갈아 끼워져 함께 빠진다.
    return [{"owner": owner, "listField": map_merged[owner]} for owner in sorted(map_merged) if map_merged[owner]]


def format_table(list_owner: list[dict]) -> str:
    return json.dumps({"listOwner": list_owner}, ensure_ascii=False, indent=2) + "\n"


def update_table(output_path: Path, map_built: dict[str, set[str]]) -> bool:
    """저장소의 표에 이 구성의 주인 줄을 잠금 안에서 갈아 끼웁니다. 바뀌었으면 True."""
    def merge_text(current: str) -> str:
        return format_table(merge_owners(json.loads(current)["listOwner"], map_built))
    return update_shared_text_file(output_path, merge_text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="파서가 쓴 경로 칸 목록을 저장소의 표로 모은다")
    parser.add_argument("--inputs", type=Path, required=True, help="CMake 가 쓴 `<주인>\\t<목록>` 줄 파일")
    parser.add_argument("--output", type=Path, required=True, help="저장소의 표")
    parser.add_argument("--stamp", type=Path, required=True, help="마치면 건드리는 파일(빌드 의존)")
    arguments = parser.parse_args(argv)

    update_table(arguments.output, read_built_owners(arguments.inputs))
    arguments.stamp.touch()
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
