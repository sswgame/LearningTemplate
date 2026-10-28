#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunPairedAnimationNetPair.py
@brief 같은 기계에서 전용 서버(`Server`)와 클라이언트(`App`)를 띄워 페어드 애니메이션 세션의 시작 · 늦은 참가 · 끝 복제 로그를 맞댄다(매니페스트 `paired-animation-two-process`).

    py -3 -m Scripts paired-net-pair --preset Ninja-Release
    py -3 -m Scripts paired-net-pair --preset Ninja-Release --ticks 900 --frames 600 --out C:/tmp/pairedpair

씬(`game/empty/maps/pairedanimnet.scene.xml`)의 서버 쪽 시작자(`PairedAnimationNetStarterComponent`)가 세션을 끝날 때마다 다시 시작하므로 서버에는 늘 재생 중인 세션이 있다.
서버를 먼저 띄워 두고 클라이언트를 붙이면 클라이언트는 재생 중인 세션을 늦은 참가로 받고(`join` → `restore`), 이어 서버가 시작하는 세션(`start` → `restore`)과 끝(`end`)을 받는다.
두 프로세스의 `[PairedAnimationNet]` 줄(`common/PairedAnimationNetLog.py`)이 맞물리고 `[Error]` 줄이 없어야 통과한다.
서버를 띄우고 클라이언트를 붙이는 부분은 `common/NetPairRun.py`(`RunDestructionNetPair` 와 같다). 리눅스 서버 ↔ Windows 클라이언트는 CI 잡 몫(결정 기록 5-12)이다.

종료 코드: 0 = 맞물린다, 1 = 사건이 맞물리지 않거나 오류 로그가 있다, 2 = 실행 파일이 없거나 프로세스가 비정상으로 끝났다.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common.BuildTree import addBuildTreeArguments, BuildTree  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.NetPairRun import findLogProblems, hasBadExit, kDefaultPort, findServerExecutable, makeClientArguments, makeServerArguments, runServerClientPair, writeLog  # noqa: E402
from common.PairedAnimationNetLog import compareEventLogs, parseEventLog  # noqa: E402

#: 세션 컴포넌트와 시작자를 둔 씬(`PairedAnimationReplicationTest.NetworkSceneHasTwoRolesStarterAndSessionComponent` 가 컴포넌트를 지킨다).
kDefaultScene = "game/empty/maps/pairedanimnet.scene.xml"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="한 기계에서 Server + App 을 띄워 페어드 애니메이션 세션 복제 로그를 맞댄다")
    addBuildTreeArguments(parser)
    parser.add_argument("--scene", default=kDefaultScene, help="두 프로세스가 여는 씬(기본: 페어드 애니메이션 복제 씬)")
    parser.add_argument("--port", type=int, default=kDefaultPort, help="서버가 듣는 UDP 포트")
    parser.add_argument("--ticks", type=int, default=900, help="서버가 돌 틱 수(-gv_serverExitAfterTicks) — 세션 1.67 초 여럿이 클라이언트가 붙은 뒤에도 돌 만큼")
    parser.add_argument("--frames", type=int, default=600, help="클라이언트가 돌 프레임 수(-gv_profileFrames)")
    parser.add_argument("--timeout", type=float, default=180.0, help="프로세스마다 기다리는 최대 시간(초)")
    parser.add_argument("--out", type=Path, default=None, help="로그 출력 폴더(기본: build/<preset>/pairedanimpair)")
    args = parser.parse_args(argv)

    tree = BuildTree.fromArguments(args)
    appPath = tree.appExecutable
    serverPath = findServerExecutable(tree)
    if appPath is None or serverPath is None:
        print(f"[RunPairedAnimationNetPair] App 또는 Server 실행 파일이 없습니다: {tree.binDir}")
        return ExitCode.CheckInvalid
    outDir = args.out or tree.path / "pairedanimpair"
    outDir.mkdir(parents=True, exist_ok=True)

    serverResult, clientResult = runServerClientPair(serverPath, appPath, makeServerArguments(args.scene, args.port, args.ticks),
                                                     makeClientArguments(args.scene, args.port, args.frames, outDir), args.timeout)
    writeLog(outDir / "server.log", serverResult)
    writeLog(outDir / "client.log", clientResult)
    for name, result in (("server", serverResult), ("client", clientResult)):
        if hasBadExit(result):
            print(f"[RunPairedAnimationNetPair] {name} 가 비정상으로 끝났습니다(종료 코드 {result.exitCode}, 시간 초과 {result.bTimedOut}). 로그: {outDir / (name + '.log')}")
            return ExitCode.CheckInvalid

    serverText, clientText = "\n".join(serverResult.listLine), "\n".join(clientResult.listLine)
    try:
        server = parseEventLog(serverText, "server")
        client = parseEventLog(clientText, "client")
    except ValueError as error:
        print(f"[RunPairedAnimationNetPair] 로그를 읽지 못했습니다: {error}")
        return ExitCode.CheckInvalid
    mapCount, listProblem = compareEventLogs(server, client)
    listProblem = findLogProblems(serverText, "server", "PairedAnimation") + findLogProblems(clientText, "client", "PairedAnimation") + listProblem
    print(f"[RunPairedAnimationNetPair] {' '.join(f'{key}={count}' for key, count in mapCount.items())}, 문제 {len(listProblem)}. 로그: {outDir}")
    for problem in listProblem:
        print(f"  {problem}")
    return ExitCode.Passed if not listProblem else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
