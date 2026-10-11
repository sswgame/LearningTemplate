#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunSharedLock.py
@brief 워크트리가 나눠 쓰는 자원(vcpkg 설치 폴더 · 도구)을 건드리기 전에 쓰는 사람을 묻고 잡는 잠금입니다.

    py -3 -m Scripts shared-lock status                         # 잡힌 잠금과 소유자
    py -3 -m Scripts shared-lock acquire vcpkg --owner <이름>    # 비어 있을 때까지 기다렸다가 잡는다(기본 최대 1800 초)
    py -3 -m Scripts shared-lock release vcpkg --owner <이름>    # 내가 잡은 것만 놓는다

잠금은 main 체크아웃의 `build/.shared-locks/<자원>/` 디렉터리입니다(mkdir 이 원자적이라 먼저 만든 쪽이 잡는다). 안의 `owner.json` 에
소유자 · 프로세스 번호 · 시각 · 하려는 일을 적어 두므로 `status` 로 누가 왜 잡았는지 볼 수 있습니다. 소유자 프로세스가 죽었고 마지막 기록이
`kStaleSeconds` 보다 오래되면 버려진 잠금으로 보고 가져옵니다.

의존성 정의가 다른 워크트리가 같은 `build/vcpkg_installed` 에 설치하면 main 의 라이브러리가 지워지고 다시 깔립니다. vcpkg 를 부르는
작업(`cmake --preset`, `--clean-first` 빌드, 설치)은 먼저 `vcpkg` 를 잡습니다. 빌드 슬롯 잠금(`LT-wt/BUILD-LOCK-README.txt`)과는 별개입니다.
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
from common import getProjectRoot  # noqa: E402
from MakeWorktree import findMainRoot  # noqa: E402

#: main 체크아웃 기준 잠금 폴더.
kLockRoot = "build/.shared-locks"
#: 소유자 프로세스가 없고 이만큼 지나면 버려진 잠금이다(초).
kStaleSeconds = 6 * 3600
#: 기다리는 최대 시간 기본값(초)과 폴링 간격(초).
kDefaultWaitSeconds = 1800
kPollSeconds = 5


def getLockDirectory(mainRoot: Path, resource: str) -> Path:
    """자원 이름 하나의 잠금 디렉터리입니다. 이름은 영숫자 · `-` · `_` 만 받습니다."""
    if not resource or any(not (ch.isalnum() or ch in "-_") for ch in resource):
        raise ValueError(f"자원 이름은 영숫자 · '-' · '_' 만 씁니다: '{resource}'")
    return mainRoot / kLockRoot / resource


def isProcessAlive(processId: int) -> bool:
    """프로세스 번호가 살아 있는지 봅니다."""
    if processId <= 0:
        return False
    if os.name == "nt":
        import ctypes

        kQueryLimited = 0x1000
        kAccessDenied = 5
        handle = ctypes.windll.kernel32.OpenProcess(kQueryLimited, False, processId)
        if not handle:
            return ctypes.windll.kernel32.GetLastError() == kAccessDenied
        ctypes.windll.kernel32.CloseHandle(handle)
        return True
    try:
        os.kill(processId, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="워크트리가 나눠 쓰는 자원(vcpkg 설치 폴더 등)을 쓰기 전에 잡는 잠금.")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="잡힌 잠금과 소유자를 보인다")
    acquireParser = sub.add_parser("acquire", help="비어 있을 때까지 기다렸다가 잡는다")
    releaseParser = sub.add_parser("release", help="내가 잡은 잠금을 놓는다")
    for subParser in (acquireParser, releaseParser):
        subParser.add_argument("resource", help="자원 이름(예: vcpkg)")
        subParser.add_argument("--owner", required=True, help="소유자 이름(에이전트 · 워크트리 이름)")
    acquireParser.add_argument("--purpose", default="", help="하려는 일(다른 쪽이 status 로 본다)")
    acquireParser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"기다릴 최대 초(기본 {kDefaultWaitSeconds}). 넘으면 실패(종료 코드 3)")
    acquireParser.add_argument("--pid", type=int, default=os.getppid(), help="소유 프로세스 번호(기본: 부모). 죽으면 버려진 잠금이 된다")
    args = parser.parse_args(argv)

    try:
        mainRoot = findMainRoot(getProjectRoot())
        if args.command == "status":
            return printStatus(mainRoot)
        lockDirectory = getLockDirectory(mainRoot, args.resource)
    except (RuntimeError, ValueError) as error:
        print(f"[SharedLock] {error}", file=sys.stderr)
        return 2

    if args.command == "release":
        return 0 if release(lockDirectory, args.owner) else 1

    deadline = time.time() + args.wait
    bShownWait = False
    while True:
        now = time.time()
        if tryAcquire(lockDirectory, args.owner, args.purpose, args.pid, now):
            print(f"[SharedLock] {args.resource} 를 잡았습니다({args.owner}).")
            return 0
        if not bShownWait:
            print(f"[SharedLock] {args.resource} 는 사용 중입니다 — {describeOwner(lockDirectory, now)}. 끝나기를 기다립니다.")
            bShownWait = True
        if now >= deadline:
            print(f"[SharedLock] {args.wait} 초를 기다렸지만 {args.resource} 가 비지 않았습니다. 사용자에게 알리세요.", file=sys.stderr)
            return 3
        time.sleep(kPollSeconds)


if __name__ == "__main__":
    sys.exit(main())
