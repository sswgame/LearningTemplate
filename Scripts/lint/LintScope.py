#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/LintScope.py

규칙이 볼 파일 — 모든 린트 규칙은 `check(projectRoot, scope)` 한 모양이다(`Rules.py`). 규칙은 저장소를 스스로 걷지 않고 이 목록에서 고른다.

- 커밋 훅(`py -3 -m Scripts lint`): 스테이지된 파일(더하기 · 고치기)과 지운 파일(옮기기의 옛 자리 포함). 커밋 비용이 저장소 크기가 아니라
  바뀐 양에 비례한다 — clang-tidy 를 바뀐 파일에만 돌리는 것, 언리얼 UBT 가 바뀐 모듈만 다시 짓는 것과 같다.
- `--all`(묶음 검증): git 이 아는 저장소 전체(추적 + 무시되지 않은 새 파일). `bWholeRepo` 가 참이다.

저장소 전체를 봐야만 내릴 수 있는 판정(낡은 예외 · 기준선 줄)은 `bWholeRepo` 일 때만 낸다 — 바뀐 파일만 보고 "더는 안 쓰인다" 고 말할 수 없다.
여러 파일의 관계를 보는 규칙은 `FactCache` 로 저장소 전체 색인을 바뀐 파일만 다시 계산해 쓴다.
"""

from __future__ import annotations

import functools
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from common.Git import runGit


def runGitLines(projectRoot: Path, listArgument: list[str]) -> list[str]:
    """`-z` 로 찍은 git 경로 목록. 실패하면 멈춘다 — 빈 목록이면 커밋 훅이 검사 없이 통과한다."""
    result = runGit([*listArgument, "-z"], cwd=projectRoot)
    if result.returnCode != 0:
        raise RuntimeError(f"git {' '.join(listArgument)} 실패: {result.stderr.strip()}")
    return [path for path in result.stdout.split("\0") if path]


@functools.cache
def listRepoFiles(projectRoot: Path) -> tuple[str, ...]:
    """git 이 아는 저장소 파일(추적 + 무시되지 않은 새 파일, 작업 트리에서 지운 것은 뺀다) — 저장소 기준 posix 경로, 정렬."""
    setDeleted = set(runGitLines(projectRoot, ["ls-files", "--deleted"]))
    listFile = runGitLines(projectRoot, ["ls-files", "--cached", "--others", "--exclude-standard"])
    return tuple(sorted({path for path in listFile if path not in setDeleted}))


def isUnder(relative: str, roots: Sequence[str]) -> bool:
    return any(root == "" or relative == root or relative.startswith(root.rstrip("/") + "/") for root in roots)


@dataclass(frozen=True)
class LintScope:
    listFile: tuple[str, ...]      # 볼 파일(있는 것), 저장소 기준 posix 경로
    listRemoved: tuple[str, ...]   # 이 커밋이 지우는 파일 — 역참조 규칙이 본다
    bWholeRepo: bool

    def select(self, roots: Sequence[str] = ("",), suffixes: Sequence[str] = (), skipParts: Sequence[str] = ()) -> list[str]:
        """`roots`(저장소 기준 폴더, `""` 은 전체) 아래, 접미사가 `suffixes`(비우면 모두)이고 `skipParts` 마디가 없는 파일."""
        tupleSuffix = tuple(suffixes)
        return [path for path in self.listFile
                if isUnder(path, roots) and (not tupleSuffix or path.endswith(tupleSuffix))
                and not (skipParts and any(part in skipParts for part in path.split("/")))]

    def touches(self, roots: Sequence[str] = ("",), suffixes: Sequence[str] = (), files: Iterable[str] = ()) -> bool:
        """더하거나 고치거나 지운 파일 가운데 `roots` · `suffixes` 에 들거나 `files` 인 것이 있는가 — 색인 규칙이 돌지 정한다."""
        tupleSuffix = tuple(suffixes)
        setFile = set(files)
        return any(path in setFile or (isUnder(path, roots) and (not tupleSuffix or path.endswith(tupleSuffix)))
                   for path in (*self.listFile, *self.listRemoved))

    def listAbsolute(self, projectRoot: Path, suffixes: Sequence[str] = ()) -> list[str]:
        """파일마다 도는 규칙(프로세스로 나눈다)에 넘길 절대 경로."""
        return [str(projectRoot / path) for path in self.select(suffixes=suffixes)]


def makeWholeScope(projectRoot: Path) -> LintScope:
    return LintScope(listRepoFiles(projectRoot), (), True)


def makeStagedScope(projectRoot: Path) -> LintScope:
    """스테이지된 변경. 옮기기는 지우기 + 더하기로 본다(`--no-renames`) — 옮기며 고친 파일도 검사에서 빠지지 않는다."""
    listChanged = runGitLines(projectRoot, ["diff", "--cached", "--name-only", "--no-renames", "--diff-filter=ACM"])
    listRemoved = runGitLines(projectRoot, ["diff", "--cached", "--name-only", "--no-renames", "--diff-filter=D"])
    return LintScope(tuple(sorted(path for path in listChanged if (projectRoot / path).is_file())), tuple(sorted(listRemoved)), False)


def makeFileScope(projectRoot: Path, listFileArgument: Iterable[str]) -> LintScope:
    """규칙 모듈의 `--files` — 절대 · 상대 경로를 저장소 기준으로 푼다(저장소 밖 · 없는 파일은 뺀다)."""
    resolvedRoot = projectRoot.resolve()
    listFile: set[str] = set()
    for item in listFileArgument:
        path = Path(item)
        path = (projectRoot / path if not path.is_absolute() and (projectRoot / path).exists() else path).resolve()
        if path.is_file():
            try:
                listFile.add(path.relative_to(resolvedRoot).as_posix())
            except ValueError:
                continue
    return LintScope(tuple(sorted(listFile)), (), False)


def readTexts(projectRoot: Path, listRelative: Iterable[str], errors: str = "strict") -> list[tuple[str, str]]:
    """(경로, 글) 목록. 못 읽는 파일(지웠거나 UTF-8 이 아니면 — `errors="strict"` 일 때)은 건너뛴다(규칙들이 저장소를 걸을 때와 같다)."""
    listSource: list[tuple[str, str]] = []
    for relative in listRelative:
        try:
            listSource.append((relative, (projectRoot / relative).read_text(encoding="utf-8", errors=errors)))
        except (UnicodeDecodeError, OSError):
            continue
    return listSource
