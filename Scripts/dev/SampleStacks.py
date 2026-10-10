#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file SampleStacks.py
@brief 살아 있는 프로세스의 스레드 스택을 여러 번 떠서 함수별로 모읍니다(Windows x64, DbgHelp) — 프로파일러 구간이 닿지 않는 곳(드라이버 · 잠금 · 서드파티)을 볼 때.

    py -3 -m Scripts stacks <pid> [--samples 200] [--interval-ms 10] [--symbols <PDB 폴더>] [--thread <tid>] [--top 30]

스레드를 잠깐 멈추고(SuspendThread) 문맥을 읽어 StackWalk64 로 걷는다 — 멈춘 동안 그 스레드는 돌지 않으므로 간격을 너무 짧게 잡지 않는다.
출력: 맨 위 프레임(자기 시간)과 스택 어디든(포함 시간) 함수별 표본 수 · 비율. PDB 는 실행 파일 옆(Debug · Release 는 Bin)에서 찾고,
Shipping 은 `--symbols build/Ninja-Shipping/Symbols` 로 준다. ICF(/OPT:ICF)로 합쳐진 함수는 남의 이름으로 보일 수 있다.
"""

from __future__ import annotations

import argparse
import ctypes
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

_kProcessAllAccess = 0x1F0FFF
_kThreadAllAccess = 0x1F03FF
_kSnapThread = 0x4
_kContextFull = 0x10001F  # CONTEXT_FULL (AMD64)
_kContextSize = 1232      # x64 CONTEXT
_kContextFlagsOffset = 0x30
_kContextRspOffset = 0x98
_kContextRbpOffset = 0xA0
_kContextRipOffset = 0xF8
_kMachineAmd64 = 0x8664
_kAddressModeFlat = 3
_kMaxFrameCount = 64
_kSuspendFailed = 0xFFFFFFFF
_kSymbolInfoBaseSize = 88
_kSymbolMaxNameLength = 500
_kSymOptions = 0x2 | 0x10 | 0x200  # SYMOPT_UNDNAME · SYMOPT_LOAD_LINES · SYMOPT_DEFERRED_LOADS


if sys.platform == "win32":
    import ctypes.wintypes as wt

    class ThreadEntry32(ctypes.Structure):
        _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD), ("th32OwnerProcessID", wt.DWORD),
                    ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]

    class Address64(ctypes.Structure):
        _fields_ = [("Offset", ctypes.c_uint64), ("Segment", wt.WORD), ("Mode", ctypes.c_int)]

    class StackFrame64(ctypes.Structure):
        _fields_ = [("AddrPC", Address64), ("AddrReturn", Address64), ("AddrFrame", Address64), ("AddrStack", Address64),
                    ("AddrBStore", Address64), ("FuncTableEntry", ctypes.c_void_p), ("Params", ctypes.c_uint64 * 4), ("Far", wt.BOOL),
                    ("Virtual", wt.BOOL), ("Reserved", ctypes.c_uint64 * 3), ("KdHelp", ctypes.c_byte * 256)]

    class SymbolInfo(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2), ("Index", wt.ULONG),
                    ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG), ("Value", ctypes.c_uint64),
                    ("Address", ctypes.c_uint64), ("Register", wt.ULONG), ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG),
                    ("MaxNameLen", wt.ULONG), ("Name", ctypes.c_char * 512)]


class StackSampler:
    """프로세스 하나의 스레드 스택을 뜹니다."""

    def __init__(self, pid: int, symbolPath: str | None) -> None:
        self._kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self._dbgHelp = ctypes.WinDLL("dbghelp", use_last_error=True)
        self._kernel.OpenProcess.restype = wt.HANDLE
        self._kernel.OpenThread.restype = wt.HANDLE
        self._kernel.CreateToolhelp32Snapshot.restype = wt.HANDLE
        self._kernel.SuspendThread.argtypes = [wt.HANDLE]
        self._kernel.SuspendThread.restype = wt.DWORD
        self._kernel.ResumeThread.argtypes = [wt.HANDLE]
        self._kernel.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
        self._kernel.CloseHandle.argtypes = [wt.HANDLE]
        self._dbgHelp.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
        self._dbgHelp.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_void_p]
        self._dbgHelp.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                                              ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        self._pid = pid
        self._process = self._kernel.OpenProcess(_kProcessAllAccess, False, pid)
        if not self._process:
            raise OSError(f"OpenProcess({pid}) failed: {ctypes.get_last_error()}")
        self._dbgHelp.SymSetOptions(_kSymOptions)
        if not self._dbgHelp.SymInitialize(self._process, symbolPath.encode() if symbolPath else None, True):
            raise OSError(f"SymInitialize failed: {ctypes.get_last_error()}")
        self._pfnFunctionTable = ctypes.cast(self._dbgHelp.SymFunctionTableAccess64, ctypes.c_void_p)
        self._pfnModuleBase = ctypes.cast(self._dbgHelp.SymGetModuleBase64, ctypes.c_void_p)
        self._mapName: dict[int, str] = {}

    def collectThreadIds(self) -> list[int]:
        snapshot = self._kernel.CreateToolhelp32Snapshot(_kSnapThread, 0)
        entry = ThreadEntry32()
        entry.dwSize = ctypes.sizeof(ThreadEntry32)
        listThreadId = []
        bMore = self._kernel.Thread32First(snapshot, ctypes.byref(entry))
        while bMore:
            if entry.th32OwnerProcessID == self._pid:
                listThreadId.append(entry.th32ThreadID)
            bMore = self._kernel.Thread32Next(snapshot, ctypes.byref(entry))
        self._kernel.CloseHandle(snapshot)
        return listThreadId

    def resolveName(self, address: int) -> str:
        if address in self._mapName:
            return self._mapName[address]
        symbol = SymbolInfo()
        symbol.SizeOfStruct = _kSymbolInfoBaseSize
        symbol.MaxNameLen = _kSymbolMaxNameLength
        displacement = ctypes.c_uint64()
        bFound = self._dbgHelp.SymFromAddr(self._process, address, ctypes.byref(displacement), ctypes.byref(symbol))
        name = symbol.Name.decode(errors="replace") if bFound else hex(address)
        self._mapName[address] = name
        return name

    def captureStack(self, threadID: int) -> list[int]:
        """스레드 하나의 프레임 주소들(맨 위부터). 멈추지 못하면 빈 목록."""
        thread = self._kernel.OpenThread(_kThreadAllAccess, False, threadID)
        if not thread:
            return []
        listAddress: list[int] = []
        if self._kernel.SuspendThread(thread) != _kSuspendFailed:
            context = (ctypes.c_byte * (_kContextSize + 16))()
            aligned = (ctypes.addressof(context) + 15) & ~15
            ctypes.c_uint32.from_address(aligned + _kContextFlagsOffset).value = _kContextFull
            if self._kernel.GetThreadContext(thread, ctypes.c_void_p(aligned)):
                frame = StackFrame64()
                frame.AddrPC.Offset = ctypes.c_uint64.from_address(aligned + _kContextRipOffset).value
                frame.AddrStack.Offset = ctypes.c_uint64.from_address(aligned + _kContextRspOffset).value
                frame.AddrFrame.Offset = ctypes.c_uint64.from_address(aligned + _kContextRbpOffset).value
                frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = _kAddressModeFlat
                for _ in range(_kMaxFrameCount):
                    if not self._dbgHelp.StackWalk64(_kMachineAmd64, self._process, thread, ctypes.byref(frame), ctypes.c_void_p(aligned), None,
                                                     self._pfnFunctionTable, self._pfnModuleBase, None):
                        break
                    if frame.AddrPC.Offset == 0:
                        break
                    listAddress.append(frame.AddrPC.Offset)
            self._kernel.ResumeThread(thread)
        self._kernel.CloseHandle(thread)
        return listAddress


def main(listArgument: list[str] | None = None) -> int:
    if sys.platform != "win32":
        print("SampleStacks.py is Windows only (DbgHelp)", file=sys.stderr)
        return 2
    parser = argparse.ArgumentParser(description="살아 있는 프로세스의 스택을 여러 번 떠서 함수별로 모은다")
    parser.add_argument("pid", type=int)
    parser.add_argument("--samples", type=int, default=200, help="뜨는 횟수(회마다 모든 스레드)")
    parser.add_argument("--interval-ms", type=float, default=10.0, help="뜨기 사이 간격(ms)")
    parser.add_argument("--symbols", default=None, help="PDB 폴더(기본: 실행 파일 옆 · _NT_SYMBOL_PATH)")
    parser.add_argument("--thread", type=int, default=0, help="이 스레드만(0 = 모두)")
    parser.add_argument("--top", type=int, default=30, help="표에 찍을 함수 수")
    args = parser.parse_args(listArgument)

    sampler = StackSampler(args.pid, args.symbols)
    selfCount: Counter[str] = Counter()
    inclusiveCount: Counter[str] = Counter()
    stackCount = 0
    for _ in range(args.samples):
        listThreadId = [args.thread] if args.thread else sampler.collectThreadIds()
        for threadID in listThreadId:
            listAddress = sampler.captureStack(threadID)
            if not listAddress:
                continue
            stackCount += 1
            listName = [sampler.resolveName(address) for address in listAddress]
            selfCount[listName[0]] += 1
            for name in set(listName):
                inclusiveCount[name] += 1
        time.sleep(args.interval_ms / 1000.0)
    if stackCount == 0:
        print("no stack was captured (access denied, or the process exited)", file=sys.stderr)
        return 1
    print(f"{stackCount} stacks from {args.samples} rounds")
    print(f"{'self':>7} {'self%':>6}  function")
    for name, count in selfCount.most_common(args.top):
        print(f"{count:7d} {100.0 * count / stackCount:5.1f}%  {name}")
    print(f"{'incl':>7} {'incl%':>6}  function")
    for name, count in inclusiveCount.most_common(args.top):
        print(f"{count:7d} {100.0 * count / stackCount:5.1f}%  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
