#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/fixer/FormatIncludeOrder.py

include 순서 · 중복을 고친다 — 규칙 · 판정은 `Scripts/lint/IncludeOrderRules.py` 한 자리이고, 같은 판정이 게이트 `CheckIncludeOrder` 의 검사다.
파일을 고쳐 쓰되 줄 끝 · BOM 은 건드리지 않는다.

사용법:
  py -3 Scripts/lint/fixer/FormatIncludeOrder.py                    # 변경된 파일(없으면 전체)
  py -3 Scripts/lint/fixer/FormatIncludeOrder.py --files a.cpp ...  # 지정한 파일만
  py -3 Scripts/lint/fixer/FormatIncludeOrder.py --all              # 프로젝트 전체
  py -3 Scripts/lint/fixer/FormatIncludeOrder.py --check            # 고치지 않고 위반 여부만(위반이면 1)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from lint.Parallel import flatMapConcurrent  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from lint import IncludeOrderRules  # noqa: E402
from lint.LintFixer import addFileArguments, selectFixerTargetFiles  # noqa: E402

kTag = "FormatIncludeOrder"


class FormatIncludeOrderFixer:
    """include 순서 픽서 — 파일 하나를 읽어 `IncludeOrderRules.fixText` 로 고친다."""

    def processFile(self, filePath: Path, checkOnly: bool = False) -> list[str]:
        try:
            relativePath = filePath.resolve().relative_to(getProjectRoot().resolve()).as_posix()
        except ValueError:
            relativePath = filePath.as_posix()
        try:
            with filePath.open("r", encoding="utf-8", errors="strict", newline="") as file:
                content = file.read()
        except UnicodeDecodeError as error:
            # 고쳐 쓰면 UTF-8 이 아닌 바이트가 사라진다 — 읽지 못한 파일은 손대지 않고 알린다.
            return [f"[{kTag}] {filePath}: UTF-8 이 아니라 건너뜁니다({error.reason}, 바이트 {error.start}) — 파일을 UTF-8 로 저장하십시오"]
        except OSError as error:
            return [f"[{kTag}] {filePath} 읽기 실패: {error}"]

        formatted, bChanged = IncludeOrderRules.fixText(content, relativePath)
        if not bChanged:
            return []
        if checkOnly:
            return [f"[{kTag}] {filePath}: include 순서 · 중복이 규칙과 다르다"]
        try:
            with filePath.open("w", encoding="utf-8", newline="") as file:
                file.write(formatted)
        except OSError as error:
            return [f"[{kTag}] {filePath} 쓰기 실패: {error}"]
        return [f"[{kTag}] {filePath}: include 순서를 고쳤다"]

    def processFiles(self, files: Sequence[Path], checkOnly: bool = False, maxWorkers: int | None = None) -> list[str]:
        return flatMapConcurrent(lambda path: self.processFile(path, checkOnly), list(files), workerCount=maxWorkers)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="include 순서 · 중복을 고친다(IncludeOrderRules 의 규칙)")
    addFileArguments(parser)
    parser.add_argument("--check", action="store_true", help="파일을 수정하지 않고 규칙 위반 여부만 검사")
    args = parser.parse_args(argv)

    listFile = selectFixerTargetFiles(args, getProjectRoot(), kTag)
    if not listFile:
        print(f"[{kTag}] 대상 파일이 없습니다.", file=sys.stderr)
        return ExitCode.Passed
    listMessage = FormatIncludeOrderFixer().processFiles(listFile, checkOnly=args.check)
    for message in listMessage:
        print(message)
    return ExitCode.Violation if (args.check and listMessage) else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
