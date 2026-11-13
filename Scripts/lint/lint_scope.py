#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/lint_scope.py

규칙이 볼 파일 — 모든 린트 규칙은 `check(projectRoot, scope)` 한 모양이다(`rules.py`). 규칙은 저장소를 스스로 걷지 않고 이 목록에서 고른다.

- 커밋 훅(`py -3 -m Scripts lint`): 스테이지된 파일(더하기 · 고치기)과 지운 파일(옮기기의 옛 자리 포함). 커밋 비용이 저장소 크기가 아니라
  바뀐 양에 비례한다 — clang-tidy 를 바뀐 파일에만 돌리는 것, 언리얼 UBT 가 바뀐 모듈만 다시 짓는 것과 같다.
- `--all`(묶음 검증): git 이 아는 저장소 전체(추적 + 무시되지 않은 새 파일). `whole_repo` 가 참이다.

저장소 전체를 봐야만 내릴 수 있는 판정(낡은 예외 · 기준선 줄)은 `whole_repo` 일 때만 낸다 — 바뀐 파일만 보고 "더는 안 쓰인다" 고 말할 수 없다.
여러 파일의 관계를 보는 규칙은 `FactCache` 로 저장소 전체 색인을 바뀐 파일만 다시 계산해 쓴다.
"""

from __future__ import annotations

import functools
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from common.git import run_git_in_hook_repository


def run_git_lines(project_root: Path, list_argument: list[str]) -> list[str]:
    """`-z` 로 찍은 git 경로 목록. 실패하면 멈춘다 — 빈 목록이면 커밋 훅이 검사 없이 통과한다."""
    result = run_git_in_hook_repository([*list_argument, "-z"], cwd=project_root)   # 커밋하는 인덱스(`git commit -a` 의 임시 인덱스 포함)를 본다
    if result.return_code != 0:
        raise RuntimeError(f"git {' '.join(list_argument)} 실패: {result.stderr.strip()}")
    return [path for path in result.stdout.split("\0") if path]


@functools.cache
def list_repo_files(project_root: Path) -> tuple[str, ...]:
    """git 이 아는 저장소 파일(추적 + 무시되지 않은 새 파일, 작업 트리에서 지운 것은 뺀다) — 저장소 기준 posix 경로, 정렬."""
    set_deleted = set(run_git_lines(project_root, ["ls-files", "--deleted"]))
    list_file = run_git_lines(project_root, ["ls-files", "--cached", "--others", "--exclude-standard"])
    return tuple(sorted({path for path in list_file if path not in set_deleted}))


def is_under(relative: str, roots: Sequence[str]) -> bool:
    return any(root == "" or relative == root or relative.startswith(root.rstrip("/") + "/") for root in roots)


@dataclass(frozen=True)
class LintScope:
    list_file: tuple[str, ...]      # 볼 파일(있는 것), 저장소 기준 posix 경로
    list_removed: tuple[str, ...]   # 이 커밋이 지우는 파일 — 역참조 규칙이 본다
    whole_repo: bool

    def select(self, roots: Sequence[str] = ("",), suffixes: Sequence[str] = (), skip_parts: Sequence[str] = ()) -> list[str]:
        """`roots`(저장소 기준 폴더, `""` 은 전체) 아래, 접미사가 `suffixes`(비우면 모두)이고 `skip_parts` 마디가 없는 파일."""
        tuple_suffix = tuple(suffixes)
        return [path for path in self.list_file
                if is_under(path, roots) and (not tuple_suffix or path.endswith(tuple_suffix))
                and not (skip_parts and any(part in skip_parts for part in path.split("/")))]

    def touches(self, roots: Sequence[str] = ("",), suffixes: Sequence[str] = (), files: Iterable[str] = ()) -> bool:
        """더하거나 고치거나 지운 파일 가운데 `roots` · `suffixes` 에 들거나 `files` 인 것이 있는가 — 색인 규칙이 돌지 정한다."""
        tuple_suffix = tuple(suffixes)
        set_file = set(files)
        return any(path in set_file or (is_under(path, roots) and (not tuple_suffix or path.endswith(tuple_suffix)))
                   for path in (*self.list_file, *self.list_removed))

    def list_absolute(self, project_root: Path, suffixes: Sequence[str] = ()) -> list[str]:
        """파일마다 도는 규칙(프로세스로 나눈다)에 넘길 절대 경로."""
        return [str(project_root / path) for path in self.select(suffixes=suffixes)]


def make_whole_scope(project_root: Path) -> LintScope:
    return LintScope(list_repo_files(project_root), (), True)


def list_merge_parents(project_root: Path) -> list[str]:
    """병합 중이면 들여오는 커밋들(`MERGE_HEAD` 의 줄 — 문어발 병합은 여럿), 아니면 빈 목록."""
    result = run_git_in_hook_repository(["rev-parse", "--path-format=absolute", "--git-path", "MERGE_HEAD"], cwd=project_root)
    if result.return_code != 0:
        raise RuntimeError(f"git rev-parse --git-path MERGE_HEAD 실패: {result.stderr.strip()}")
    merge_head = Path(result.stdout.strip())
    return merge_head.read_text(encoding="utf-8").split() if merge_head.is_file() else []


def make_staged_scope(project_root: Path) -> LintScope:
    """
    스테이지된 변경. 옮기기는 지우기 + 더하기로 본다(`--no-renames`) — 옮기며 고친 파일도 검사에서 빠지지 않는다.

    병합 중이면 스테이지 내용이 **모든 부모와 다른** 파일(손으로 푼 충돌 · 병합하며 고친 파일)만 본다 — 한쪽 부모와 같은 파일은 그 브랜치의 커밋에서
    이미 린트를 지났다. 두 브랜치가 따로는 맞고 합치면 어긋나는 의미 충돌(한쪽이 지운 이름을 다른 쪽이 새로 쓴다)은 묶음 검증 첫 단계 `lint --all` 이 본다.
    """
    list_changed = run_git_lines(project_root, ["diff", "--cached", "--name-only", "--no-renames", "--diff-filter=ACM"])
    list_removed = run_git_lines(project_root, ["diff", "--cached", "--name-only", "--no-renames", "--diff-filter=D"])
    for parent in list_merge_parents(project_root):
        set_differ = set(run_git_lines(project_root, ["diff", "--cached", "--name-only", "--no-renames", parent]))
        list_changed = [path for path in list_changed if path in set_differ]
        list_removed = [path for path in list_removed if path in set_differ]
    return LintScope(tuple(sorted(path for path in list_changed if (project_root / path).is_file())), tuple(sorted(list_removed)), False)


def make_file_scope(project_root: Path, list_file_argument: Iterable[str]) -> LintScope:
    """규칙 모듈의 `--files` — 절대 · 상대 경로를 저장소 기준으로 푼다(저장소 밖 · 없는 파일은 뺀다)."""
    resolved_root = project_root.resolve()
    list_file: set[str] = set()
    for item in list_file_argument:
        path = Path(item)
        path = (project_root / path if not path.is_absolute() and (project_root / path).exists() else path).resolve()
        if path.is_file():
            try:
                list_file.add(path.relative_to(resolved_root).as_posix())
            except ValueError:
                continue
    return LintScope(tuple(sorted(list_file)), (), False)


@functools.lru_cache(maxsize=4096)
def _read_bytes(key: str) -> bytes | None:
    try:
        return Path(key).read_bytes()
    except FileNotFoundError:
        return None


def read_bytes(path: Path | str) -> bytes | None:
    """
    파일 바이트(없으면 None, 그 밖의 OSError 는 올린다). **규칙은 파일 내용을 이것(`readText` · `read_texts`)이나 `FactCache` 로만 얻는다** — 커밋 훅은
    규칙 스물 몇 개가 같은 스테이지 파일을 보므로 한 번 읽어 함께 쓴다. `pre_commit.check_read_bound` 가 스테이지 파일 하나에 모든 규칙이 연 파일이 그 파일
    한 번뿐인지 본다(직접 `read_text` 하면 두 번째 열기로 걸린다). 크기 한도는 `--all` 이 저장소 전체를 붙잡지 않게 둔다(커밋 훅의 스테이지는 다 담는다).
    """
    return _read_bytes(os.path.normcase(os.path.abspath(path)))


def read_text(path: Path | str, errors: str = "strict", encoding: str = "utf-8") -> str | None:
    """`Path.read_text` 와 같은 글(줄 끝은 LF 로). 못 읽거나(지웠거나 OSError) `errors="strict"` 에서 UTF-8 이 아니면 None."""
    try:
        data = read_bytes(path)
        return None if data is None else data.decode(encoding, errors).replace("\r\n", "\n").replace("\r", "\n")
    except (UnicodeDecodeError, OSError):
        return None


def read_texts(project_root: Path, list_relative: Iterable[str], errors: str = "strict") -> list[tuple[str, str]]:
    """(경로, 글) 목록. 못 읽는 파일(지웠거나 UTF-8 이 아니면 — `errors="strict"` 일 때)은 건너뛴다(규칙들이 저장소를 걸을 때와 같다)."""
    list_source: list[tuple[str, str]] = []
    for relative in list_relative:
        text = read_text(project_root / relative, errors)
        if text is not None:
            list_source.append((relative, text))
    return list_source
