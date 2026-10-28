#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""common.PairedAnimationNetLog — 서버 · 클라이언트 사건 줄이 맞물리면 문제 없음, 늦은 참가 · 시작 복제 · 끝이 빠지거나 서버가 보내지 않은 상태를 복원하면 문제, 섞인 쪽은 거절."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))

from common import PairedAnimationNetLog as netLog  # noqa: E402

# 세션 1 은 클라이언트가 붙기 전에 시작해 늦은 참가로 넘어가고(앵커 0.75), 세션 2 는 붙은 뒤에 시작한다(앵커 0.0166667).
_kServerLog = """[Info] [PairedAnimationNet] side=server event=start session=1 anchor=0.0166667
[Info] [PairedAnimationNet] side=server event=join session=1 anchor=0.75
[Info] [PairedAnimationNet] side=server event=end session=1 anchor=0
[Info] [PairedAnimationNet] side=server event=start session=2 anchor=0.0166667
[Info] [PairedAnimationNet] side=server event=end session=2 anchor=0
"""

_kClientLog = """[Info] [PairedAnimationNet] side=client event=restore session=1 anchor=0.75
[Info] [PairedAnimationNet] side=client event=end session=1 anchor=0
[Info] [PairedAnimationNet] side=client event=restore session=2 anchor=0.0166667
[Info] [PairedAnimationNet] side=client event=end session=2 anchor=0
"""


def compare(serverText: str, clientText: str) -> tuple[dict[str, int], list[str]]:
    return netLog.compareEventLogs(netLog.parseEventLog(serverText, "server"), netLog.parseEventLog(clientText, "client"))


class PairedAnimationNetLogTest(unittest.TestCase):
    def testMatchingLogsHaveNoProblem(self) -> None:
        mapCount, listProblem = compare(_kServerLog, _kClientLog)
        self.assertEqual([], listProblem)
        self.assertEqual({"serverStart": 2, "serverJoin": 1, "serverEnd": 2, "clientRestore": 2, "clientEnd": 2}, mapCount)

    def testServerWithoutLateJoinOrEndIsAProblem(self) -> None:
        noJoin = "\n".join(line for line in _kServerLog.splitlines() if "event=join" not in line)
        _, listProblem = compare(noJoin, _kClientLog)
        self.assertTrue(any("late join" in problem for problem in listProblem))
        noEnd = "\n".join(line for line in _kServerLog.splitlines() if "event=end" not in line)
        _, listEndProblem = compare(noEnd, _kClientLog)
        self.assertTrue(any("session end" in problem for problem in listEndProblem))
        self.assertTrue(any("never ended" in problem for problem in listEndProblem))

    def testClientThatSkippedTheLateJoinOrTheStartFails(self) -> None:
        onlyStarted = "\n".join(line for line in _kClientLog.splitlines() if "session=1" not in line)
        _, listProblem = compare(_kServerLog, onlyStarted)
        self.assertTrue(any("restored none" in problem for problem in listProblem))
        onlyJoined = "\n".join(line for line in _kClientLog.splitlines() if "session=2" not in line)
        _, listJoinProblem = compare(_kServerLog, onlyJoined)
        self.assertTrue(any("session start was not replicated" in problem for problem in listJoinProblem))

    def testRestoreOfAStateTheServerNeverSentIsAProblem(self) -> None:
        wrongAnchor = _kClientLog.replace("anchor=0.75", "anchor=0.9")
        _, listProblem = compare(_kServerLog, wrongAnchor)
        self.assertTrue(any("never sent" in problem for problem in listProblem))

    def testClientWithoutEndMessageFails(self) -> None:
        noEnd = "\n".join(line for line in _kClientLog.splitlines() if "event=end" not in line)
        _, listProblem = compare(_kServerLog, noEnd)
        self.assertTrue(any("no end message" in problem for problem in listProblem))

    def testMixedSidesAreRejected(self) -> None:
        with self.assertRaises(ValueError):
            netLog.parseEventLog(_kServerLog + _kClientLog, "server")

    def testLogProblemsAreErrorsAndThisFeaturesWarnings(self) -> None:
        text = "[Info] ok\n[Error] boom\n[Warning] other thing\n[Warning] PairedAnimationReplication: rejected\n"
        listProblem = netLog.findLogProblems(text, "server")
        self.assertEqual(2, len(listProblem))
        self.assertTrue(listProblem[0].startswith("server: [Error]"))


if __name__ == "__main__":
    unittest.main()
