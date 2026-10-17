#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev.CompareDestructionHashLogs — 겹치는 (번호, 사건 수)의 해시가 같고 마지막이 같으면 0, 다르거나 비교할 것이 없으면 1, 서버 · 클라이언트 줄이 섞이면 거절."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from dev import CompareDestructionHashLogs as compare  # noqa: E402

_kServerLog = """[Info] Destruction net session started as server with 3 objects
[Info] [DestructionHash] side=server id=1 events=1 hash=00000000000000aa name=BarrelA
[Info] [DestructionHash] side=server id=1 events=3 hash=00000000000000ab name=BarrelA
[Info] [DestructionHash] side=server id=2 events=2 hash=00000000000000ba name=BrickWall
"""


class CompareDestructionHashLogsTest(unittest.TestCase):
    def testSharedKeysAndLastLinesMatch(self) -> None:
        # 클라이언트는 스냅숏으로 사건 1 을 건너뛰었다 — 겹치는 열쇠만 본다.
        client = compare.parseHashLog("[DestructionHash] side=client id=1 events=3 hash=00000000000000AB name=BarrelA\n"
                                      "[DestructionHash] side=client id=2 events=2 hash=00000000000000ba name=BrickWall\n")
        sharedCount, listProblem = compare.compareHashLogs(compare.parseHashLog(_kServerLog), client)
        self.assertEqual(2, sharedCount)
        self.assertEqual([], listProblem)

    def testDifferentHashAtTheSameKeyIsAProblem(self) -> None:
        client = compare.parseHashLog("[DestructionHash] side=client id=1 events=3 hash=00000000000000ff name=BarrelA\n"
                                      "[DestructionHash] side=client id=2 events=2 hash=00000000000000ba name=BrickWall\n")
        _, listProblem = compare.compareHashLogs(compare.parseHashLog(_kServerLog), client)
        self.assertTrue(any("id=1 events=3" in problem for problem in listProblem))

    def testClientThatStoppedEarlyOrComparedNothingFails(self) -> None:
        stopped = compare.parseHashLog("[DestructionHash] side=client id=1 events=1 hash=00000000000000aa name=BarrelA\n")
        _, listProblem = compare.compareHashLogs(compare.parseHashLog(_kServerLog), stopped)
        self.assertTrue(any("id=1: server ended" in problem for problem in listProblem))
        self.assertTrue(any("id=2: the client never logged it" in problem for problem in listProblem))
        _, listEmptyProblem = compare.compareHashLogs(compare.parseHashLog(_kServerLog), compare.parseHashLog(""))
        self.assertTrue(any("nothing was compared" in problem for problem in listEmptyProblem))

    def testMixedSidesAreRejected(self) -> None:
        with self.assertRaises(ValueError):
            compare.parseHashLog(_kServerLog + "[DestructionHash] side=client id=1 events=1 hash=00000000000000aa name=BarrelA\n")

    def testMainReturnsExitCodes(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            serverPath = Path(folder) / "server.log"
            clientPath = Path(folder) / "client.log"
            serverPath.write_text(_kServerLog, encoding="utf-8")
            clientPath.write_text(_kServerLog.replace("side=server", "side=client"), encoding="utf-8")
            self.assertEqual(0, compare.main([str(serverPath), str(clientPath)]))
            clientPath.write_text(_kServerLog.replace("side=server", "side=client").replace("00ba", "00bb"), encoding="utf-8")
            self.assertEqual(1, compare.main([str(serverPath), str(clientPath)]))
            self.assertEqual(2, compare.main([str(serverPath), str(Path(folder) / "missing.log")]))


if __name__ == "__main__":
    unittest.main()
