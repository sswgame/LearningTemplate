#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file DestructionHashLog.py
@brief 파괴 복제 해시 로그 줄(`[DestructionHash]`)을 읽고 서버 · 클라이언트 로그를 맞대는 공용 코드입니다.

줄 형식은 `DestructionNetSession::formatHashLine` 이 정한다:
`[DestructionHash] side=<server|client> id=<번호> events=<사건 수> hash=<16 자리> name=<이름>`.
`Scripts/dev/CompareDestructionHashLogs.py`(로그 파일 둘을 맞댄다)와 `Scripts/dev/RunDestructionNetPair.py`(프로세스 둘을 띄워 맞댄다)가 같이 쓴다.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

_kHashLinePattern = re.compile(r"\[DestructionHash\] side=(server|client) id=(\d+) events=(\d+) hash=([0-9a-fA-F]{16})")


@dataclass(frozen=True)
class HashLog:
    """로그 하나에서 읽은 해시 줄 — (번호, 사건 수) → 해시와, 오브젝트마다 마지막 (사건 수, 해시)."""

    side: str
    mapKeyToHash: dict[tuple[int, int], str]
    mapLastByObject: dict[int, tuple[int, str]]


def parseHashLog(text: str) -> HashLog:
    """글에서 해시 줄을 읽는다. 한 로그에 서버 · 클라이언트 줄이 섞이면 ValueError 다(리슨 호스트 로그는 한쪽만 남긴다)."""
    mapKeyToHash: dict[tuple[int, int], str] = {}
    mapLastByObject: dict[int, tuple[int, str]] = {}
    setSide: set[str] = set()
    for line in text.splitlines():
        match = _kHashLinePattern.search(line)
        if match is None:
            continue
        side, netID, eventCount, hashText = match.group(1), int(match.group(2)), int(match.group(3)), match.group(4).lower()
        setSide.add(side)
        mapKeyToHash[(netID, eventCount)] = hashText
        mapLastByObject[netID] = (eventCount, hashText)
    if len(setSide) > 1:
        raise ValueError("the log mixes server and client hash lines")
    return HashLog(next(iter(setSide), ""), mapKeyToHash, mapLastByObject)


def compareHashLogs(server: HashLog, client: HashLog) -> tuple[int, list[str]]:
    """겹치는 열쇠 수와 문제 목록을 돌려준다. 문제가 없고 겹치는 열쇠가 있어야 같다.

    클라이언트가 스냅숏으로 건너뛴 사건 수는 줄이 없으므로 겹치는 열쇠만 본다. 또 오브젝트마다 클라이언트의 마지막 줄이 서버의 마지막 줄과 같아야 한다(끝까지 따라왔다).
    겹치는 열쇠가 하나도 없으면 실패다 — 비교한 것이 없다.
    """
    listProblem: list[str] = []
    sharedCount = 0
    for key, clientHash in sorted(client.mapKeyToHash.items()):
        serverHash = server.mapKeyToHash.get(key)
        if serverHash is None:
            continue
        sharedCount += 1
        if serverHash != clientHash:
            listProblem.append(f"id={key[0]} events={key[1]}: server {serverHash} client {clientHash}")
    for netID, (serverCount, serverHash) in sorted(server.mapLastByObject.items()):
        clientLast = client.mapLastByObject.get(netID)
        if clientLast is None:
            listProblem.append(f"id={netID}: the client never logged it (server ended at events={serverCount})")
        elif clientLast != (serverCount, serverHash):
            listProblem.append(f"id={netID}: server ended at events={serverCount} {serverHash}, client at events={clientLast[0]} {clientLast[1]}")
    if sharedCount == 0:
        listProblem.append("no (id, events) key is in both logs - nothing was compared")
    return sharedCount, listProblem


def readLog(path: Path) -> str:
    """로그 파일을 글로 읽는다. 로그는 UTF-8 로 쓴다. PowerShell `*>` 로 받은 로그는 UTF-16 이다(BOM 으로 가린다)."""
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8", errors="replace")
