#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
지금 이 트리에서 **혼자 서지 못하는 헤더**를 전부 묻는다.

[왜 필요한가 — 혼자 서지 못하는 헤더는 남의 사정에 기대어 컴파일된다]
헤더가 자기가 쓰는 이름의 선언을 직접 include 하지 않아도, 그것을 먼저 include 해 준 다른 헤더가
있으면 빌드는 통과한다. 그러다 그 "다른 헤더" 가 정리되는 날 **내 코드를 한 줄도 안 고쳤는데**
빌드가 깨진다. 그리고 그 깨짐은 늘 엉뚱한 파일에서 난다.

[강제 include 는 얇아야 이 검사가 뜻이 있다]
`ReflectionParser` 가 만드는 `FlagOps.gen.h` 는 `/FI` 로 타깃 **전 TU** 에 강제 include 된다. 그것이 무엇이든
include 하면 그 이름은 어디서나 "이미 있는" 것이 되어 누락이 보이지 않고, 그 내용은 "플래그 열거형을 가진 헤더가
무엇이냐" 에 따라 바뀌므로 **오늘 서는 헤더가 내 코드를 안 고쳐도 내일 못 선다.** 그래서 `FlagOps.gen.h` 와 그것이
include 하는 `*.gen.h` 는 불투명 열거형 전방 선언과 `IsBitFlagEnum` 특수화만 든다(`Core/Common/BitFlagTrait.h` —
`<type_traits>` 만 — 밖에는 include 하지 않는다).

[게이트가 아니다 — 정기 실행과 커밋 훅이 나눠 막는다]
헤더 하나에 컴파일러를 한 번씩 부르므로 전 트리가 3~10 분이다. 린트 스위트(30 초)에 얹지 않는다. 대신
CI 의 `header-self-contained` 워크플로가 하루 한 번 `--fail-on-violation` 으로 트리 전체를 보고, 커밋 훅은 빌드 폴더가 있을 때
staged 헤더만 본다(`gate/CheckHeaderSelfContained.py`). 판정 · 플래그 고르기는 `common/HeaderSelfContained.py` 한 자리다.

[언제 쓰나 — 매번은 아니다]
한 폴더를 훑어 끝냈을 때, 헤더를 여럿 옮기거나 include 를 정리한 뒤, 남의 커밋을 받은 뒤에 돌린다.

사용법:
  py -3 Scripts/lint/report/RunHeaderSelfContained.py                       # Source/ 전부
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --filter Engine/Graphics
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --files Source/Engine/Scene/Scene.h
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --preset Ninja-Shipping --jobs 8
  py -3 Scripts/lint/report/RunHeaderSelfContained.py --preset CI-Debug --fail-on-violation   # CI 정기 잡
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from LintReport import LintReport, ReportContext  # noqa: E402
from common.HeaderSelfContained import (collectCheckedHeaders, findHeaderProbeProblem, findHeadersNotSelfContained,  # noqa: E402
                                        isCheckedHeader)


class RunHeaderSelfContainedReport(LintReport):
    description = "혼자 서지 못하는 헤더를 보고한다 (게이트 아님)."
    bUsesBuildTree = True
    bUsesJobs = True
    bUsesFilter = True

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=None, help="검사할 헤더 경로 목록")
        parser.add_argument("--fail-on-violation", action="store_true",
                            help="서지 못하는 헤더가 있으면 1, 검사할 수 없으면 2 로 끝낸다 (CI 정기 잡)")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
        buildDir = context.buildTree.path

        problem = findHeaderProbeProblem(buildDir)
        if problem:
            print(f"[HeaderSelfContained] {problem}", file=sys.stderr)
            # 보고로 돌 때는 막지 않는다 — 돌 수 없으면 그렇다고 말하고 끝낸다. CI 는 "아무것도 안 봤다" 를 통과로 읽으면 안 된다.
            return 2 if args.fail_on_violation else 0

        if args.files:
            listHeader = [Path(p) if Path(p).is_absolute() else repositoryRoot / p for p in args.files]
            listHeader = [p for p in listHeader if p.is_file() and isCheckedHeader(p)]
        else:
            listHeader = collectCheckedHeaders(repositoryRoot, args.filter)

        if not listHeader:
            print("[HeaderSelfContained] 검사할 헤더가 없습니다.")
            return 2 if args.fail_on_violation else 0

        print(f"[HeaderSelfContained] 헤더 {len(listHeader)}개를 단독 컴파일합니다 …")
        with tempfile.TemporaryDirectory(prefix="swHeaderProbe") as probeDirName:
            listFailure = findHeadersNotSelfContained(repositoryRoot, buildDir, listHeader, Path(probeDirName),
                                                      workerCount=context.jobs)

        if not listFailure:
            print(f"[HeaderSelfContained] OK — 헤더 {len(listHeader)}개가 전부 혼자 섭니다.")
            return 0

        print(f"\n[HeaderSelfContained] 혼자 서지 못하는 헤더 {len(listFailure)}개:\n")
        for spelling, reason in listFailure:
            print(f"  {spelling}\n      {reason}")
        print("\n  그 헤더가 직접 쓰는 이름의 선언을 그 헤더가 직접 include 하세요.")
        print("  지금 컴파일되는 것은 남이 먼저 include 해 준 덕이고, 그 남이 바뀌면 깨집니다.")
        return 1 if args.fail_on_violation else 0


main = RunHeaderSelfContainedReport.run


if __name__ == "__main__":
    sys.exit(main())
