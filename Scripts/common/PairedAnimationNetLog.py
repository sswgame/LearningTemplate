#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file PairedAnimationNetLog.py
@brief 페어드 애니메이션 복제 사건 줄(`[PairedAnimationNet]`)을 읽고 서버 · 클라이언트 로그를 맞대는 공용 코드입니다.

줄 형식은 `PairedAnimationReplication.cpp` 의 `formatEventLine` 이 정한다:
`[PairedAnimationNet] side=<server|client> event=<start|join|end|restore> session=<번호> anchor=<앵커 시각 초>`.

- 서버: `start` 새 세션을 모두에게 보냈다 · `join` 늦게 붙은 연결에 재생 중인 세션을 보냈다 · `end` 끝 메시지를 보냈다.
- 클라이언트: `restore` 상태를 이어 받아 세션을 복원했다 · `end` 끝 메시지를 받았다.

`Scripts/dev/RunPairedAnimationNetPair.py` 가 프로세스 둘을 띄워 맞댄다.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

_kEventPattern = re.compile(r"\[PairedAnimationNet\] side=(server|client) event=(start|join|end|restore) session=(\d+) anchor=(\S+)")
#: 같은 상태의 앵커 시각은 같은 float32 가 한쪽에서 쓰이고 다른 쪽에서 읽힌 값이라 글자는 같다 — 그래도 서식 차이에 걸리지 않게 숫자로 견준다.
kAnchorTolerance = 1e-4


@dataclass(frozen=True)
class NetEvent:
    """로그 한 줄의 사건 — (어느 쪽, 사건, 세션 번호, 앵커 시각)."""

    side: str
    event: str
    sessionID: int
    anchor: float


@dataclass
class NetEventLog:
    """로그 하나에서 읽은 사건들(로그 순서)."""

    listEvent: list[NetEvent] = field(default_factory=list)

    def listOf(self, event: str) -> list[NetEvent]:
        return [entry for entry in self.listEvent if entry.event == event]

    def setSessionOf(self, event: str) -> set[int]:
        return {entry.sessionID for entry in self.listEvent if entry.event == event}


def parseEventLog(text: str, expectedSide: str) -> NetEventLog:
    """글에서 사건 줄을 읽는다. 기대한 쪽(`server` · `client`)이 아닌 줄이 섞이면 ValueError 다(서버 로그에 클라이언트 줄이 있다는 것은 두 프로세스가 섞인 것이다)."""
    log = NetEventLog()
    for line in text.splitlines():
        match = _kEventPattern.search(line)
        if match is None:
            continue
        side = match.group(1)
        if side != expectedSide:
            raise ValueError(f"the {expectedSide} log has a {side} line: {line.strip()}")
        log.listEvent.append(NetEvent(side, match.group(2), int(match.group(3)), float(match.group(4))))
    return log


def hasAnchor(listEvent: list[NetEvent], sessionID: int, anchor: float) -> bool:
    return any(entry.sessionID == sessionID and abs(entry.anchor - anchor) <= kAnchorTolerance for entry in listEvent)


def compareEventLogs(server: NetEventLog, client: NetEventLog) -> tuple[dict[str, int], list[str]]:
    """세션 시작 · 늦은 참가 · 끝이 두 로그에서 맞물리는지 보고 (센 값 표, 문제 목록)을 돌려준다. 문제가 없어야 같다.

    - 서버에 시작 · 늦은 참가 · 끝이 각각 하나 이상 있어야 한다(확인이 그 경로를 지나갔다).
    - 클라이언트의 복원은 모두 서버가 보낸 (세션, 앵커)여야 한다 — 서버가 보낸 상태를 그대로 이어 받았다.
    - 늦은 참가: 서버가 `join` 으로 보낸 세션을 클라이언트가 복원했다.
    - 세션 시작 복제: 서버가 `join` 으로 보내지 않은 `start` 세션을 클라이언트가 복원했다(붙은 뒤에 시작한 세션).
    - 끝: 클라이언트가 받은 끝은 모두 서버가 보낸 끝이고, 하나 이상 받았다.
    """
    listProblem: list[str] = []
    serverSent = [entry for entry in server.listEvent if entry.event in ("start", "join")]
    restoreList = client.listOf("restore")
    startIDs, joinIDs, serverEndIDs = server.setSessionOf("start"), server.setSessionOf("join"), server.setSessionOf("end")
    mapCount = {
        "serverStart": len(server.listOf("start")), "serverJoin": len(server.listOf("join")), "serverEnd": len(server.listOf("end")),
        "clientRestore": len(restoreList), "clientEnd": len(client.listOf("end")),
    }
    for label, key in (("session start", "serverStart"), ("late join", "serverJoin"), ("session end", "serverEnd")):
        if mapCount[key] == 0:
            listProblem.append(f"the server log has no '{label}' line - the check never reached that path")
    for entry in restoreList:
        if not hasAnchor(serverSent, entry.sessionID, entry.anchor):
            listProblem.append(f"client restored session {entry.sessionID} at anchor {entry.anchor}, which the server never sent")
    lateRestored = [entry for entry in restoreList if entry.sessionID in joinIDs]
    if mapCount["serverJoin"] > 0 and not lateRestored:
        listProblem.append(f"the server sent a late join for session(s) {sorted(joinIDs)} but the client restored none of them")
    startRestored = [entry for entry in restoreList if entry.sessionID in startIDs and entry.sessionID not in joinIDs]
    if mapCount["serverStart"] > 0 and not startRestored:
        listProblem.append("the client restored no session that started after it joined - session start was not replicated")
    for entry in client.listOf("end"):
        if entry.sessionID not in serverEndIDs:
            listProblem.append(f"client got an end for session {entry.sessionID}, which the server never ended")
    if mapCount["serverEnd"] > 0 and mapCount["clientEnd"] == 0:
        listProblem.append("the client got no end message")
    return mapCount, listProblem
