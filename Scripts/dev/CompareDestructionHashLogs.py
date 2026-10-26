#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file CompareDestructionHashLogs.py
@brief 서버 로그와 클라이언트 로그의 파괴 구조 해시 줄(`[DestructionHash]`)을 (번호, 사건 수)로 맞대어 같은지 봅니다.

    py -3 Scripts/dev/CompareDestructionHashLogs.py <서버 로그> <클라이언트 로그> [<클라이언트 로그> ...]

줄 형식은 `DestructionNetSession::formatHashLine` 이 정한다:
`[DestructionHash] side=<server|client> id=<번호> events=<사건 수> hash=<16 자리> name=<이름>`.
같은 그래프에 같은 사건을 같은 순서로 적용하면 같은 해시이므로, 다른 컴파일러 · 플랫폼의 서버(예: 리눅스 `Server`)와 클라이언트(Windows `App`)가
같은 (번호, 사건 수)에서 다른 해시를 남기면 결정성이 깨진 것이다. 클라이언트가 스냅숏으로 건너뛴 사건 수는 줄이 없으므로 겹치는 열쇠만 본다.
또 오브젝트마다 클라이언트의 마지막 줄이 서버의 마지막 줄과 같아야 한다(끝까지 따라왔다). 겹치는 열쇠가 하나도 없으면 실패다 — 비교한 것이 없다.
읽기와 맞대기는 `common/DestructionHashLog.py` 에 있다(프로세스 둘을 띄워 맞대는 `RunDestructionNetPair.py` 와 같이 쓴다).

종료 코드: 0 = 같다, 1 = 다르거나 비교할 것이 없다, 2 = 로그를 읽지 못했다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.DestructionHashLog import compareHashLogs, parseHashLog, readLog  # noqa: E402,F401 — 테스트가 이 모듈 이름으로 부른다
from common.ExitCode import ExitCode  # noqa: E402


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Compare [DestructionHash] lines of a server log with client logs")
    parser.add_argument("serverLog", type=Path, help="log of the server process")
    parser.add_argument("clientLog", type=Path, nargs="+", help="logs of the client processes")
    arguments = parser.parse_args(argv)
    try:
        server = parseHashLog(readLog(arguments.serverLog))
        listClient = [(path, parseHashLog(readLog(path))) for path in arguments.clientLog]
    except (OSError, ValueError) as error:
        print(f"[CompareDestructionHashLogs] cannot read the logs: {error}")
        return ExitCode.CheckInvalid
    if server.side != "server":
        print(f"[CompareDestructionHashLogs] {arguments.serverLog} has no server hash lines")
        return ExitCode.Violation
    bSame = True
    for path, client in listClient:
        if client.side != "client":
            print(f"[CompareDestructionHashLogs] {path}: no client hash lines")
            bSame = False
            continue
        sharedCount, listProblem = compareHashLogs(server, client)
        print(f"[CompareDestructionHashLogs] {path}: {sharedCount} shared keys, {len(listProblem)} problems")
        for problem in listProblem:
            print(f"  {problem}")
        bSame = bSame and not listProblem
    return ExitCode.Passed if bSame else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
