"""
파일 내용 지문의 캐시 — 빌드마다 도는 지문 생성기(`engine_abi_stamp.py` · `cook_code_fingerprint.py`)가 같이 쓴다.

그 생성기들은 헤더 하나만 바뀌어도 트리 전체(수천 파일)를 읽어 해시했다 — 고친 파일 수가 아니라 트리 크기에 비례하는 모양이다.
파일마다 (mtime, 크기) → 내용 SHA-1 을 빌드 트리의 JSON 에 두고, 둘 중 하나라도 다른 파일만 다시 읽는다. 지문은 경로와 파일 SHA-1 을
차례로 넣은 SHA-1 이라 캐시가 있든 없든 같은 값이다. 내용은 줄 끝을 LF 로 맞춰 해시한다(체크아웃마다 다른 CRLF 가 지문을 바꾸지 않게).
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Iterable

CACHE_VERSION = 1


class FileDigestCache:
    """`cachePath`(빌드 트리 안 JSON)에 파일별 지문을 둔다. 형식이 다르거나 깨진 캐시는 비운 채 시작한다(캐시일 뿐 정본이 아니다)."""

    def __init__(self, cache_path: Path) -> None:
        self._cache_path = cache_path
        self._map_entry: dict[str, list] = {}
        self._map_used: dict[str, list] = {}
        try:
            data = json.loads(cache_path.read_text(encoding="utf-8"))
            if data.get("version") == CACHE_VERSION and isinstance(data.get("files"), dict):
                self._map_entry = data["files"]
        except (OSError, ValueError, AttributeError):
            pass

    def file_digest(self, absolute_path: Path, key: str) -> str:
        """파일 내용(줄 끝 LF)의 SHA-1. (mtime, 크기)가 캐시와 같으면 읽지 않는다."""
        status = os.stat(absolute_path)
        entry = self._map_entry.get(key)
        if entry is None or entry[0] != status.st_mtime_ns or entry[1] != status.st_size:
            content = absolute_path.read_bytes().replace(b"\r\n", b"\n")
            entry = [status.st_mtime_ns, status.st_size, hashlib.sha1(content).hexdigest()]
        self._map_used[key] = entry
        return entry[2]

    def tree_digest(self, repository_root: Path, list_relative_path: Iterable[str]) -> bytes:
        """경로(저장소 기준, 정렬된 순서 그대로)와 파일 지문을 차례로 넣은 SHA-1(바이트)."""
        digest = hashlib.sha1()
        for relative_path in list_relative_path:
            digest.update(relative_path.encode("utf-8"))
            digest.update(b"\0")
            digest.update(self.file_digest(repository_root / relative_path, relative_path).encode("ascii"))
            digest.update(b"\0")
        return digest.digest()

    def save(self) -> None:
        """이번에 본 파일만 남겨 쓴다(지운 파일의 줄이 쌓이지 않게). 바뀌지 않았으면 쓰지 않는다. 임시 파일을 옮겨 써 반쯤 쓴 캐시가 남지 않는다."""
        if self._map_used == self._map_entry:
            return
        self._cache_path.parent.mkdir(parents=True, exist_ok=True)
        temporary_path = self._cache_path.with_suffix(self._cache_path.suffix + ".tmp")
        temporary_path.write_text(json.dumps({"version": CACHE_VERSION, "files": self._map_used}), encoding="utf-8")
        os.replace(temporary_path, self._cache_path)
