#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
소스 게이트(`LintGate.sourceScanRoots` 를 채운 `gate/` 의 게이트) 전부를 **한 프로세스, 한 번의 훑기**로 돌립니다 — CTest · CI 린트 잡의 실행 단위.

게이트마다 프로세스를 띄우면 게이트마다 트리를 걷고, 같은 파일을 읽고, 같은 주석 가리기를 되풀이한다. 여기서는 파일을 한 번 읽어
그 파일을 보는 게이트들에 차례로 넘기고(`SourceScan.scanSourceGates`), 파일을 코어 수만큼의 프로세스로 나눈다. 게이트를 더해도
걷기 · 읽기 · 가리기는 늘지 않고 그 게이트의 검사 시간만 는다. 출력은 게이트를 혼자 돌릴 때와 같은 줄이고 게이트 순서(파일 이름순)다.
끝에 게이트마다 `checkSourceFile` 이 쓴 초(프로세스 합)를 찍는다 — 느린 게이트가 함께 도는 묶음 안에 숨지 않게.

종료 코드: 검사가 서지 않은 게이트가 있으면 2, 위반이 있으면 1, 필수 입력이 없어 검사를 못 한(불성립) 게이트가 있으면 5, 아니면 0
(`GateVerdict.mergeExitCodes`). 게이트 하나만 볼 때는 그 게이트를 그대로 돌린다
(`py -3 -m Scripts gate CheckXxx` — 같은 훑기를 게이트 하나로 부른다).

  py -3 Scripts/lint/RunSourceGates.py [--root <repo>] [--gates CheckA,CheckB]
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.Paths import getProjectRoot  # noqa: E402
from common.RuleData import RuleDataError  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from lint.GateVerdict import mergeExitCodes  # noqa: E402
from lint.LintCatalog import discoverSourceGateScripts  # noqa: E402
from lint.LintGate import GateError  # noqa: E402
from lint.SourceScan import scanSourceGates  # noqa: E402

#: 끝에 시간을 찍을 게이트 수.
_kSlowestShown = 5


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="소스 게이트 전부를 한 번의 트리 훑기로 돌린다(CTest · CI 린트 잡의 실행 단위)")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    parser.add_argument("--gates", default="", help="이 게이트만(쉼표로 여럿) — 측정 · 비교용")
    args = parser.parse_args(argv)
    repositoryRoot = (args.root or getProjectRoot()).resolve()
    setOnly = {name for name in args.gates.split(",") if name}

    listScript = [script for script in discoverSourceGateScripts() if not setOnly or script.name in setOnly]
    if setOnly - {script.name for script in listScript}:
        print(f"[RunSourceGates] 소스 게이트가 아닙니다: {', '.join(sorted(setOnly - {script.name for script in listScript}))}", file=sys.stderr)
        return ExitCode.CheckInvalid

    start = time.perf_counter()
    mapReturnCode: dict[str, int] = {}
    listJob = []
    for script in listScript:
        gate = script.gateClass()
        gateArgs, _ = gate.parseArguments(["--root", str(repositoryRoot)])
        returnCode = gate.beginRun()
        if returnCode == 0:
            try:
                gate.prepareSourceScan(repositoryRoot, gateArgs)
            except (GateError, RuleDataError) as exception:
                print(f"[{gate.name}] {exception}", file=sys.stderr)
                returnCode = ExitCode.CheckInvalid
        if returnCode != ExitCode.Passed:
            mapReturnCode[gate.name] = returnCode
            continue
        listJob.append((gate, gateArgs))

    try:
        listResult = scanSourceGates(repositoryRoot, listJob)
    except OSError as exception:
        print(f"[RunSourceGates] {exception}", file=sys.stderr)
        return ExitCode.CheckInvalid

    listTiming: list[tuple[float, str]] = []
    for (gate, gateArgs), result in zip(listJob, listResult):
        listTiming.append((result.seconds, gate.name))
        try:
            gateResult = gate.finishSourceScanChecked(repositoryRoot, gateArgs, result)
        except (GateError, RuleDataError) as exception:
            print(f"[{gate.name}] {exception}", file=sys.stderr)
            mapReturnCode[gate.name] = ExitCode.CheckInvalid
            continue
        mapReturnCode[gate.name] = gate.endRun(repositoryRoot, gateArgs, gateResult)
        sys.stdout.flush()
        sys.stderr.flush()

    listTiming.sort(reverse=True)
    slowest = ", ".join(f"{name} {seconds:.2f} s" for seconds, name in listTiming[:_kSlowestShown])
    print(f"[RunSourceGates] 게이트 {len(listScript)}개 · {time.perf_counter() - start:.1f} s — 검사 시간(프로세스 합) 상위: {slowest}")
    return mergeExitCodes(list(mapReturnCode.values()))


if __name__ == "__main__":
    sys.exit(main())
