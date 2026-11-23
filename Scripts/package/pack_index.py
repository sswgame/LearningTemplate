#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/package/pack_index.py

팩(`.pack`)의 항목 경로 목록을 읽는다 — 배포 확인(`shipping-check`)이 "굽은 팩에 이것이 들었는가" 를 본다.
레이아웃의 원본은 `Source/Engine/Content/Pack/PackTypes.h` 의 `PackHeader`(파일 끝 96 바이트)다. 페이로드는 풀지 않는다(경로 목록은 압축하지 않는다).
"""

from __future__ import annotations

import struct
from pathlib import Path

#: `PackHeader` — 매직 · 판 · DLC · 압축 · 암호화 · 섹터 · 플래그 · 파일 수 · 색인 자리와 크기 · 경로 목록 자리와 크기 · 페이로드 · 블록 표 · 블록 해시 · 블록 수 · 블록 크기 · 예약.
HEADER = struct.Struct("<4sIIBBHHIQQQQQQQII10s")
MAGIC = b"SWPK"
FLAG_HAS_STRING_POOL = 1 << 0   # PackFlag::HasStringPool
FLAG_ENCRYPTED = 1 << 2         # PackFlag::Encrypted


class PackIndexError(Exception):
    """팩의 경로 목록을 읽지 못했다. 메시지가 이유다."""


def read_pack_paths(path: Path) -> set[str]:
    """팩 @p path 의 항목 경로(도메인 제외, 소문자)를 읽는다. 팩이 아니거나 경로 목록이 없거나 암호화됐으면 `PackIndexError`."""
    size = path.stat().st_size if path.is_file() else 0
    if size < HEADER.size:
        raise PackIndexError(f"{path} is missing or smaller than a pack header")
    with path.open("rb") as file:
        file.seek(size - HEADER.size)
        fields = HEADER.unpack(file.read(HEADER.size))
        magic, flags, pool_offset, pool_size = fields[0], fields[6], fields[10], fields[11]
        if magic != MAGIC:
            raise PackIndexError(f"{path} has no pack header at its end (magic {magic!r})")
        if flags & FLAG_HAS_STRING_POOL == 0 or flags & FLAG_ENCRYPTED != 0 or pool_offset + pool_size > size:
            raise PackIndexError(f"{path} has no readable entry path list (flags {flags:#x})")
        file.seek(pool_offset)
        return {name.decode("utf-8").lower() for name in file.read(pool_size).split(b"\0") if name}
