#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file NetPairRun.py
@brief 같은 기계에서 전용 서버(`Server`)와 클라이언트(`App`)를 띄워 두 출력을 모으는 공용 코드입니다.

`Scripts/dev/RunDestructionNetPair.py`(파괴 복제 해시 로그)와 `Scripts/dev/RunPairedAnimationNetPair.py`(페어드 애니메이션 세션 로그)가 같이 쓴다. 순서는 같다:
서버를 `-gv_netListenPort` 로 띄우고(`-gv_serverExitAfterTicks` 로 스스로 끝난다) 잠깐 뒤 클라이언트를 `-gv_netConnectAddress` 로 붙인다. 서버를 먼저 띄워 두므로
클라이언트가 붙기 전의 사건은 서버가 늦은 참가 스냅숏으로 넘긴다. 로그를 어떻게 맞댈지는 부르는 쪽이 정한다.
"""

from __future__ import annotations

import threading
from pathlib import Path

from .AppRun import AppRunResult, runApp
from .BuildTree import BuildTree

#: 서버 기동을 기다리는 시간(초). 클라이언트는 연결 재시도가 있어 길게 잡지 않는다.
kServerHeadStartSeconds = 3.0
kDefaultPort = 7777


def findServerExecutable(tree: BuildTree) -> Path | None:
    """빌드 폴더의 `Server` 실행 파일. 없으면 None."""
    for name in ("Server.exe", "Server"):
        candidate = tree.binDir / name
        if candidate.is_file():
            return candidate
    return None


def writeLog(path: Path, result: AppRunResult) -> None:
    path.write_text("\n".join(result.listLine) + "\n", encoding="utf-8")


def makeServerArguments(scene: str, port: int, ticks: int) -> list[str]:
    return [f"-gv_firstScene={scene}", f"-gv_netListenPort={port}", f"-gv_serverExitAfterTicks={ticks}"]


def makeClientArguments(scene: str, port: int, frames: int) -> list[str]:
    return [f"-gv_firstScene={scene}", f"-gv_netConnectAddress=127.0.0.1:{port}", f"-gv_profileFrames={frames}"]


def runServerClientPair(serverPath: Path, appPath: Path, serverArguments: list[str], clientArguments: list[str], timeoutSeconds: float) -> tuple[AppRunResult, AppRunResult]:
    """서버를 따로 스레드에서 돌리고(끝날 때까지 출력을 읽는다) 잠깐 뒤 클라이언트를 붙인다. 둘이 모두 끝난 뒤 (서버, 클라이언트) 결과를 돌려준다."""
    mapResult: dict[str, AppRunResult] = {}
    serverThread = threading.Thread(target=lambda: mapResult.__setitem__("server", runApp(serverPath, serverArguments, timeoutSeconds=timeoutSeconds)))
    serverThread.start()
    threading.Event().wait(kServerHeadStartSeconds)
    mapResult["client"] = runApp(appPath, clientArguments, timeoutSeconds=timeoutSeconds)
    serverThread.join()
    return mapResult["server"], mapResult["client"]


def hasBadExit(result: AppRunResult) -> bool:
    """프로세스가 뜨지 못했거나 시간 안에 못 끝났거나 종료 코드가 0 이 아니다."""
    return result.bLaunched is False or result.bTimedOut or result.exitCode != 0
