#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
픽서 하나 = 클래스 하나. `LintGate` 의 형제다.

`gate/` 를 정리하고 나서 `fixer/` 를 보니 같은 일이 벌어져 있었다 — 이쪽은 더 노골적이다.
**대상 파일을 고르는 스무 줄이 세 스크립트에 글자 그대로 복사돼 있었다**
(`FormatBranchBraces` · `FormatForwardDeclarations` · `RunClangFormat`):

```python
    if args.files:
        fileList = [Path(f).resolve() for f in args.files if Path(f).is_file()]
    elif args.all:
        fileList = collectSourceFiles(getLintSearchDirs(root))
    else:
        modifiedFiles = getModifiedCppFiles(root)
        ...
```

거기에 `--all`/`--check` argparse 블록, `flatMapConcurrent` 배치 함수, "위반이 있고 --check 면 1"
종료 규칙까지 같았다. 다른 것은 **설명 문자열과 로그 태그뿐**이었다.

픽서가 쓰는 것은 `listPass` 하나다 — 텍스트를 받아 `(새 텍스트, 바뀌었는가)` 를 돌려주는 변환과,
그 변환이 잡은 것을 검사 모드와 수정 모드에서 각각 뭐라고 부를지. 파일 읽기·쓰기는 기반이 맡는다
(줄끝을 보존하는 `newline=""` 로 통일했다 — 포맷터가 줄끝을 바꾸면 안 된다).

대상 파일 고르기는 `addFileArguments` / `selectTargetFiles` 로 따로 내놓는다. 픽서가 아닌
`RunClangFormat` 도 같은 규칙으로 파일을 고르기 때문이다.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Callable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (  # noqa: E402
    collectSourceFiles,
    flatMapConcurrent,
    getLintSearchDirs,
    getModifiedCppFiles,
    getProjectRoot,
    useUtf8Stdout,
)


def addFileArguments(parser: argparse.ArgumentParser) -> None:
    """대상 파일을 고르는 인자 — 세 스크립트가 같은 철자를 쓴다."""
    parser.add_argument(
        "files",
        nargs="*",
        help="대상 C++ 파일 목록 (생략 시 Git 변경 파일, 없으면 전체 대상)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="프로젝트 전체 C++ 파일에 대해 실행",
    )


def selectTargetFiles(args: argparse.Namespace, repositoryRoot: Path, tag: str) -> list[Path]:
    """
    무엇을 고칠지 고릅니다: 지정한 파일 > `--all` > Git 변경 파일 > 전체.

    마지막 폴백("변경된 파일이 없으면 전체")이 이 규칙의 핵심이다. 깨끗한 트리에서 돌려도
    아무 일도 안 하는 대신 전체를 본다 — 처음 받은 저장소에서도 한 번에 맞춰진다.
    """
    if args.files:
        return [Path(name).resolve() for name in args.files if Path(name).is_file()]

    if args.all:
        return collectSourceFiles(getLintSearchDirs(repositoryRoot))

    listModified = getModifiedCppFiles(repositoryRoot)
    if listModified:
        print(f"[{tag}] Git 변경 파일 {len(listModified)}개 감지.", file=sys.stderr)
        return listModified

    listAll = collectSourceFiles(getLintSearchDirs(repositoryRoot))
    print(f"[{tag}] 변경된 파일이 없어 전체 {len(listAll)}개 파일 대상 실행.", file=sys.stderr)
    return listAll


@dataclass(frozen=True)
class FixPass:
    """
    텍스트 변환 한 가지.

    - `transform` : `(텍스트) -> (새 텍스트, 바뀌었는가)`
    - `problem`   : `--check` 에서 이 변환이 걸렸을 때 할 말 (무엇이 어긋났는가)
    - `done`      : 실제로 고쳤을 때 할 말
    - `badSample` : 이 변환이 **반드시 고쳐야 하는** 조각
    - `goodSample`: 이 변환이 **건드리면 안 되는** 조각

    픽서가 변환을 여럿 들면 **선언 순서대로** 이어 돌린다. 순서가 의미를 갖는 경우가 있어
    (`FormatBranchBraces` 는 if 를 먼저 벗겨야 case 의 문장 수가 부풀지 않는다) 목록 순서가
    곧 계약이다.

    **조각 둘은 변환과 같은 자리에 있다.** 게이트가 `selfTestCases` 를 들고 다니는 것과 같은 이유다 —
    규칙과 그 증거가 떨어져 있으면 언제든 어긋난다. `CheckFixersAreAlive` 가 읽어 간다.
    `goodSample` 이 특히 중요하다: 픽서는 **파일을 고쳐 쓰므로**, 잡아선 안 될 것을 잡으면
    게이트처럼 "빨간 줄" 이 뜨는 게 아니라 소스가 조용히 바뀐다.
    """

    transform: Callable[[str], tuple[str, bool]]
    problem: str
    done: str
    badSample: str = ""
    goodSample: str = ""


class LintFixer:
    """
    파일을 고쳐 쓰는 린트 하나. 상속해서 `listPass` 만 적으면 나머지는 기반이 준다.

    - `name`       : 클래스 이름에서 `Fixer` 를 뗀 것. CLI 이름이다.
    - `tag`        : 메시지 앞의 `[태그]`. 비우면 `name`.
    - `description`: `--help` 한 줄.
    - `listPass`   : 이 픽서가 하는 변환들 (선언 순서대로 돈다).
    """

    name: str = ""
    tag: str = ""
    description: str = ""
    listPass: tuple[FixPass, ...] = ()

    def __init_subclass__(cls, **kwargs) -> None:
        super().__init_subclass__(**kwargs)
        if not cls.name:
            cls.name = cls.__name__.removesuffix("Fixer")
        if not cls.tag:
            cls.tag = cls.name

    # --- 파일 하나 ---------------------------------------------------------------

    def processFile(self, filePath: Path, checkOnly: bool = False) -> list[str]:
        """
        파일 하나를 검사하거나 고칩니다. 걸린 것이 있으면 메시지 목록을 돌려줍니다.

        줄끝은 건드리지 않는다(`newline=""`) — 포맷터가 CRLF 를 LF 로 바꿔 놓으면 그 파일 전체가
        바뀐 것으로 보이고, 진짜 변경이 그 안에 묻힌다.
        """
        try:
            with filePath.open("r", encoding="utf-8", errors="ignore", newline="") as file:
                content = file.read()
        except Exception as exception:
            return [f"[{self.tag}] {filePath} 읽기 실패: {exception}"]

        formatted = content
        listHit: list[FixPass] = []
        for fixPass in self.listPass:
            formatted, bChanged = fixPass.transform(formatted)
            if bChanged:
                listHit.append(fixPass)

        if not listHit:
            return []

        if checkOnly:
            return [f"[{self.tag}] {filePath}: {hit.problem}" for hit in listHit]

        try:
            with filePath.open("w", encoding="utf-8", newline="") as file:
                file.write(formatted)
        except Exception as exception:
            return [f"[{self.tag}] {filePath} 쓰기 실패: {exception}"]

        return [f"[{self.tag}] {filePath}: {hit.done}" for hit in listHit]

    def processFiles(
        self,
        files: Sequence[Path],
        checkOnly: bool = False,
        maxWorkers: int | None = None,
    ) -> list[str]:
        """여러 파일을 동시에 처리합니다 (워커 수 정책은 `common.Parallel`)."""
        return flatMapConcurrent(
            lambda path: self.processFile(path, checkOnly), list(files), workerCount=maxWorkers
        )

    # --- 진입점 ------------------------------------------------------------------

    @classmethod
    def run(cls, argv: Sequence[str] | None = None) -> int:
        """각 픽서 모듈은 `main = XxxFixer.run` 한 줄로 이것을 내보냅니다."""
        return cls().main(argv)

    def main(self, argv: Sequence[str] | None = None) -> int:
        useUtf8Stdout()


        parser = argparse.ArgumentParser(description=self.description)
        addFileArguments(parser)
        parser.add_argument("--check", action="store_true", help="파일을 수정하지 않고 규칙 위반 여부만 검사")
        args = parser.parse_args(argv)

        listFile = selectTargetFiles(args, getProjectRoot(), self.tag)
        if not listFile:
            print(f"[{self.tag}] 대상 C++ 파일이 없습니다.", file=sys.stderr)
            return 0

        listMessage = self.processFiles(listFile, checkOnly=args.check)
        for message in listMessage:
            print(message)

        return 1 if (args.check and listMessage) else 0


def findFixerClass(module: ModuleType) -> type[LintFixer] | None:
    """모듈 안에 정의된 픽서 클래스를 찾습니다 (한 파일에 픽서 하나). `findGateClass` 의 짝입니다."""
    for value in vars(module).values():
        if isinstance(value, type) and issubclass(value, LintFixer) and value is not LintFixer:
            if value.__module__ == module.__name__:
                return value
    return None
