#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckHeaderSelfContained.py

헤더가 **혼자** 컴파일되는지 본다 — 쓰는 이름의 선언을 남이 먼저 include 해 준 덕에 서는 헤더를 막는다.
판정 · 플래그 고르기는 `common/HeaderSelfContained.py` 한 자리이고, 트리 전체 보고는 `report/RunHeaderSelfContained.py` 다.

- **커밋 훅이 staged 헤더만 본다**(`--files`). 헤더 하나에 컴파일러를 한 번씩 부르므로 전 트리는 3~10 분 — 그래서 CTest 린트로는
  등록하지 않는다(`ctestSkipReason`). 트리 전체는 CI 의 `header-self-contained` 워크플로가 하루 한 번 본다.
- **빌드 폴더가 있을 때만 돈다.** 컴파일 DB 가 없거나 코드젠이 아직 돌지 않은 폴더(`FlagOps.gen.h` 가 자리 표시자)면 참고 한 줄을 남기고
  통과한다 — 빌드하지 않은 PC 의 커밋을 막지 않는다. 보는 폴더는 `--preset` · `--build-dir`(기본 `build/Ninja-Debug`).
- 디스크의 헤더를 컴파일한다(staged 내용이 아니라). 부분 staging 한 헤더는 작업 트리 내용으로 판정된다.

  python Scripts/lint/gate/CheckHeaderSelfContained.py [--root <repo>] [--preset <이름> | --build-dir <dir>] [--files a.h b.h]
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import BuildTree, addBuildTreeArguments  # noqa: E402
from common.HeaderSelfContained import (findHeaderProbeProblem, findHeadersNotSelfContained, isCheckedHeader,  # noqa: E402
                                        kHeaderScanRoot)
from LintGate import GateResult, LintGate  # noqa: E402


class CheckHeaderSelfContainedGate(LintGate):
    """staged 헤더의 단독 컴파일 — 빌드 폴더가 있을 때만."""

    description = "헤더가 혼자 컴파일되는지 검사 (빌드 폴더의 컴파일 DB 플래그로 -fsyntax-only)"
    buildComment = "Checking that headers compile on their own..."
    timeoutSeconds = 900
    preCommitPattern = (f"{kHeaderScanRoot}/*.h",)
    preCommitFileArgument = "--files"
    ctestSkipReason = "전 트리 3~10 분 — CI 의 header-self-contained 워크플로가 하루 한 번 RunHeaderSelfContained --fail-on-violation 으로 본다"
    selfTestSkipReason = "컴파일 DB 와 그 빌드의 컴파일러 · 생성 헤더가 있어야 컴파일할 수 있다 — 플래그 고르기는 Test/PythonTest/TestHeaderSelfContained.py 가 본다"
    violationHeader = "혼자 서지 못하는 헤더"
    hint = (
        "  그 헤더가 직접 쓰는 이름의 선언을 그 헤더가 직접 include 합니다(또는 전방 선언).\n"
        "  지금 다른 데서 컴파일되는 것은 남이 먼저 include 해 준 덕이고, 그 남이 바뀌면 엉뚱한 파일에서 깨집니다."
    )

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        addBuildTreeArguments(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        buildDir = BuildTree.fromArguments(args, repositoryRoot).path
        problem = findHeaderProbeProblem(buildDir)
        if problem:
            return GateResult(listNote=[f"건너뜀 — {problem}"], summary="빌드 폴더가 없어 보지 않았다")

        listPath = self.selectTargetFiles(repositoryRoot, args.files, listScanRoot=(kHeaderScanRoot,), suffixes=(".h",))
        listHeader = [path for path in listPath if isCheckedHeader(path)]
        with tempfile.TemporaryDirectory(prefix="swHeaderGate") as probeDirName:
            listFailure = findHeadersNotSelfContained(repositoryRoot, buildDir, listHeader, Path(probeDirName))
        return GateResult(listViolation=[f"{spelling}: {reason}" for spelling, reason in listFailure],
                          summary=f"헤더 {len(listHeader)}개가 혼자 선다")


main = CheckHeaderSelfContainedGate.run

if __name__ == "__main__":
    sys.exit(main())
