#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunEditFile.py
@brief 파일의 한 곳을 정확히 바꿉니다 — 인라인 `python -c` · heredoc 편집이 반복해서 낸 사고(NUL · 탭이 실제 문자로 들어감, CRLF 가 LF 로 바뀜)를 형식으로 막습니다.

    py -3 -m Scripts edit-file <경로> --old-file <파일> --new-file <파일>   # 바꿀 글 · 새 글을 파일에서 읽는다(이스케이프 걱정 없음)
    py -3 -m Scripts edit-file --changes <JSON 파일>                        # [{"path": .., "old": .., "new": ..}, ..] 여러 곳을 한 번에

규칙(어기면 아무것도 쓰지 않고 종료 1):
- `old` 는 파일에 정확히 한 번 나와야 한다(0번 · 2번 이상은 오류).
- 원본의 줄 끝(CRLF 또는 LF)과 BOM 유무를 보존한다. `old` · `new` 는 LF 로 적어도 된다(원본이 CRLF 면 맞춘다). 원본이 CRLF 와 LF 를 섞어 쓰면 멈춘다.
- 결과에 NUL 이 새로 생기거나 `new` 에 NUL 외의 제어 문자가 있으면 오류다. 탭 · 줄바꿈은 허용한다.
- 저장소 안의 기존 파일만 고친다. 모든 변경을 메모리에서 먼저 검증한 뒤 한꺼번에 쓴다.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Paths import getProjectRoot  # noqa: E402

_kBom = "﻿"
_kAllowedControl = {"\t", "\n", "\r"}


class EditError(Exception):
    """검증 실패 — 메시지는 사용자에게 그대로 보인다."""


@dataclass(frozen=True)
class Change:
    """한 곳의 치환. `path` 는 저장소 루트 기준 또는 절대 경로, `old` · `new` 는 LF 로 정규화하기 전의 글."""
    path: str
    old: str
    new: str


def checkNewTextInternal(new: str, where: str) -> None:
    """`new` 에 NUL 이나 탭 · 줄바꿈 외의 제어 문자가 있으면 멈춘다."""
    for char in new:
        if ord(char) < 0x20 and char not in _kAllowedControl:
            raise EditError(f"{where}: new 에 제어 문자 U+{ord(char):04X} 가 있습니다(이스케이프가 실제 문자로 풀린 것일 수 있습니다)")


def replaceOnceInternal(original: str, old: str, new: str, where: str) -> str:
    """원본 글에서 `old` 를 정확히 한 번 `new` 로 바꾼다. 줄 끝은 원본을 따르고, BOM 은 건드리지 않는다."""
    if not old:
        raise EditError(f"{where}: old 가 비어 있습니다")
    bHasBom = original.startswith(_kBom)
    body = original[1:] if bHasBom else original
    bCrlf = "\r\n" in body
    if bCrlf and "\n" in body.replace("\r\n", ""):
        raise EditError(f"{where}: 원본이 CRLF 와 LF 를 섞어 씁니다 — 줄 끝을 먼저 통일하세요")
    if "\r" in body.replace("\r\n", ""):
        raise EditError(f"{where}: 원본에 단독 CR 이 있습니다")
    lfBody = body.replace("\r\n", "\n")
    lfOld = old.replace("\r\n", "\n")
    lfNew = new.replace("\r\n", "\n")
    count = lfBody.count(lfOld)
    if count != 1:
        raise EditError(f"{where}: old 가 {count}번 나옵니다(정확히 한 번이어야 합니다)")
    result = lfBody.replace(lfOld, lfNew, 1)
    if "\0" in result and "\0" not in lfBody:
        raise EditError(f"{where}: 결과에 NUL 이 생깁니다")
    if bCrlf:
        result = result.replace("\n", "\r\n")
    return (_kBom if bHasBom else "") + result


def lineCountInternal(text: str) -> int:
    return len(text.replace("\r\n", "\n").split("\n")) if text else 0


def resolveInsideRootInternal(pathText: str, root: Path) -> Path:
    path = Path(pathText)
    if not path.is_absolute():
        path = Path.cwd() / path
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        raise EditError(f"{pathText}: 저장소 밖의 경로입니다") from None
    if not resolved.is_file():
        raise EditError(f"{pathText}: 파일이 없습니다")
    return resolved


def planEdits(listChange: list[Change], root: Path) -> tuple[dict[Path, str], int, int]:
    """모든 변경을 메모리에서 적용해 (경로 → 새 글, 지운 줄 수, 더한 줄 수) 를 돌려준다. 하나라도 틀리면 EditError."""
    if not listChange:
        raise EditError("변경이 없습니다")
    mapText: dict[Path, str] = {}
    removed = added = 0
    for index, change in enumerate(listChange, start=1):
        where = f"변경 {index}({change.path})"
        checkNewTextInternal(change.new, where)
        path = resolveInsideRootInternal(change.path, root)
        if path not in mapText:
            data = path.read_bytes()
            if b"\0" in data:
                raise EditError(f"{where}: 원본이 이미 NUL 을 담고 있어 텍스트로 고치지 않습니다")
            try:
                mapText[path] = data.decode("utf-8")
            except UnicodeDecodeError:
                raise EditError(f"{where}: UTF-8 이 아닙니다") from None
        mapText[path] = replaceOnceInternal(mapText[path], change.old, change.new, where)
        removed += lineCountInternal(change.old)
        added += lineCountInternal(change.new)
    return mapText, removed, added


def writeAllInternal(mapText: dict[Path, str]) -> None:
    """같은 폴더의 임시 파일에 쓴 뒤 바꿔 넣는다(한 파일은 쓰다 끊기지 않는다)."""
    for path, text in mapText.items():
        handle, tempName = tempfile.mkstemp(dir=path.parent, prefix=path.name + ".", suffix=".tmp")
        try:
            with os.fdopen(handle, "wb") as stream:
                stream.write(text.encode("utf-8"))
            os.replace(tempName, path)
        except BaseException:
            Path(tempName).unlink(missing_ok=True)
            raise


def readTextFileInternal(path: Path) -> str:
    try:
        return path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise EditError(f"{path}: 읽지 못했습니다({error})") from None


def loadChangesInternal(jsonPath: Path) -> list[Change]:
    try:
        listRaw = json.loads(readTextFileInternal(jsonPath))
    except json.JSONDecodeError as error:
        raise EditError(f"{jsonPath}: JSON 이 틀립니다({error})") from None
    if not isinstance(listRaw, list):
        raise EditError(f"{jsonPath}: 최상위가 배열이어야 합니다")
    listChange: list[Change] = []
    for index, item in enumerate(listRaw, start=1):
        if not isinstance(item, dict) or set(item) != {"path", "old", "new"} or not all(isinstance(value, str) for value in item.values()):
            raise EditError(f"{jsonPath}: {index}번째 항목은 문자열 키 path · old · new 만 가져야 합니다")
        listChange.append(Change(item["path"], item["old"], item["new"]))
    return listChange


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="파일의 한 곳을 정확히 한 번 바꾼다(줄 끝 · BOM 보존, NUL 거부, 전부 검증한 뒤 한꺼번에 쓴다).")
    parser.add_argument("path", nargs="?", help="고칠 파일(--old-file · --new-file 과 함께)")
    parser.add_argument("--old-file", type=Path, help="바꿀 글이 든 파일")
    parser.add_argument("--new-file", type=Path, help="새 글이 든 파일")
    parser.add_argument("--changes", type=Path, help='[{"path": .., "old": .., "new": ..}] JSON 파일 — 여러 곳을 한 번에')
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)

    try:
        if args.changes:
            if args.path or args.old_file or args.new_file:
                raise EditError("--changes 는 경로 · --old-file · --new-file 과 함께 쓰지 않습니다")
            listChange = loadChangesInternal(args.changes)
        else:
            if not (args.path and args.old_file and args.new_file):
                raise EditError("경로 · --old-file · --new-file 을 모두 주거나 --changes 를 줍니다")
            listChange = [Change(args.path, readTextFileInternal(args.old_file), readTextFileInternal(args.new_file))]
        mapText, removed, added = planEdits(listChange, args.root or getProjectRoot())
        writeAllInternal(mapText)
    except EditError as error:
        print(f"[edit-file] 오류(아무것도 쓰지 않음): {error}", file=sys.stderr)
        return 1
    print(f"[edit-file] {len(mapText)}개 파일 {len(listChange)}곳 고침: -{removed} +{added} 줄")
    return 0


if __name__ == "__main__":
    sys.exit(main())
