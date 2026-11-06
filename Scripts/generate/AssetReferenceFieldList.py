#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/AssetReferenceFieldList.py

리플렉션 파서가 대상마다 쓴 경로 칸 목록(`AssetReferenceFields.list`, `<이름>\\t<대상>` 줄)을 저장소의 표 `Config/Dev/AssetReferenceFields.generated.json` 으로 모은다.

- 표를 저장소에 두는 까닭: 커밋 전 린트(`Scripts/lint/AssetReferences.py`)와 `move-asset` 은 빌드 없이 돈다.
- 표는 주인(`CONTENT_OWNER` — 엔진은 빈 글, 게임은 `game:<폴더>`)별이다. 한 구성은 게임 하나만 지으므로 이 구성이 짓는 주인의 줄만 갈아 끼우고 나머지는 그대로 둔다.
- 낡음은 `App --check-asset-refs` 가 리플렉션과 표를 대조해 잡는다. 내용이 같으면 쓰지 않는다(빌드가 저장소를 더럽히지 않게).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402


def readBuiltOwners(inputsPath: Path) -> dict[str, set[str]]:
    """이 구성이 짓는 주인 → 칸 이름들입니다. `inputsPath` 의 줄은 `<주인>\\t<목록>`(CMake), 목록의 줄은 `<이름>\\t<대상>`(파서)입니다. 목록이 없으면 예외입니다."""
    mapOwner: dict[str, set[str]] = {}
    for inputLine in inputsPath.read_text(encoding="utf-8").splitlines():
        if not inputLine.strip():
            continue
        owner, separator, listPath = inputLine.partition("\t")
        if not separator or not listPath:
            raise ValueError(f"{inputsPath}: '<주인>\\t<목록>' 꼴이 아닌 줄: {inputLine!r}")
        setField = mapOwner.setdefault(owner, set())
        for fieldLine in Path(listPath).read_text(encoding="utf-8").splitlines():
            name, separator, target = fieldLine.partition("\t")
            if not separator or not name or not target:
                raise ValueError(f"{listPath}: '<이름>\\t<대상>' 꼴이 아닌 줄: {fieldLine!r}")
            setField.add(name)
    return mapOwner


def mergeOwners(listExisting: list[dict], mapBuilt: dict[str, set[str]]) -> list[dict]:
    """있던 표에 이 구성의 주인 줄을 갈아 끼웁니다. 주인 순으로 정렬합니다."""
    mapMerged = {entry["owner"]: sorted(entry["listField"]) for entry in listExisting}
    for owner, setField in mapBuilt.items():
        mapMerged[owner] = sorted(setField)
    # 칸이 없는 주인은 적지 않는다 — 칸을 모두 잃은 주인도 이 구성이 지었으면 빈 줄로 갈아 끼워져 함께 빠진다.
    return [{"owner": owner, "listField": mapMerged[owner]} for owner in sorted(mapMerged) if mapMerged[owner]]


def formatTable(listOwner: list[dict]) -> str:
    return json.dumps({"listOwner": listOwner}, ensure_ascii=False, indent=2) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="파서가 쓴 경로 칸 목록을 저장소의 표로 모은다")
    parser.add_argument("--inputs", type=Path, required=True, help="CMake 가 쓴 `<주인>\\t<목록>` 줄 파일")
    parser.add_argument("--output", type=Path, required=True, help="저장소의 표")
    parser.add_argument("--stamp", type=Path, required=True, help="마치면 건드리는 파일(빌드 의존)")
    arguments = parser.parse_args(argv)

    existing = json.loads(arguments.output.read_text(encoding="utf-8"))["listOwner"]
    text = formatTable(mergeOwners(existing, readBuiltOwners(arguments.inputs)))
    if arguments.output.read_text(encoding="utf-8") != text:
        arguments.output.write_text(text, encoding="utf-8", newline="\n")
    arguments.stamp.touch()
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
