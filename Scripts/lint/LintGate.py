#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
게이트 하나 = 클래스 하나.

`gate/` 의 린트들은 하는 일이 제각각이지만 **껍데기는 늘 같다**: UTF-8 출력을 켜고, `--root` 를
받고, 저장소 루트를 정하고, 위반 목록을 찍고, 있으면 1 · 없으면 0 을 돌려준다. 게이트마다 이것을 적으면
각자 조금씩 달라진다 — `--root` 기본값이 저장소가 아니라 `Scripts/` 를 가리키거나(CMake · 훅은 늘 `--root` 를
줘서 손으로 돌릴 때만 드러난다), `main()` 이 인자 없이 `parse_args()` 를 불러 프로그램에서 부를 수 없거나,
위반 줄이 stdout · stderr 로 갈린다.

그래서 껍데기를 여기 한 번만 적는다. 게이트가 쓰는 것은 **`scan()` 하나**다.

새 게이트를 넣는 법:

```python
class CheckSomethingGate(LintGate):
    description = "무엇을 검사하는가"
    preCommitPattern = ("Source/*",)            # 이 파일이 staged 됐을 때만 훅에서 돈다
    preCommitFileArgument = "--files"           # 훅이 staged 부분집합을 --files 로 넘긴다
    selfTestCases = [{"name": "...", "files": {"Source/Probe.h": "..."}}]

    def addArguments(self, parser):
        self.addFilesArgument(parser)

    def scan(self, repositoryRoot, args):
        # 파일 고르기: --files 와 전체 훑기가 같은 규칙, 빌드 산출물 · 내려받은 외부 도구로는 내려가지 않는다
        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=("Source",), suffixes=(".h", ".cpp"))
        # 읽기: 동시에, 찾는 표식이 없는 파일은 거른다(비싼 정규식 앞에서)
        listViolation = [f"{path}: ..." for path, text in readTextFiles(listPath, mustContain="SOMETHING") if ...]
        return GateResult(listViolation=listViolation, summary=f"{len(listPath)} files scanned")

main = CheckSomethingGate.run
```

파일마다 파이썬 정규식을 오래 돌리는 게이트라면 파일별 일을 `common.flatMapInProcesses` 로 나눈다(`CheckCodeConventions`) —
스레드는 GIL 에 막힌다. 파일 읽기가 대부분이면 `mapConcurrent` · `readTextFiles` 로 충분하다(`common/Parallel.py` 머리말).

`gate/` 에 놓으면 `CheckLintsAreAlive` 가 알아서 집어 가고(목록이 아니라 자리가 규칙이다),
증거(`selfTestCases`)를 들고 있지 않으면 그 자리에서 실패한다.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path
from types import ModuleType
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import collectRepositoryFiles, getProjectRoot, kNotOurDirNames, readTextFiles, resolveFileArguments  # noqa: E402


class GateError(Exception):
    """
    검사가 **성립하지 않는다** — 위반(1)이 아니라 오류(2)로 끝냅니다.

    "위반이 없다" 와 "검사할 수 없었다" 는 다른 답이다. 후자를 0 으로 돌려주면 게이트가 조용히
    죽고, 1 로 돌려주면 코드를 고쳐도 통과하지 않는다.
    """


@dataclass
class GateResult:
    """
    게이트가 한 번 훑고 돌려주는 것.

    - `listViolation`: 있으면 실패(1). 한 줄에 하나씩 찍힌다.
    - `listNote`     : 찍기만 하고 **실패시키지 않는다**(경고·참고).
    - `summary`      : 위반이 없을 때 `OK (...)` 괄호 안에 들어갈 문구. 무엇을 얼마나 봤는지 적는다 —
                       "0건" 과 "아무것도 안 봤다" 를 구분해 주는 것이 이 한 줄이다.
    """

    listViolation: list[str] = field(default_factory=list)
    listNote: list[str] = field(default_factory=list)
    summary: str = ""


class LintGate:
    """
    게이트 린트 하나. 상속해서 `scan()` 만 쓰면 CLI·출력·종료 코드는 기반이 준다.

    - `name`             : 로그 태그 `[이름]`. 비우면 모듈 이름(= 파일 이름)을 쓴다.
    - `description`      : `--help` 한 줄.
    - `violationHeader`  : 실패 머리말의 낱말. 기본 "위반".
    - `hint`             : 위반 목록 뒤에 덧붙일 안내 한 줄(고치는 법).
    - `maxViolationShown`: 위반을 몇 개까지 찍을지. 0 이면 전부.
    - `maxNoteShown`     : 참고를 몇 개까지 찍을지.
    - `selfTestCases`    : 이 게이트가 **반드시 잡아야 하는** 조각. `CheckLintsAreAlive` 가 읽어 간다.
    - `selfTestSkipReason`: 조각을 만들 수 없는 이유. 이유 없는 예외는 없다.

    CMake 가 이 게이트를 타깃·CTest 로 등록할 때 묻는 것 셋도 여기 있다 — CMake 에 따로 적으면 파이썬과 어긋난다
    (`Scripts/lint/LintCatalog.py` 가 읽어 간다).

    - `buildComment`     : ninja 가 이 타깃을 만들 때 찍는 줄. **영어다** — 이 저장소의 빌드 출력은
                           전부 영어이고, Windows 콘솔 코드페이지에서 한글이 깨진 전례가 있다.
    - `timeoutSeconds`   : CTest TIMEOUT.
    - `listCtestArgument`: `--root` 말고 더 줄 인자. CMake 변수 참조를 그대로 적는다.
    - `ctestSkipReason`  : CTest 린트로 등록하지 않는 이유(트리 전체가 몇 분 걸리는 검사). 커밋 훅 · 직접 실행은 그대로다.
                           이유 없는 예외는 없다 — 그 검사를 트리 전체로 돌리는 다른 자리(CI 잡)를 적는다.
    """

    name: str = ""
    description: str = ""
    buildComment: str = ""
    timeoutSeconds: int = 30
    listCtestArgument: tuple[str, ...] = ()
    ctestSkipReason: str = ""
    violationHeader: str = "위반"
    noteHeader: str = "참고"
    hint: str = ""
    maxViolationShown: int = 0
    maxNoteShown: int = 20
    selfTestCases: list[dict] = []
    selfTestSkipReason: str = ""

    # --- 커밋 훅이 묻는 것 ---------------------------------------------------
    #
    # 훅도 폴더를 훑는다 — 게이트를 이름으로 import 하면 새 게이트가 빠지고, C++ 가 staged 됐을 때만 돌리면
    # **`.cmake` 나 `.py` 만 커밋할 때 아무 게이트도 돌지 않는다.** 훅이 알아야 하는 것은 게이트마다 다르므로 게이트가 든다 —
    # `LintCatalog` 가 CMake 등록 정보를 게이트에서 가져가는 것과 같은 방식이다.
    #
    # - `preCommitPattern`     : 이 패턴에 맞는 파일이 staged 되었을 때만 돈다 (`fnmatch`,
    #                            저장소 기준 POSIX 경로). **비우면 항상 돈다.**
    # - `preCommitFileArgument`: staged 부분집합을 넘기는 방법. `"--files"`(게이트 · 픽서 같은 철자) ·
    #                            `""`(전체를 훑는 게이트).
    # - `preCommitSkipReason`  : 훅에서 돌 수 없는 이유. 이유 없는 예외는 없다.
    preCommitPattern: tuple[str, ...] = ()
    preCommitFileArgument: str = ""
    preCommitSkipReason: str = ""

    def __init_subclass__(cls, **kwargs) -> None:
        super().__init_subclass__(**kwargs)
        if not cls.name:
            # 클래스 이름에서 `Gate` 를 뗀 것이 로그 태그다 — `CheckEngineLayersGate` -> `CheckEngineLayers`.
            # 모듈 이름을 쓰지 않는 이유: 스크립트로 직접 돌리면 `__main__` 이 된다.
            cls.name = cls.__name__.removesuffix("Gate")

    # --- 구현이 채우는 것 ------------------------------------------------------

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        """`--root` 말고 더 받을 인자가 있으면 여기서 더합니다."""

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        """저장소를 한 번 훑고 결과를 돌려줍니다. 검사가 성립하지 않으면 `GateError` 를 던집니다."""
        raise NotImplementedError

    # --- 기반이 주는 것 --------------------------------------------------------

    @classmethod
    def run(cls, argv: list[str] | None = None) -> int:
        """진입점. 각 게이트 모듈은 `main = XxxGate.run` 한 줄로 이것을 내보냅니다."""
        return cls().main(argv)

    def main(self, argv: list[str] | None = None) -> int:
        parser = argparse.ArgumentParser(description=self.description)
        parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
        self.addArguments(parser)
        args = parser.parse_args(argv)

        repositoryRoot = (args.root or getProjectRoot()).resolve()
        try:
            result = self.scan(repositoryRoot, args)
        except GateError as exception:
            print(f"[{self.name}] {exception}", file=sys.stderr)
            return 2
        return self.report(result)

    def report(self, result: GateResult) -> int:
        """결과를 찍고 종료 코드를 정합니다. 출력 모양을 바꾸려는 게이트만 재정의합니다."""
        if result.listNote:
            print(f"[{self.name}] {self.noteHeader} {len(result.listNote)}건:")
            self.printListInternal(result.listNote, self.maxNoteShown)

        if result.listViolation:
            print(f"[{self.name}] {self.violationHeader} {len(result.listViolation)}건:", file=sys.stderr)
            self.printListInternal(result.listViolation, self.maxViolationShown)
            if self.hint:
                print(self.hint)
            return 1

        print(f"[{self.name}] OK ({result.summary})" if result.summary else f"[{self.name}] OK")
        return 0

    # --- 대상 파일 고르기 — `--files` 를 받는 게이트가 함께 쓴다 ---------------

    @staticmethod
    def addFilesArgument(parser: argparse.ArgumentParser, helpText: str = "검사할 파일 (생략 시 전체)") -> None:
        """커밋 훅이 staged 부분집합을 넘기는 `--files` 인자를 더합니다(`preCommitFileArgument = "--files"`)."""
        parser.add_argument("--files", nargs="*", default=None, help=helpText)

    @staticmethod
    def selectTargetFiles(repositoryRoot: Path,
                          listFileArgument: list[str] | None,
                          *,
                          listScanRoot: Iterable[str] = ("",),
                          suffixes: Iterable[str] = (),
                          fileNames: Iterable[str] = (),
                          excludedDirNames: Iterable[str] = kNotOurDirNames,
                          bAnySuffix: bool = False) -> list[Path]:
        """
        게이트가 볼 파일을 고릅니다. `--files` 가 있으면 그 파일 가운데서(`common.resolveFileArguments`), 없으면 `listScanRoot` 를 걸어서
        (`common.collectRepositoryFiles`) — **같은 규칙으로**.

        - 확장자가 `suffixes` 이거나 이름이 `fileNames` 인 파일만(`bAnySuffix` 면 모두).
        - `excludedDirNames` 의 폴더 아래는 뺀다(기본은 빌드 산출물 · 내려받은 외부 도구, `kNotOurDirNames`). 걸을 때는 그 폴더로 내려가지 않는다.
        - `listScanRoot`(저장소 기준, `""` 은 전체) 밖의 파일은 뺀다.
        - `--files` 의 상대 경로는 **저장소 루트 기준**으로 풀고, 거기 없으면 현재 폴더 기준으로 푼다. 저장소 밖 파일은 뺀다.

        게이트마다 이 일을 따로 하지 말 것 — 상대 경로를 푸는 기준과 제외 목록이 갈리고, `--files` 에 제외를 빠뜨리면
        **커밋 훅과 전체 검사가 서로 다른 파일을 본다.** 픽서도 같은 함수로 고른다(`LintFixer.selectFixerTargetFiles`).
        """
        listScanRoot = tuple(listScanRoot)
        if not listFileArgument:
            return collectRepositoryFiles(repositoryRoot, listScanRoot, suffixes=suffixes, fileNames=fileNames,
                                          excludedDirNames=excludedDirNames, bAnySuffix=bAnySuffix)
        return resolveFileArguments(repositoryRoot, listFileArgument, listScanRoot=listScanRoot, suffixes=suffixes, fileNames=fileNames,
                                    excludedDirNames=excludedDirNames, bAnySuffix=bAnySuffix)

    @staticmethod
    def readFiles(listPath: Iterable[Path], *, encoding: str = "utf-8", errors: str = "replace",
                  mustContain: str | None = None) -> list[tuple[Path, str]]:
        """
        게이트가 파일 내용을 읽는 **창구** — 지금은 `common.readTextFiles`(동시 읽기 · `mustContain` 앞 거르기)를 그대로 부른다.
        한 번 읽어 여러 게이트가 나눠 쓰기 · 내용 해시 캐시를 얹을 자리는 이 메서드와 `selectTargetFiles` 둘이다 — 게이트가 `open()` ·
        `read_text()` 를 직접 부르면 그 최적화를 비켜 간다.
        """
        return readTextFiles(listPath, encoding=encoding, errors=errors, mustContain=mustContain)

    @staticmethod
    def printListInternal(lines: list[str], maxShown: int) -> None:
        shown = lines[:maxShown] if maxShown > 0 else lines
        for line in shown:
            print(f"  - {line}")
        if len(lines) > len(shown):
            print(f"  ... +{len(lines) - len(shown)} more")


def findGateClass(module: ModuleType) -> type[LintGate] | None:
    """모듈 안에 정의된 게이트 클래스를 찾습니다 (한 파일에 게이트 하나)."""
    for value in vars(module).values():
        if isinstance(value, type) and issubclass(value, LintGate) and value is not LintGate:
            if value.__module__ == module.__name__:
                return value
    return None
