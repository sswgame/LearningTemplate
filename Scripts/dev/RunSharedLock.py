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
소유자 · 프로세스 번호 · 시각 · 하려는 일을 적어 두므로 `status` 로 누가 왜 잡았는지 볼 수 있습니다. 소유자 프로세스가 죽었으면 버려진 잠금으로
보고 가져옵니다(기록이 없으면 `kStaleSeconds` 가 지난 뒤).

`acquire` 의 소유 프로세스는 `--pid`, 없으면 세션 프로세스(`CLAUDE_PID` — `worktree-make` 의 주인과 같은 값)다. 둘 다 없으면 오류로 멈춘다.
부른 셸이나 py 런처(`os.getppid()`)는 명령이 끝나면 바로 죽어 쓸 수 없다 — 그것을 기본값으로 두었을 때는 잡자마자 주인이 죽은 잠금이 되어,
죽은 주인을 버려진 잠금으로 볼 수 없어 시간(6 시간)으로만 풀렸다. 사람 셸은 `--pid $PID` 를 준다.

의존성 정의가 다른 워크트리가 같은 `build/vcpkg_installed` 에 설치하면 main 의 라이브러리가 지워지고 다시 깔립니다. vcpkg 를 부르는
작업(`cmake --preset`, `--clean-first` 빌드, 설치)은 먼저 `vcpkg` 를 잡습니다.

**빌드 칸(`build`, 공유 칸 2 개)이 동시 빌드 한도다**(작업 방식 2 절). 빌드는 `run` 으로 돌린다 — 잡고, 명령을 돌리고, 지든 이기든 놓는다.
소유 프로세스가 `run` 자신이라 명령이 도는 동안만 살아 있다(`acquire` 의 `--pid` 처럼 셸 번호를 잘못 주어 바로 버려지는 일이 없다).
칸 수는 자원이 정한다(`kMapSharedSlotCount`) — 부르는 쪽마다 칸 수를 주면 서로 다른 한도로 잡는다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Paths import getProjectRoot  # noqa: E402
from common.SharedLock import (acquire, getLockDirectory, getSharedSlotCount, kDefaultWaitSeconds, kMapSharedSlotCount, printStatus,  # noqa: E402
                               releaseHeld, runHolding)
from common.Worktree import findMainRoot, kOwnerPidEnvironment, resolveOwnerProcessId  # noqa: E402


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
    acquireParser.add_argument("--pid", type=int, default=None,
                               help=f"소유 프로세스 번호 — 세션처럼 오래 사는 프로세스(기본: 환경 변수 {kOwnerPidEnvironment}). 죽으면 버려진 잠금이 된다. 사람 셸이면 PowerShell 의 $PID")
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
        args.pid = resolveOwnerProcessId(args.pid)
        if args.pid is None:
            raise ValueError(f"소유 프로세스를 정할 수 없습니다 — --pid 를 줍니다(환경 변수 {kOwnerPidEnvironment} 가 없다). "
                             "부른 셸 · py 런처는 바로 끝나 잠금이 곧바로 버려지므로 기본값으로 쓰지 않는다")
        return acquire(mainRoot, args)
    except (RuntimeError, ValueError) as error:
        print(f"[SharedLock] {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
