"""
@file SharedLock.py
@brief 워크트리가 나눠 쓰는 자원(vcpkg 설치 폴더 · 빌드 칸 · GPU)의 잠금 — 독점 · 공유 칸, 버려진 잠금 판정, 잡고 돌리고 놓기(`runHolding`).

명령은 `dev/RunSharedLock.py`(`py -3 -m Scripts shared-lock`)이고 동작 설명도 그 파일 머리에 있다. `RunFullBuild` 가 빌드 칸을 잡을 때 이 모듈을 쓴다.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

from .ExitCode import ExitCode
from .Process import isProcessAlive, runProcess

#: main 체크아웃 기준 잠금 폴더.
kLockRoot = "build/.shared-locks"
#: 소유자 기록이 없는(쓰다 만) 잠금이 이만큼 지나면 버려진 잠금이다(초).
kStaleSeconds = 6 * 3600
#: 기다리는 최대 시간 기본값(초)과 폴링 간격(초).
kDefaultWaitSeconds = 1800
kPollSeconds = 5
#: 공유 모드를 쓰는 자원 → 칸 수. 칸 수를 부르는 쪽마다 주면 서로 다른 한도로 잡아 한도가 무너진다 — 그래서 자원이 정한다.
#: `build` 는 동시 빌드 한도(작업 방식 2 절: 둘)다. 표에 없는 자원은 독점만 쓴다.
kMapSharedSlotCount: dict[str, int] = {"gpu": 2, "build": 2}


def getLockDirectory(mainRoot: Path, resource: str) -> Path:
    """자원 이름 하나의 잠금 디렉터리입니다. 이름은 영숫자 · `-` · `_` 만 받습니다."""
    if not resource or any(not (ch.isalnum() or ch in "-_") for ch in resource):
        raise ValueError(f"자원 이름은 영숫자 · '-' · '_' 만 씁니다: '{resource}'")
    return mainRoot / kLockRoot / resource


def readOwner(lockDirectory: Path) -> dict | None:
    """잠금의 소유자 기록입니다. 없거나 깨졌으면 None."""
    try:
        return json.loads((lockDirectory / "owner.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def isStale(lockDirectory: Path, now: float) -> bool:
    """
    버려진 잠금인지 — 기록이 없고 오래됐거나, 소유자 프로세스가 죽었다. 소유자는 오래 사는 프로세스(`run` 자신 · 세션)라 죽었으면 끝난 것이다.
    프로세스 번호가 재사용되면 살아 있다고 보아 안전한 쪽(놓지 않음)으로만 틀린다.
    """
    owner = readOwner(lockDirectory)
    if owner is None or "pid" not in owner:
        return now - lockDirectory.stat().st_mtime > kStaleSeconds
    return not isProcessAlive(int(owner["pid"]))


def release(lockDirectory: Path, owner: str | None) -> bool:
    """잠금을 놓습니다. `owner` 가 주어지면 그 소유자의 것만 놓습니다."""
    record = readOwner(lockDirectory)
    if owner is not None and record is not None and record.get("owner") != owner:
        print(f"[SharedLock] {lockDirectory.name} 는 '{record.get('owner')}' 의 것이라 놓지 않습니다.", file=sys.stderr)
        return False
    ownerFile = lockDirectory / "owner.json"
    if ownerFile.exists():
        ownerFile.unlink()
    try:
        lockDirectory.rmdir()
    except FileNotFoundError:
        pass
    return True


def tryAcquire(lockDirectory: Path, owner: str, purpose: str, pid: int, now: float) -> bool:
    """잠금을 한 번 시도합니다. 버려진 잠금이면 치우고 다시 시도합니다."""
    lockDirectory.parent.mkdir(parents=True, exist_ok=True)
    for _ in range(2):
        try:
            lockDirectory.mkdir()
        except FileExistsError:
            if not isStale(lockDirectory, now):
                return False
            print(f"[SharedLock] 버려진 잠금을 가져옵니다: {lockDirectory.name} (이전 소유자 {readOwner(lockDirectory)})")
            release(lockDirectory, None)
            continue
        record = {"owner": owner, "pid": pid, "time": now, "purpose": purpose}
        (lockDirectory / "owner.json").write_text(json.dumps(record, ensure_ascii=False), encoding="utf-8")
        return True
    return False


def getSharedSlotDirectories(mainRoot: Path, resource: str, slotCount: int) -> list[Path]:
    """자원의 공유 칸 디렉터리들입니다(`<자원>-shared1` …)."""
    return [getLockDirectory(mainRoot, f"{resource}-shared{index}") for index in range(1, slotCount + 1)]


def isHeld(lockDirectory: Path, now: float) -> bool:
    """잠금이 살아 있는 소유자에게 잡혀 있으면 True 입니다. 버려진 잠금은 치우고 False 입니다."""
    if not lockDirectory.is_dir():
        return False
    if isStale(lockDirectory, now):
        print(f"[SharedLock] 버려진 잠금을 치웁니다: {lockDirectory.name}")
        release(lockDirectory, None)
        return False
    return True


def acquireShared(mainRoot: Path, args: argparse.Namespace) -> int:
    """공유 칸 하나를 잡습니다. 독점 보유자가 있거나 칸이 모두 찼으면 기다립니다."""
    exclusiveDirectory = getLockDirectory(mainRoot, args.resource)
    listSlot = getSharedSlotDirectories(mainRoot, args.resource, args.slots)
    deadline = time.time() + args.wait
    bShownWait = False
    while True:
        now = time.time()
        if not isHeld(exclusiveDirectory, now):
            for slotDirectory in listSlot:
                if not tryAcquire(slotDirectory, args.owner, args.purpose, args.pid, now):
                    continue
                # 잡은 뒤 다시 본다 — 그사이 독점이 들어왔으면 놓고 기다린다(독점이 먼저다).
                if isHeld(exclusiveDirectory, time.time()):
                    release(slotDirectory, args.owner)
                    break
                print(f"[SharedLock] {args.resource} 공유 칸 {slotDirectory.name} 을 잡았습니다({args.owner}).")
                return 0
        if not bShownWait:
            holder = describeOwner(exclusiveDirectory, now) if exclusiveDirectory.is_dir() else "공유 칸이 모두 찼습니다"
            print(f"[SharedLock] {args.resource} 공유 칸을 기다립니다 — {holder}.")
            bShownWait = True
        if now >= deadline:
            print(f"[SharedLock] {args.wait} 초를 기다렸지만 {args.resource} 공유 칸이 비지 않았습니다. 사용자에게 알리세요.", file=sys.stderr)
            return ExitCode.LimitReached
        time.sleep(kPollSeconds)


def waitForSharedHolders(mainRoot: Path, args: argparse.Namespace, deadline: float) -> bool:
    """독점을 잡은 뒤 공유 보유자가 모두 놓을 때까지 기다립니다. 시한을 넘으면 False 입니다."""
    listSlot = getSharedSlotDirectories(mainRoot, args.resource, args.slots)
    bShownWait = False
    while True:
        now = time.time()
        listHeld = [slot for slot in listSlot if isHeld(slot, now)]
        if not listHeld:
            return True
        if not bShownWait:
            print(f"[SharedLock] {args.resource} 독점 — 공유 보유자가 놓기를 기다립니다: " + ", ".join(describeOwner(slot, now) for slot in listHeld))
            bShownWait = True
        if now >= deadline:
            return False
        time.sleep(kPollSeconds)


def releaseShared(mainRoot: Path, args: argparse.Namespace) -> int:
    """내가 잡은 공유 칸을 놓습니다. 잡은 칸이 없으면 할 일이 없어 성공입니다."""
    for slotDirectory in getSharedSlotDirectories(mainRoot, args.resource, args.slots):
        record = readOwner(slotDirectory)
        if slotDirectory.is_dir() and record is not None and record.get("owner") == args.owner:
            release(slotDirectory, args.owner)
    return 0


def describeOwner(lockDirectory: Path, now: float) -> str:
    """사람이 읽는 소유자 한 줄입니다."""
    record = readOwner(lockDirectory)
    if record is None:
        return "소유자 기록 없음"
    ageMinutes = int((now - float(record.get("time", now))) / 60)
    return f"{record.get('owner')} (pid {record.get('pid')}, {ageMinutes}분 전, {record.get('purpose', '')})"


def printStatus(mainRoot: Path) -> int:
    """잡힌 잠금을 모두 보입니다."""
    lockRoot = mainRoot / kLockRoot
    listLock = sorted(p for p in lockRoot.iterdir() if p.is_dir()) if lockRoot.is_dir() else []
    now = time.time()
    for lockDirectory in listLock:
        print(f"{lockDirectory.name}: {describeOwner(lockDirectory, now)}{' [버려짐]' if isStale(lockDirectory, now) else ''}")
    if not listLock:
        print("잡힌 잠금이 없습니다.")
    return 0


def getSharedSlotCount(resource: str) -> int:
    """공유 모드로 잡는 자원의 칸 수(`kMapSharedSlotCount`). 표에 없으면 ValueError — 공유로 쓰지 않는 자원이다."""
    if resource not in kMapSharedSlotCount:
        raise ValueError(f"'{resource}' 는 공유 모드가 없는 자원입니다(공유 자원: {', '.join(sorted(kMapSharedSlotCount))}) — --shared 없이 잡는다")
    return kMapSharedSlotCount[resource]


def acquire(mainRoot: Path, args: argparse.Namespace) -> int:
    """`args.shared` 에 따라 공유 칸 또는 독점을 잡습니다. `Passed` 면 잡았고, `LimitReached` 면 시한을 넘겼습니다."""
    if args.shared:
        return acquireShared(mainRoot, args)
    lockDirectory = getLockDirectory(mainRoot, args.resource)
    deadline = time.time() + args.wait
    bShownWait = False
    while True:
        now = time.time()
        if tryAcquire(lockDirectory, args.owner, args.purpose, args.pid, now):
            # 독점은 잡은 뒤 공유 보유자가 모두 놓기를 기다린다 — 그동안 새 공유는 독점을 보고 물러난다.
            if not waitForSharedHolders(mainRoot, args, deadline):
                release(lockDirectory, args.owner)
                print(f"[SharedLock] {args.wait} 초를 기다렸지만 {args.resource} 공유 보유자가 놓지 않았습니다. 사용자에게 알리세요.", file=sys.stderr)
                return ExitCode.LimitReached
            print(f"[SharedLock] {args.resource} 를 잡았습니다({args.owner}).")
            return 0
        if not bShownWait:
            print(f"[SharedLock] {args.resource} 는 사용 중입니다 — {describeOwner(lockDirectory, now)}. 끝나기를 기다립니다.")
            bShownWait = True
        if now >= deadline:
            print(f"[SharedLock] {args.wait} 초를 기다렸지만 {args.resource} 가 비지 않았습니다. 사용자에게 알리세요.", file=sys.stderr)
            return ExitCode.LimitReached
        time.sleep(kPollSeconds)


def releaseHeld(mainRoot: Path, args: argparse.Namespace) -> int:
    """`acquire` 로 잡은 것을 놓습니다(공유면 내 칸, 아니면 독점)."""
    if args.shared:
        return releaseShared(mainRoot, args)
    return 0 if release(getLockDirectory(mainRoot, args.resource), args.owner) else 1


def runHolding(mainRoot: Path, args: argparse.Namespace, listCommand: list[str], logPath: Path | None = None) -> int:
    """
    잠금을 잡고 `listCommand` 를 돌린 뒤 **반드시** 놓습니다(명령이 지거나 예외로 끝나도). 소유 프로세스는 이 프로세스다 — 명령이 도는 동안
    살아 있고, 죽으면 잠금이 버려진 것으로 보인다. `args.belowNormal` 이면 명령을 낮은 우선순위로 띄운다(Windows).
    `logPath` 를 주면 명령의 출력이 콘솔 대신 그 파일로 간다(`common/LogDigest.py` 가 요약해 보인다).
    잡지 못하면 그 종료 코드(3)를, 잡았으면 명령의 종료 코드를 돌려준다.
    """
    if not listCommand:
        raise ValueError("돌릴 명령이 없습니다 — `run <자원> --owner <이름> -- <명령…>`")
    args.pid = os.getpid()
    acquired = acquire(mainRoot, args)
    if acquired != 0:
        return acquired
    try:
        result = runProcess(listCommand, bCapture=False, stdoutPath=logPath, bBelowNormalPriority=args.belowNormal)
        if not result.bLaunched:
            print(f"[SharedLock] {result.stderr}", file=sys.stderr)
        return result.returnCode
    finally:
        releaseHeld(mainRoot, args)
