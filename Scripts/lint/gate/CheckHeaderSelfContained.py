#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckHeaderSelfContained.py

헤더가 **혼자** 컴파일되는지 본다 — 쓰는 이름의 선언을 남이 먼저 include 해 준 덕에 서는 헤더를 막는다.
판정 · 플래그 고르기는 `common/HeaderSelfContained.py` 한 자리이고, 트리 전체 보고는 `report/RunHeaderSelfContained.py` 다.

- **커밋 훅이 staged 헤더만 본다**(`--files`). 헤더 하나에 컴파일러를 한 번씩 부르므로 전 트리는 3~10 분 — 그래서 CTest 린트로는
  등록하지 않는다(`ctestSkipReason`). 트리 전체는 CI 의 `header-self-contained` 워크플로가 하루 한 번 본다.
- **빌드된 컴파일 DB 가 어딘가에 있을 때만 돈다.** 보는 폴더는 `--preset` · `--build-dir`(기본 `build/Ninja-Debug`)이고, 작업자 워크트리처럼
  자기 폴더에 없으면 메인 저장소의 같은 폴더를 빌려 소스 쪽 경로만 워크트리로 옮긴다(`CompileDatabaseRehome`). 어디에도 없거나 코드젠이 아직
  돌지 않았으면(`FlagOps.gen.h` 가 자리 표시자) 헤더를 하나도 검사하지 못한 것이다 — **불성립(`UNAVAILABLE`, 종료 5)** 으로 끝난다. 통과가 아니다:
  커밋 훅은 빌드하지 않은 PC 의 커밋을 막지 않도록 허용하되 요약의 `불성립(허용)` 칸과 사유 한 줄로 보이고, 통합에서는 실패다(`GateVerdict.py`).
- 디스크의 헤더를 컴파일한다(staged 내용이 아니라). 부분 staging 한 헤더는 작업 트리 내용으로 판정된다.

  python Scripts/lint/gate/CheckHeaderSelfContained.py [--root <repo>] [--preset <이름> | --build-dir <dir>] [--files a.h b.h]
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.BuildTree import addBuildTreeArguments, BuildTree  # noqa: E402
from common.HeaderSelfContained import (findHeadersNotSelfContained, isCheckedHeader, kHeaderScanRoot,  # noqa: E402
                                        selectProbeBuildDir)
from common.ExitCode import ExitCode  # noqa: E402
from common.Worktree import findMainRoot  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402


class CheckHeaderSelfContainedGate(LintGate):
    """staged 헤더의 단독 컴파일 — 빌드 폴더가 있을 때만."""

    description = "헤더가 혼자 컴파일되는지 검사 (빌드 폴더의 컴파일 DB 플래그로 -fsyntax-only)"
    buildComment = "Checking that headers compile on their own..."
    timeoutSeconds = 900
    preCommitPattern = (f"{kHeaderScanRoot}/*.h",)
    preCommitFileArgument = "--files"
    ctestSkipReason = "전 트리 3~10 분 — CI 의 header-self-contained 워크플로가 하루 한 번 RunHeaderSelfContained --fail-on-violation 으로 본다"
    # 컴파일해 위반을 잡는 경로는 빌드된 컴파일 DB · 컴파일러 · 생성 헤더가 있어야 돈다 — 플래그 고르기는 Test/PythonTest/TestHeaderSelfContained.py 가 본다.
    # 여기 조각은 빌드가 없을 때 이 게이트가 조용히 0 으로 끝나지 않고 불성립(5)을 내는지 본다.
    selfTestCases = [
        {
            "name": "빌드 폴더가 없으면 불성립(통과가 아니다)",
            "expectedViolation": "UNAVAILABLE (빌드 폴더 준비 안 됨 — 컴파일 DB 가 없습니다",
            "expectedExitCode": ExitCode.PrerequisiteMissing,
            "files": {"Source/Probe.h": "#pragma once\nint probe();\n"},
            "args": ["--build-dir", "build", "--files", "Source/Probe.h"],
            "git": True,
        },
    ]
    violationHeader = "혼자 서지 못하는 헤더"
    hint = (
        "  그 헤더가 직접 쓰는 이름의 선언을 그 헤더가 직접 include 합니다(또는 전방 선언).\n"
        "  지금 다른 데서 컴파일되는 것은 남이 먼저 include 해 준 덕이고, 그 남이 바뀌면 엉뚱한 파일에서 깨집니다."
    )

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)
        addBuildTreeArguments(parser)

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        # 작업자 워크트리에는 빌드 폴더가 없다 — 자기 폴더에 DB 가 없으면 메인 저장소의 컴파일 DB 를 빌려 오고 소스 쪽 경로만 옮긴다.
        try:
            mainRoot = findMainRoot(repositoryRoot)
        except RuntimeError as error:
            raise GateError(str(error)) from error
        buildDir, problem = selectProbeBuildDir([BuildTree.fromArguments(args, repositoryRoot).path,
                                                 BuildTree.fromArguments(args, mainRoot).path])
        if problem:
            # 빌드 폴더가 없으면 헤더를 하나도 검사하지 못했다 — 통과가 아니라 불성립이다(종료 5). 통합에서는 실패이고 훅(빌드 없는 PC)에서만 허용된다.
            return GateResult.unavailable(f"빌드 폴더 준비 안 됨 — {problem}")

        listPath = self.selectTargetFiles(repositoryRoot, args.files, bMustFind=True, listScanRoot=(kHeaderScanRoot,), suffixes=(".h",))
        listHeader = [path for path in listPath if isCheckedHeader(path)]
        if not listHeader:
            return GateResult.skipped("검사 대상 헤더 없음 — 고른 파일이 생성 헤더 · pch.h 뿐이다")
        with tempfile.TemporaryDirectory(prefix="swHeaderGate") as probeDirName:
            listFailure = findHeadersNotSelfContained(repositoryRoot, buildDir, listHeader, Path(probeDirName),
                                                        mainRoot=mainRoot)
        return GateResult(listViolation=[f"{spelling}: {reason}" for spelling, reason in listFailure],
                          summary=f"헤더 {len(listHeader)}개가 혼자 선다")


main = CheckHeaderSelfContainedGate.run

if __name__ == "__main__":
    sys.exit(main())
