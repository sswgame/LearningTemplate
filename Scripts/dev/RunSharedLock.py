#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunSharedLock.py
@brief 워크트리가 나눠 쓰는 자원(vcpkg 설치 폴더 · 도구)을 건드리기 전에 쓰는 사람을 묻고 잡는 잠금입니다.

    py -3 -m Scripts shared-lock status                         # 잡힌 잠금과 소유자
    py -3 -m Scripts shared-lock acquire vcpkg --owner <이름>    # 비어 있을 때까지 기다렸다가 잡는다(기본 최대 1800 초)
    py -3 -m Scripts shared-lock release vcpkg --owner <이름>    # 내가 잡은 것만 놓는다
    py -3 -m Scripts shared-lock acquire gpu --owner <이름> --shared   # 공유 모드(칸 2 개) — 독점이 없을 때 빈 칸을 잡는다
    py -3 -m Scripts shared-lock release gpu --owner <이름> --shared   # 내가 잡은 공유 칸을 놓는다
    py -3 -m Scripts shared-lock run build --shared --owner <이름> --below-normal -- cmake --build --preset Ninja-Debug   # 빌드 칸을 잡고 돌린 뒤 놓는다

공유 모드는 GPU 처럼 "동시에 둘까지는 괜찮지만 단독이 필요한 일도 있는" 자원에 씁니다. 공유 칸은 `<자원>-shared<n>` 디렉터리이고,
독점(기본)은 `<자원>` 디렉터리입니다. 독점은 먼저 자기 디렉터리를 만든 뒤 공유 보유자가 모두 놓을 때까지 기다리고(그동안 새 공유는 들어오지 못한다),
공유는 칸을 잡은 뒤 독점이 있으면 칸을 놓고 기다립니다. 두 쪽 모두 "잡고 나서 확인" 하므로 동시에 시도해도 독점과 공유가 함께 쥐는 일이 없습니다.
기능 확인용 시나리오 · hostgpu 실행은 공유로, Release 측정 · 부하 기준선 · 실제 장치 제거 실험은 독점으로 잡습니다(작업 방식 2 절).

잠금은 main 체크아웃의 `build/.shared-locks/<자원>/` 디렉터리입니다(mkdir 이 원자적이라 먼저 만든 쪽이 잡는다). 안의 `owner.json` 에
소유자 · 프로세스 번호 · 시각 · 하려는 일을 적어 두므로 `status` 로 누가 왜 잡았는지 볼 수 있습니다. 소유자 프로세스가 죽었고 마지막 기록이
`kStaleSeconds` 보다 오래되면 버려진 잠금으로 보고 가져옵니다.

의존성 정의가 다른 워크트리가 같은 `build/vcpkg_installed` 에 설치하면 main 의 라이브러리가 지워지고 다시 깔립니다. vcpkg 를 부르는
작업(`cmake --preset`, `--clean-first` 빌드, 설치)은 먼저 `vcpkg` 를 잡습니다.

**빌드 칸(`build`, 공유 칸 2 개)이 동시 빌드 한도다**(작업 방식 2 절). 빌드는 `run` 으로 돌린다 — 잡고, 명령을 돌리고, 지든 이기든 놓는다.
소유 프로세스가 `run` 자신이라 명령이 도는 동안만 살아 있다(`acquire` 의 `--pid` 처럼 셸 번호를 잘못 주어 바로 버려지는 일이 없다).
칸 수는 자원이 정한다(`kMapSharedSlotCount`) — 부르는 쪽마다 칸 수를 주면 서로 다른 한도로 잡는다.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import getProjectRoot, isProcessAlive, runProcess  # noqa: E402
from MakeWorktree import findMainRoot  # noqa: E402

#: main 체크아웃 기준 잠금 폴더.
kLockRoot = "build/.shared-locks"
#: 소유자 프로세스가 없고 이만큼 지나면 버려진 잠금이다(초).
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
    """버려진 잠금인지 — 기록이 없고 오래됐거나, 소유자 프로세스가 없고 오래됐다."""
    owner = readOwner(lockDirectory)
    if owner is None:
        return now - lockDirectory.stat().st_mtime > kStaleSeconds
    if isProcessAlive(int(owner.get("pid", 0))):
        return False
    return now - float(owner.get("time", 0)) > kStaleSeconds


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
            return 3
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
    """`args.shared` 에 따라 공유 칸 또는 독점을 잡습니다. 0 이면 잡았고, 3 이면 시한을 넘겼습니다."""
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
                return 3
            print(f"[SharedLock] {args.resource} 를 잡았습니다({args.owner}).")
            return 0
        if not bShownWait:
            print(f"[SharedLock] {args.resource} 는 사용 중입니다 — {describeOwner(lockDirectory, now)}. 끝나기를 기다립니다.")
            bShownWait = True
        if now >= deadline:
            print(f"[SharedLock] {args.wait} 초를 기다렸지만 {args.resource} 가 비지 않았습니다. 사용자에게 알리세요.", file=sys.stderr)
            return 3
        time.sleep(kPollSeconds)


def releaseHeld(mainRoot: Path, args: argparse.Namespace) -> int:
    """`acquire` 로 잡은 것을 놓습니다(공유면 내 칸, 아니면 독점)."""
    if args.shared:
        return releaseShared(mainRoot, args)
    return 0 if release(getLockDirectory(mainRoot, args.resource), args.owner) else 1


def runHolding(mainRoot: Path, args: argparse.Namespace, listCommand: list[str]) -> int:
    """
    잠금을 잡고 `listCommand` 를 돌린 뒤 **반드시** 놓습니다(명령이 지거나 예외로 끝나도). 소유 프로세스는 이 프로세스다 — 명령이 도는 동안
    살아 있고, 죽으면 잠금이 버려진 것으로 보인다. `args.belowNormal` 이면 명령을 낮은 우선순위로 띄운다(Windows).
    잡지 못하면 그 종료 코드(3)를, 잡았으면 명령의 종료 코드를 돌려준다.
    """
    if not listCommand:
        raise ValueError("돌릴 명령이 없습니다 — `run <자원> --owner <이름> -- <명령…>`")
    args.pid = os.getpid()
    acquired = acquire(mainRoot, args)
    if acquired != 0:
        return acquired
    try:
        result = runProcess(listCommand, bCapture=False, bBelowNormalPriority=args.belowNormal)
        if not result.bLaunched:
            print(f"[SharedLock] {result.stderr}", file=sys.stderr)
        return result.returnCode
    finally:
        releaseHeld(mainRoot, args)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="워크트리가 나눠 쓰는 자원(vcpkg 설치 폴더 · 빌드 칸 · GPU)을 쓰기 전에 잡는 잠금.")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="잡힌 잠금과 소유자를 보인다")
    acquireParser = sub.add_parser("acquire", help="비어 있을 때까지 기다렸다가 잡는다")
    releaseParser = sub.add_parser("release", help="내가 잡은 잠금을 놓는다")
    runParser = sub.add_parser("run", help="잡고 `--` 뒤의 명령을 돌린 뒤 놓는다(빌드는 이것으로 — 놓기를 잊을 수 없다)")
    for subParser in (acquireParser, releaseParser, runParser):
        subParser.add_argument("resource", help="자원 이름(예: vcpkg · build · gpu)")
        subParser.add_argument("--owner", required=True, help="소유자 이름(에이전트 · 워크트리 이름)")
        subParser.add_argument("--shared", action="store_true", help="공유 모드(칸 수는 자원이 정한다 — kMapSharedSlotCount). 없으면 독점이며, "
                                                                     "독점은 공유 보유자가 모두 놓을 때까지 기다린다")
    for subParser in (acquireParser, runParser):
        subParser.add_argument("--purpose", default="", help="하려는 일(다른 쪽이 status 로 본다)")
        subParser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"기다릴 최대 초(기본 {kDefaultWaitSeconds}). 넘으면 실패(종료 코드 3)")
    acquireParser.add_argument("--pid", type=int, default=os.getppid(), help="소유 프로세스 번호(기본: 부모). 죽으면 버려진 잠금이 된다")
    runParser.add_argument("--below-normal", dest="belowNormal", action="store_true", help="명령을 낮은 우선순위로 띄운다(도우미 빌드 — 작업 방식 2 절)")
    # `run` 의 명령은 `--` 뒤 전부다 — argparse 의 REMAINDER 는 자원 이름 뒤의 옵션(--owner …)까지 삼키므로 먼저 가른다.
    listArgument = list(sys.argv[1:] if argv is None else argv)
    separator = listArgument.index("--") if "--" in listArgument else len(listArgument)
    listArgument, listRunCommand = listArgument[:separator], listArgument[separator + 1:]
    args = parser.parse_args(listArgument)

    try:
        mainRoot = findMainRoot(getProjectRoot())
        if args.command == "status":
            return printStatus(mainRoot)
        getLockDirectory(mainRoot, args.resource)
        args.slots = getSharedSlotCount(args.resource) if args.shared else kMapSharedSlotCount.get(args.resource, 0)
        if args.command == "release":
            return releaseHeld(mainRoot, args)
        if args.command == "run":
            return runHolding(mainRoot, args, listRunCommand)
        return acquire(mainRoot, args)
    except (RuntimeError, ValueError) as error:
        print(f"[SharedLock] {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
