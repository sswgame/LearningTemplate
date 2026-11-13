#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/index_cache.py

여러 파일의 관계를 보는 규칙(층 순환 · 참조 0 · 호출 그래프 · 생성 표 · 에셋 역참조)의 저장소 전체 색인 — 파일마다 뽑은 사실을 내용 해시를 키로
두고, 해시가 바뀐 파일만 다시 뽑는다. 판정은 늘 저장소 전체 사실로 내리므로 커밋 훅과 `--all` 의 결과가 같다. 언리얼 UBT 의 makefile 캐시(바뀐
것만 다시 훑는다)와 같은 방식이다.

저장은 `Saved/Lint/<이름>/` 한 폴더다 — 커밋 훅이 스테이지 파일 수에 비례해 읽게 나눈다:
- `manifest.json`: 판(뽑는 법) · 조각마다 (경로, 내용 키) 목록의 해시 · 요약(`contribute` 토큰의 수). 늘 읽는다 — 지금 키로 낸 조각 해시와 다른 조각만 연다.
- `shard-<xx>.json`: 사실 조각(`shard_of(경로)` 의 sha1 앞 두 자리). 바뀐 파일 · 지운 파일 · 물은 파일(`want`)의 조각만 읽는다.
- 요약: 판정에 저장소 전체 합이 필요한 규칙은 사실 대신 파일마다의 토큰(`contribute`)을 접은 수를 읽는다. 사실이 바뀐 파일만큼 옛 토큰을 빼고 새 토큰을
  더한다(옛 사실은 그 파일의 조각에 있다). 사실 전체를 한 번에 읽는 길은 없다 — 호출 그래프처럼 접기 어려운 판정도 요약 + 증분 판정으로 한다(shader-kill-order).

- 키는 git 이 이미 아는 해시다: 작업 트리가 인덱스와 같은 파일은 인덱스 블롭 id(`git ls-files -s`), 다른 파일 · 새 파일만 읽어 sha1 을 낸다.
- 규칙 모듈과 그것이 import 하는 모듈 · 뽑기가 읽는 표 · 이 파일이 바뀌면(뽑는 법이 바뀌었다) 캐시를 통째로 버린다 — 판 번호를 손으로 올리지 않는다.
- 캐시는 파생물이다. 없거나 깨지면(조각 해시가 manifest 와 다르다 — 쓰다 멈췄다) 처음부터 다시 뽑는다(정답은 늘 소스다).
"""

from __future__ import annotations

import collections
import functools
import hashlib
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Sequence

from lint.lint_scope import read_bytes, read_text, run_git_lines
from lint.module_imports import SCRIPTS_DIR, list_imported_module_files

CACHE_FOLDER = "Saved/Lint"


@functools.cache
def get_index_keys(project_root: Path) -> dict[str, str]:
    """인덱스(스테이지)의 경로 → 블롭 id."""
    map_key: dict[str, str] = {}
    for entry in run_git_lines(project_root, ["ls-files", "-s"]):
        head, _, path = entry.partition("\t")
        map_key[path] = head.split()[1]
    return map_key


@functools.cache
def get_dirty_files(project_root: Path) -> frozenset[str]:
    """작업 트리가 인덱스와 다른 파일과 새 파일 — 이 파일만 내용을 읽어 키를 낸다."""
    return frozenset(run_git_lines(project_root, ["diff", "--name-only"])) | frozenset(run_git_lines(project_root, ["ls-files", "--others", "--exclude-standard"]))


@functools.cache
def get_working_tree_key(project_root: Path, relative: str) -> str | None:
    """작업 트리 파일의 내용 키(없으면 None). 인덱스와 같은 파일은 블롭 id, 다르거나 새 파일은 읽어서 낸 sha1 — 물은 파일만 읽는다(새 파일이 많아도 모두 읽지 않는다)."""
    if relative not in get_dirty_files(project_root):
        return get_index_keys(project_root).get(relative)
    try:
        data = read_bytes(project_root / relative)
    except OSError:
        return None
    return None if data is None else "w" + hashlib.sha1(data).hexdigest()


def read_working_tree(project_root: Path, list_relative: list[str], errors: str) -> dict[str, str | None]:
    """못 읽는 파일은 None — 규칙이 저장소를 걸을 때처럼 건너뛴다."""
    return {relative: read_text(project_root / relative, errors) for relative in list_relative}


def compute_version(list_version_file: Iterable[str]) -> str:
    """뽑는 법을 정하는 파일들의 해시 — Scripts 의 모듈은 그것이 import 하는 모듈까지 넣는다(도우미 모듈이 바뀌어도 다시 뽑는다)."""
    set_path: set[Path] = {Path(__file__).resolve()}
    for file in list_version_file:
        path = Path(file).resolve()
        if path.suffix == ".py" and SCRIPTS_DIR in path.parents:
            module_name = path.relative_to(SCRIPTS_DIR).with_suffix("").as_posix().replace("/", ".")
            set_path.update(SCRIPTS_DIR.parent / relative for relative in list_imported_module_files(module_name))
        else:
            set_path.add(path)
    digest = hashlib.sha1()
    for path in sorted(set_path):
        digest.update(path.read_bytes())
    return digest.hexdigest()



def get_path_shard(relative: str) -> str:
    return relative


@dataclass
class Facts:
    map_fact: dict[str, Any]                    # 물은 경로(`want`)의 사실, `listRelative` 순
    summary: collections.Counter[str]          # `contribute` 토큰의 수(0 은 없다)


class CacheBroken(Exception):
    """조각과 manifest 가 어긋났다 — 처음부터 다시 뽑는다."""


def get_shard_name(key: str) -> str:
    return "shard-" + hashlib.sha1(key.encode("utf-8")).hexdigest()[:2] + ".json"


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
    os.replace(temporary, path)   # 커밋 훅 둘이 겹쳐도 반쯤 쓴 파일을 읽지 않는다


def open_facts(project_root: Path, name: str, list_version_file: Sequence[str], list_relative: Iterable[str], extract: Callable[[str, str], Any], *,
              errors: str = "strict", map_key: dict[str, str] | None = None,
              read_texts: Callable[[list[str]], dict[str, str | None]] | None = None,
              want: Iterable[str] = (),
              contribute: Callable[[str, Any], Iterable[str]] | None = None,
              shard_of: Callable[[str], str] = get_path_shard) -> Facts:
    """
    `listRelative` 의 파일마다 `extract(경로, 글)` 의 결과(JSON 으로 쓸 수 있는 값)를 색인하고, `want` 의 사실과 요약을 돌려준다. 못 읽은 파일은 빠진다.

    - `list_version_file`: 뽑는 법을 정하는 파일(부르는 규칙 모듈 `__file__`, 뽑기가 읽는 표) — 하나라도 바뀌면 다시 뽑는다.
    - 기본은 작업 트리 내용이고, 인덱스 내용을 보려면 `mapKey` · `read_texts` 를 함께 넘긴다.
    - `want`: 사실을 돌려받을 경로(그 조각만 읽는다). 저장소 전체가 필요한 판정은 요약(`contribute`)으로 낸다.
    - `contribute(경로, 사실)`: 요약에 더할 토큰 — 판정이 이 수만으로 나면 사실 조각을 읽지 않는다.
    - `shard_of(경로)`: 조각을 고르는 열쇠 — 함께 묻는 파일(같은 폴더 · 같은 이름 짝)이 한 조각에 들게 한다.
    """
    try:
        return _open_facts(project_root, name, list_version_file, list(list_relative), extract, errors, map_key, read_texts, want, contribute, shard_of, False)
    except CacheBroken:
        return _open_facts(project_root, name, list_version_file, list(list_relative), extract, errors, map_key, read_texts, want, contribute, shard_of, True)


def digest_shard(list_path_key: Iterable[tuple[str, str]]) -> str:
    """조각 하나의 (경로, 내용 키) 목록의 해시 — manifest 는 조각마다 이것만 두어, 바뀐 조각만 열어 본다."""
    return hashlib.sha1("\n".join(f"{path}\t{key}" for path, key in sorted(list_path_key)).encode("utf-8")).hexdigest()


def _open_facts(project_root: Path, name: str, list_version_file: Sequence[str], list_relative: list[str], extract: Callable[[str, str], Any],
                      errors: str, map_key: dict[str, str] | None, read_texts: Callable[[list[str]], dict[str, str | None]] | None,
                      want: Iterable[str], contribute: Callable[[str, Any], Iterable[str]] | None,
                      shard_of: Callable[[str], str], reset: bool) -> Facts:
    keys = map_key if map_key is not None else {relative: key for relative in list_relative if (key := get_working_tree_key(project_root, relative)) is not None}
    read = read_texts if read_texts is not None else functools.partial(read_working_tree, project_root, errors=errors)
    folder = project_root / CACHE_FOLDER / name
    manifest_path = folder / "manifest.json"
    version = compute_version(list_version_file)
    map_old_digest: dict[str, str] = {}
    summary: collections.Counter[str] = collections.Counter()
    fresh = True   # 쓸 수 있는 manifest 가 없다(처음 · 판이 바뀜 · 깨짐) — 모두 다시 뽑고 옛 조각은 버린다
    if not reset:
        try:
            document = json.loads(manifest_path.read_text(encoding="utf-8"))
            if document.get("version") == version:
                map_old_digest, summary, fresh = document["shards"], collections.Counter(document["summary"]), False
        except (OSError, ValueError, KeyError):
            pass

    # 지금 조각마다의 경로 — 키를 모르는 파일(git 밖)이 든 조각은 늘 바뀐 것으로 본다(매번 다시 뽑는다).
    map_current_path: dict[str, list[str]] = {}
    for relative in list_relative:
        map_current_path.setdefault(get_shard_name(shard_of(relative)), []).append(relative)
    map_current_digest = {shard_name: digest_shard((path, keys[path]) for path in list_path) if all(path in keys for path in list_path) else ""
                        for shard_name, list_path in map_current_path.items()}
    set_changed = {shard_name for shard_name in set(map_current_digest) | set(map_old_digest)
                  if not map_current_digest.get(shard_name) or map_current_digest.get(shard_name) != map_old_digest.get(shard_name)}
    set_want = set(list_relative) & set(want)
    map_shard: dict[str, dict[str, list[Any]]] = {}

    def get_shard(shard_name: str) -> dict[str, list[Any]]:
        if shard_name not in map_shard:
            shard: dict[str, list[Any]] = {}
            if not fresh and shard_name in map_old_digest:
                try:
                    shard = json.loads((folder / shard_name).read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    raise CacheBroken(name) from None
                if digest_shard((path, entry[0]) for path, entry in shard.items()) != map_old_digest[shard_name]:
                    raise CacheBroken(name)   # 조각과 manifest 가 어긋났다(쓰다 멈췄다)
            map_shard[shard_name] = shard
        return map_shard[shard_name]

    def add_tokens(relative: str, fact: Any, sign: int) -> None:
        for token in contribute(relative, fact) if contribute is not None else ():
            summary[token] += sign
            if summary[token] < 0:
                raise CacheBroken(name)
            if summary[token] == 0:
                del summary[token]

    list_stale: list[str] = []
    for shard_name in sorted(set_changed):
        shard = get_shard(shard_name)
        set_current = set(map_current_path.get(shard_name, ()))
        for path in [path for path in shard if path not in set_current]:
            add_tokens(path, shard.pop(path)[1], -1)   # 지운 파일
        list_stale += [path for path in map_current_path.get(shard_name, ()) if path not in keys or shard.get(path, [None])[0] != keys[path]]
    if list_stale:
        for relative, text in read(list_stale).items():
            shard = get_shard(get_shard_name(shard_of(relative)))
            if relative in shard:
                add_tokens(relative, shard.pop(relative)[1], -1)
            if text is None:
                continue   # 못 읽은 파일은 빠진다
            fact = extract(relative, text)
            shard[relative] = [keys.get(relative, ""), fact]
            add_tokens(relative, fact, 1)
    map_fact = {relative: entry[1] for relative in list_relative if relative in set_want
               and (entry := get_shard(get_shard_name(shard_of(relative))).get(relative)) is not None}

    if set_changed or fresh:
        folder.mkdir(parents=True, exist_ok=True)
        if fresh:
            for stale in folder.glob("shard-*.json"):
                stale.unlink()   # 판이 바뀌었다 — 옛 조각은 쓰지 않는다
        map_new_digest = {shard_name: digest for shard_name, digest in map_old_digest.items() if shard_name not in set_changed} if not fresh else {}
        for shard_name in set_changed:
            shard = map_shard[shard_name]
            if shard:
                write_json(folder / shard_name, shard)
                map_new_digest[shard_name] = digest_shard((path, entry[0]) for path, entry in shard.items())
            else:
                (folder / shard_name).unlink(missing_ok=True)
        write_json(manifest_path, {"version": version, "shards": dict(sorted(map_new_digest.items())), "summary": dict(summary)})
    return Facts(map_fact, summary)


def load_facts(project_root: Path, name: str, list_version_file: Sequence[str], list_relative: Iterable[str], extract: Callable[[str, str], Any], **options: Any) -> dict[str, Any]:
    """`open_facts` 의 사실만(`want` 의 경로)."""
    return open_facts(project_root, name, list_version_file, list_relative, extract, **options).map_fact
