#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
빌드 트리의 컴파일 DB 를 읽어 **TU 를 골라 하나씩 돌리는** 자리.

`report/` 의 `RunClangTidy` 는 clang-tidy 를, `RunBuildWarnings` 는 `-fsyntax-only` 컴파일을 TU 마다 돌린다.
도구만 다를 뿐 앞뒤가 같으므로 그 앞뒤를 여기 `TranslationUnitSweep` 이 맡는다:

| | 맡는 것 |
| --- | --- |
| 컴파일 DB 읽기 | `BuildTree.readCompileDatabase` |
| TU 거르기 | `/generated/` · `*.gen.cpp` 제외, `--filter` 부분 문자열 |
| 동시 실행 | `common.Parallel.mapConcurrent`(진행률 콜백 포함) |
| 결과 | 출력 문자열 이어 붙이기 |

두 스크립트에 남는 것은 **자기 도구를 어떻게 부르는가**와 **결과를 어떻게 묶어 읽히는가** 뿐이다.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Sequence, TypeVar

from .BuildTree import BuildTree, BuildTreeError
from .Parallel import getProcessWorkerCount, mapConcurrent

#: 어느 도구에서든 대상이 아닌 TU — 리플렉션 코드젠 산출물과 CMake 의 PCH 더미.
_kAlwaysSkipPart = ("/generated/",)
_kAlwaysSkipSuffix = (".gen.cpp", "cmake_pch.cxx", "cmake_pch.c")

TItem = TypeVar("TItem")


class TranslationUnitSweep:
    """
    빌드 트리 하나의 TU 를 골라 자식 프로세스로 훑는다.

    빌드 폴더(`BuildTree`)만 알면 되고, **무엇을 돌릴지는 호출부가 준다**(`run` 의 `worker`). 도구를 여기
    알게 만들면 세 번째 도구가 생길 때 다시 갈라진다.
    """

    def __init__(self, tree: BuildTree, *, tag: str) -> None:
        self.tree = tree
        self._tag = tag
        #: 마지막 `selectUnits` 가 건너뛴, 디스크에 없는 TU(낡은 컴파일 DB — 이름을 바꾸고 그 프리셋을 다시 안 지었을 때).
        self.listMissingFile: list[str] = []

    @property
    def bHasDatabase(self) -> bool:
        """configure 한 빌드 트리인가 — 아니면 이 훑기는 성립하지 않는다."""
        return self.tree.bHasCompileDatabase

    def selectUnits(self, pathFilter: str = "", *, requirePathPart: str = "") -> list[dict]:
        """
        검사할 TU 의 컴파일 DB 항목을 고릅니다 (DB 가 없으면 빈 목록).

        생성 코드와 PCH 더미는 언제나 뺀다 — 우리가 고칠 대상이 아니다.
        `requirePathPart` 를 주면 그 조각이 든 경로만 남긴다(예: `/Source/`).
        `pathFilter` 는 사용자가 `--filter` 로 주는 부분 문자열이다.
        디스크에 없는 TU(낡은 DB)는 건너뛰고 `listMissingFile` 에 남긴다 — 컴파일러가 "파일 없음" 을 오류로 내면 경고 보고가 그 줄로 덮인다.
        """
        self.listMissingFile = []
        try:
            listDatabase = self.tree.readCompileDatabase()
        except BuildTreeError:
            return []

        listEntry: list[dict] = []
        for entry in listDatabase:
            filePath = str(entry.get("file", "")).replace("\\", "/")
            if requirePathPart and requirePathPart not in filePath:
                continue
            if any(part in filePath for part in _kAlwaysSkipPart):
                continue
            if filePath.endswith(_kAlwaysSkipSuffix):
                continue
            if pathFilter and pathFilter.lower() not in filePath.lower():
                continue
            sourcePath = Path(filePath)
            if sourcePath.is_absolute() is False:
                sourcePath = Path(str(entry.get("directory", ""))) / sourcePath
            if sourcePath.is_file() is False:
                self.listMissingFile.append(filePath)
                continue
            listEntry.append(entry)
        return listEntry

    def run(
        self,
        listItem: Sequence[TItem],
        worker: Callable[[TItem], str],
        *,
        workerCount: int | None = None,
        progressEvery: int = 0,
    ) -> str:
        """
        항목마다 `worker(item)` 를 동시에 돌리고 출력을 이어 붙여 돌려줍니다.

        항목은 보통 `selectUnits` 가 준 DB 항목이지만, 파일 경로만 필요하면 호출부가 그것으로
        바꿔 넘겨도 된다(clang-tidy 는 같은 파일이 여러 타깃에 있어 **유일화해서** 넘긴다).

        워커 수 기본값은 `getProcessWorkerCount` 다 — 자식 프로세스가 이미 코어를 쓰므로
        스캔용 정책(코어의 두 배)을 쓰면 서로 경합한다. `progressEvery` 개마다 진행률을 찍는다.
        """
        if not listItem:
            return ""

        workers = workerCount or getProcessWorkerCount(len(listItem))

        def onProgressInternal(doneCount: int, totalCount: int) -> None:
            if doneCount % progressEvery == 0:
                print(f"  ... {doneCount}/{totalCount}")

        listOutput = list(
            mapConcurrent(
                worker,
                list(listItem),
                workerCount=workers,
                onProgress=onProgressInternal if progressEvery > 0 else None,
            )
        )
        return "".join(listOutput)

    def reportMissingDatabase(self) -> None:
        """DB 가 없을 때 무엇을 하라고 말할지 — 두 스크립트가 같은 말을 한다."""
        print(f"[{self._tag}] {self.tree.compileDatabasePath} 가 없습니다 — "
              f"`cmake --preset {self.tree.name}` 으로 configure 하세요.")
