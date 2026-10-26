#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunDestructionNetPair.py
@brief 같은 기계에서 전용 서버(`Server`)와 클라이언트(`App`)를 띄워 파괴 복제 해시 로그를 맞댄다(후보 DestructionNetSessionScene 의 "두 로그 견주기").

    py -3 Scripts/dev/RunDestructionNetPair.py --preset Ninja-Release
    py -3 Scripts/dev/RunDestructionNetPair.py --preset Ninja-Release --ticks 1800 --frames 1800 --out C:/tmp/destructionpair

순서: 서버를 `-gv_netListenPort` 로 띄우고(`-gv_serverExitAfterTicks` 로 스스로 끝난다) 클라이언트를 `-gv_netConnectAddress` 로 붙인 뒤
둘의 출력 줄에서 `[DestructionHash]` 줄을 `CompareDestructionHashLogs` 와 같은 규칙으로 맞댄다. 서버를 먼저 띄워 두므로 클라이언트가 붙기 전의 사건은
스냅숏으로 건너뛴다(겹치는 열쇠만 본다). 리눅스 서버 ↔ Windows 클라이언트는 CI 잡 몫(결정 기록 5-12)이라 이 도구는 한 기계의 두 프로세스만 다룬다.

종료 코드: 0 = 같다, 1 = 해시가 다르거나 비교할 것이 없다, 2 = 실행 파일이 없거나 프로세스가 비정상으로 끝났다.
"""

from __future__ import annotations

import argparse
import sys
import threading
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common.AppRun import AppRunResult, runApp  # noqa: E402
from common.BuildTree import addBuildTreeArguments, BuildTree  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.DestructionHashLog import compareHashLogs, parseHashLog  # noqa: E402

#: 파괴 복제 컴포넌트를 둔 씬(`DestructionShowcaseTest.NetworkSceneAddsTheSessionComponentToTheShowcase` 가 컴포넌트를 지킨다).
kDefaultScene = "game/empty/maps/destructionnet.scene.xml"
kDefaultPort = 7777
#: 서버 기동을 기다리는 시간(초). 클라이언트는 연결 재시도가 있어 길게 잡지 않는다.
kServerHeadStartSeconds = 3.0


def findServerExecutable(tree: BuildTree) -> Path | None:
    """빌드 폴더의 `Server` 실행 파일. 없으면 None."""
    for name in ("Server.exe", "Server"):
        candidate = tree.binDir / name
        if candidate.is_file():
            return candidate
    return None


def writeLog(path: Path, result: AppRunResult) -> None:
    path.write_text("\n".join(result.listLine) + "\n", encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="한 기계에서 Server + App 을 띄워 파괴 복제 해시 로그를 맞댄다")
    addBuildTreeArguments(parser)
    parser.add_argument("--scene", default=kDefaultScene, help="두 프로세스가 여는 씬(기본: 파괴 복제 씬)")
    parser.add_argument("--port", type=int, default=kDefaultPort, help="서버가 듣는 UDP 포트")
    parser.add_argument("--ticks", type=int, default=1800, help="서버가 돌 틱 수(-gv_serverExitAfterTicks)")
    parser.add_argument("--frames", type=int, default=1500, help="클라이언트가 돌 프레임 수(-gv_profileFrames)")
    parser.add_argument("--timeout", type=float, default=180.0, help="프로세스마다 기다리는 최대 시간(초)")
    parser.add_argument("--out", type=Path, default=None, help="로그 출력 폴더(기본: build/<preset>/destructionpair)")
    args = parser.parse_args(argv)

    tree = BuildTree.fromArguments(args)
    appPath = tree.appExecutable
    serverPath = findServerExecutable(tree)
    if appPath is None or serverPath is None:
        print(f"[RunDestructionNetPair] App 또는 Server 실행 파일이 없습니다: {tree.binDir}")
        return ExitCode.CheckInvalid
    outDir = args.out or tree.path / "destructionpair"
    outDir.mkdir(parents=True, exist_ok=True)

    serverArguments = [f"-gv_firstScene={args.scene}", f"-gv_netListenPort={args.port}", f"-gv_serverExitAfterTicks={args.ticks}"]
    clientArguments = [f"-gv_firstScene={args.scene}", f"-gv_netConnectAddress=127.0.0.1:{args.port}", f"-gv_profileFrames={args.frames}"]

    # 서버는 따로 스레드에서 돌리고(끝날 때까지 출력을 읽는다) 잠깐 뒤 클라이언트를 붙인다.
    mapResult: dict[str, AppRunResult] = {}
    serverThread = threading.Thread(target=lambda: mapResult.__setitem__("server", runApp(serverPath, serverArguments, timeoutSeconds=args.timeout)))
    serverThread.start()
    threading.Event().wait(kServerHeadStartSeconds)
    mapResult["client"] = runApp(appPath, clientArguments, timeoutSeconds=args.timeout)
    serverThread.join()

    serverResult, clientResult = mapResult["server"], mapResult["client"]
    writeLog(outDir / "server.log", serverResult)
    writeLog(outDir / "client.log", clientResult)
    for name, result in (("server", serverResult), ("client", clientResult)):
        if result.bLaunched is False or result.bTimedOut or result.exitCode != 0:
            print(f"[RunDestructionNetPair] {name} 가 비정상으로 끝났습니다(종료 코드 {result.exitCode}, 시간 초과 {result.bTimedOut}). 로그: {outDir / (name + '.log')}")
            return ExitCode.CheckInvalid

    try:
        server = parseHashLog("\n".join(serverResult.listLine))
        client = parseHashLog("\n".join(clientResult.listLine))
    except ValueError as error:
        print(f"[RunDestructionNetPair] 로그를 읽지 못했습니다: {error}")
        return ExitCode.CheckInvalid
    if server.side != "server" or client.side != "client":
        print(f"[RunDestructionNetPair] 해시 줄이 없습니다(서버 쪽 '{server.side}', 클라이언트 쪽 '{client.side}'). 로그: {outDir}")
        return ExitCode.Violation
    sharedCount, listProblem = compareHashLogs(server, client)
    print(f"[RunDestructionNetPair] 겹친 열쇠 {sharedCount}, 문제 {len(listProblem)}")
    for problem in listProblem:
        print(f"  {problem}")
    return ExitCode.Passed if not listProblem else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
