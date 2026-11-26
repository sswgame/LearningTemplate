#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/git_locks.py

강제로 끝난 git 이 남긴 잠금 파일(`index.lock` 등)을 찾아 알린다. `--remove` 는 확실히 찌꺼기인 것만 지운다.

왜 남는가. git 은 `index.lock` 을 빈 파일로 만들어 열어 둔 채 작업 트리를 훑고, 다 훑은 뒤에 쓴다. 그 사이 강제로 끝나면(도구 시한 · TaskStop · 훅 중단 — Windows 의
TerminateProcess 는 정리를 부르지 않는다) 0 바이트 파일이 남아 다음 커밋 · 병합이 `Unable to create index.lock` 으로 멈춘다. 읽기만 하는 `git status` · `git diff` 도
색인을 새로 쓰려고 같은 잠금을 잡는다(git 2.x, 잠금이 선 순간 끝내면 6 번 중 6 번 남았다). 그래서 Scripts 가 띄우는 git 은 `GIT_OPTIONAL_LOCKS=0` 으로 그 잠금을 잡지 않고
(`common/__init__.py` · `common/git.sh`), 쓰는 git(add · commit · merge)이 남긴 것은 이 명령이 찾는다.

찌꺼기 판정. 빈 파일이고, 어느 프로세스도 열고 있지 않고, 만든 지 `MIN_AGE_SECONDS` 가 지났다. 살아 있는 git 은 빈 잠금을 늘 열고 있으므로 열린 빈 파일은 지우지 않는다.
내용이 든 잠금(`git commit -a` 의 훅 구간처럼 닫아 두는 때가 있다)은 알리기만 한다.

  py -3 -m Scripts git-locks              # 찾아 알린다(남은 것이 있으면 1)
  py -3 -m Scripts git-locks --remove     # 찌꺼기를 지운다(지우지 않은 것이 남으면 1)
"""

from __future__ import annotations

import argparse
import os
import time
from pathlib import Path

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from common.host_platform import is_windows_host

MIN_AGE_SECONDS = 10.0


def list_lock_files(git_directory: Path) -> list[Path]:
    """공통 git 폴더와 워크트리 폴더 바로 아래의 `*.lock`(index · HEAD · config · packed-refs)."""
    return sorted([*git_directory.glob("*.lock"), *git_directory.glob("worktrees/*/*.lock")])


def is_open_in_any_process(path: Path) -> bool:
    """어느 프로세스든 이 파일을 열고 있는지. 알 수 없는 플랫폼은 열려 있는 것으로 본다(지우지 않는다)."""
    if is_windows_host():
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        generic_read, open_existing, invalid_handle = 0x80000000, 3, wintypes.HANDLE(-1).value
        handle = kernel32.CreateFileW(str(path), generic_read, 0, None, open_existing, 0, None)   # 아무것도 나누지 않고 연다 — 다른 핸들이 있으면 진다
        if handle == invalid_handle:
            return True
        kernel32.CloseHandle(handle)
        return False
    process_root = Path("/proc")
    if not process_root.is_dir():
        return True
    target = str(path.resolve())
    for link in process_root.glob("[0-9]*/fd/*"):
        try:
            if os.readlink(link) == target:
                return True
        except OSError:
            continue   # 남의 프로세스 · 막 끝난 프로세스
    return False


def describe_lock(path: Path, now: float) -> tuple[bool, str]:
    """(찌꺼기인가, 까닭)."""
    info = path.stat()
    age = now - info.st_mtime
    if info.st_size != 0:
        return False, f"{info.st_size} 바이트 — 쓰던 git 이 남긴 것이다. 도는 git 이 없으면 손으로 지운다"
    if age < MIN_AGE_SECONDS:
        return False, f"만든 지 {age:.0f} 초 — 도는 git 일 수 있다"
    if is_open_in_any_process(path):
        return False, "git 이 열고 있다(도는 중)"
    return True, f"0 바이트 · 연 프로세스 없음 · {age:.0f} 초 — 강제로 끝난 git 의 찌꺼기"


class GitLocksCommand(Command):
    name = "git-locks"
    help = "강제로 끝난 git 이 남긴 빈 잠금 파일(index.lock 등)을 찾는다 — --remove 는 연 프로세스가 없는 빈 것만 지운다"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--remove", action="store_true", help="찌꺼기(빈 파일 · 연 프로세스 없음)를 지운다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        left = 0
        for path in list_lock_files(context.main_root / ".git"):
            try:
                is_leftover, reason = describe_lock(path, time.time())
                if is_leftover and args.remove:
                    path.unlink()
            except FileNotFoundError:
                continue   # 그 사이 git 이 끝냈다
            removed = is_leftover and args.remove
            left += 0 if removed else 1
            print(f"[git-locks] {'지웠다' if removed else '남아 있다'}: {path} — {reason}{'' if removed or not is_leftover else ' (--remove 로 지운다)'}")
        return ExitCode.VIOLATION if left else ExitCode.PASSED
