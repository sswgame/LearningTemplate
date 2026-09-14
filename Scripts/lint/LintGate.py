#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
게이트 하나 = 클래스 하나.

`gate/` 의 린트들은 하는 일이 제각각이지만 **껍데기는 늘 같았다**: UTF-8 출력을 켜고, `--root` 를
받고, 저장소 루트를 정하고, 위반 목록을 찍고, 있으면 1 · 없으면 0 을 돌려준다. 여덟 파일이 그
열댓 줄을 각자 적고 있었고, 그래서 **각자 조금씩 달랐다**:

- `--root` 기본값이 두 종류였다. `CheckRenderOwnership` · `CheckTestSuites` 는
  `parents[2]` — `Scripts/` 였다(저장소 루트는 `parents[3]` 이다). 문서에 적힌 대로
  `py -3 Scripts/lint/gate/CheckRenderOwnership.py` 를 치면 `Scripts/` 를 훑고 "헤더가 없습니다"
  로 실패했다. CMake·PreCommitLint 가 늘 `--root` 를 줘서 아무도 몰랐을 뿐이다.
- 절반은 `main()` 이 `parse_args()` 를 인자 없이 불러 **프로그램에서 부를 수 없었다**
  (`CheckEngineLayers` · `CheckDataFileReferences` · `CheckResourceCasing` · `CheckSourceGlob`).
- 위반 줄이 어디는 stdout, 어디는 stderr 로 갔고, 머리말도 `  - ` 와 `  ` 가 섞여 있었다.

그래서 껍데기를 여기 한 번만 적는다. 게이트가 쓰는 것은 **`scan()` 하나**다.

새 게이트를 넣는 법:

```python
class CheckSomethingGate(LintGate):
    description = "무엇을 검사하는가"
    selfTestCases = [{"name": "...", "files": {"Source/Probe.h": "..."}}]

    def scan(self, repositoryRoot, args):
        return GateResult(listViolation=[...], summary=f"{n} files scanned")

main = CheckSomethingGate.run
```

`gate/` 에 놓으면 `CheckLintsAreAlive` 가 알아서 집어 가고(목록이 아니라 자리가 규칙이다),
증거(`selfTestCases`)를 들고 있지 않으면 그 자리에서 실패한다.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path
from types import ModuleType

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import getProjectRoot, useUtf8Stdout  # noqa: E402


class GateError(Exception):
    """
    검사가 **성립하지 않는다** — 위반(1)이 아니라 오류(2)로 끝냅니다.

    "위반이 없다" 와 "검사할 수 없었다" 는 다른 답이다. 후자를 0 으로 돌려주면 게이트가 조용히
    죽고(이 저장소가 이미 겪었다), 1 로 돌려주면 코드를 고쳐도 통과하지 않는다.
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

    CMake 가 이 게이트를 타깃·CTest 로 등록할 때 묻는 것 셋도 여기 있다. 예전에는 그 셋이
    `cmake/Engine/AssetAndToolTargets.cmake` 에 손으로 적혀 있었다 — 게이트를 하나 더하면
    파이썬과 CMake 두 곳을 고쳐야 했고, 둘은 언제든 어긋날 수 있었다(`Scripts/lint/LintCatalog.py`).

    - `buildComment`     : ninja 가 이 타깃을 만들 때 찍는 줄. **영어다** — 이 저장소의 빌드 출력은
                           전부 영어이고, Windows 콘솔 코드페이지에서 한글이 깨진 전례가 있다.
    - `timeoutSeconds`   : CTest TIMEOUT.
    - `listCtestArgument`: `--root` 말고 더 줄 인자. CMake 변수 참조를 그대로 적는다.
    """

    name: str = ""
    description: str = ""
    buildComment: str = ""
    timeoutSeconds: int = 30
    listCtestArgument: tuple[str, ...] = ()
    violationHeader: str = "위반"
    noteHeader: str = "참고"
    hint: str = ""
    maxViolationShown: int = 0
    maxNoteShown: int = 20
    selfTestCases: list[dict] = []
    selfTestSkipReason: str = ""

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
        useUtf8Stdout()

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
