#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
자식 프로세스에 거는 하드웨어 제한 — 부하 검증(`Scripts/qa/Load.py`)의 하드웨어 프로필이 쓰는 CPU 코어 수 · 메모리 상한.

Windows 는 Job Object 하나에 친화도(`JOB_OBJECT_LIMIT_AFFINITY`)와 프로세스 메모리 상한(`JOB_OBJECT_LIMIT_PROCESS_MEMORY`)을 걸고, 프로세스를
**멈춘 채로 만든 뒤** Job 에 넣고 깨운다(만들고 나서 넣으면 엔진이 워커 수를 정하는 순간과 겨룬다 — 워커 수는 친화도로 정한다,
`sw::thread::getUsableProcessorCount`). 자식이 만드는 프로세스(셰이더 컴파일러 등)도 같은 Job 에 든다.
리눅스는 `sched_setaffinity` 와 `RLIMIT_AS` 를 자식 쪽 `preexec_fn` 에서 건다.

소프트웨어 제한이다 — 클럭 · 캐시 · 메모리 대역폭 · GPU 는 그대로라 결과는 "에뮬레이션" 으로 표시하고 실제 기기 측정과 섞지 않는다.
언리얼 Gauntlet 의 디바이스 프로필이 실제 기기를 고르는 자리이고, 기기가 없을 때 같은 판정을 미리 돌려 보는 것이 이 제한의 몫이다.
"""

from __future__ import annotations

import contextlib
import os
import subprocess
import sys
from dataclasses import dataclass
from collections.abc import Callable, Iterator

#: `CREATE_SUSPENDED` — 첫 스레드를 멈춘 채로 만든다(Job 에 넣은 뒤 깨운다).
_kCreateSuspended = 0x00000004
_kJobObjectExtendedLimitInformation = 9
_kJobObjectLimitAffinity = 0x00000010
_kJobObjectLimitProcessMemory = 0x00000100
_kJobObjectLimitKillOnJobClose = 0x00002000


def closeJobHandle(job: object | None) -> None:
    """Job 핸들을 닫는다(None 은 건너뜀)."""
    if job:
        import ctypes
        import ctypes.wintypes as wintypes
        kernel32 = ctypes.WinDLL("kernel32")
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle(wintypes.HANDLE(job))


@dataclass(frozen=True)
class ProcessLimits:
    """제한 하나 — 0 은 그 제한을 걸지 않는다."""

    cpuCount: int = 0
    memoryMegabytes: int = 0

    @property
    def bActive(self) -> bool:
        return self.cpuCount > 0 or self.memoryMegabytes > 0

    def describe(self) -> str:
        listPart = [f"cpu {self.cpuCount}" if self.cpuCount > 0 else "", f"memory {self.memoryMegabytes} MB" if self.memoryMegabytes > 0 else ""]
        return ", ".join(part for part in listPart if part) or "none"

    def makeCreationFlags(self) -> int:
        """Popen 의 `creationflags` — Windows 에서 제한이 있으면 멈춘 채로 만든다."""
        return _kCreateSuspended if self.bActive and sys.platform == "win32" else 0

    def makePreexec(self) -> Callable[[], None] | None:
        """리눅스 자식 쪽에서 부를 함수(Windows · 제한 없음은 None)."""
        if not self.bActive or sys.platform == "win32":
            return None
        cpuCount = self.cpuCount
        memoryBytes = self.memoryMegabytes * 1024 * 1024

        def applyInternal() -> None:
            import resource
            if cpuCount > 0:
                os.sched_setaffinity(0, set(range(min(cpuCount, os.cpu_count() or cpuCount))))
            if memoryBytes > 0:
                resource.setrlimit(resource.RLIMIT_AS, (memoryBytes, memoryBytes))

        return applyInternal

    @contextlib.contextmanager
    def attach(self, process: subprocess.Popen) -> Iterator[None]:
        """
        Windows: 멈춘 채로 만든 @p process 를 제한을 건 Job 에 넣고 깨운 채 `with` 본문을 돈다. Job 핸들은 본문이 끝나는 모든 길
        (정상 · 예외)에서 여기서 닫는다 — 호출자가 닫기를 잊을 수 없다. 본문 동안은 쥐고 있어야 한다
        (닫으면 `KILL_ON_JOB_CLOSE` 로 자식이 끝난다 — 스크립트가 죽어도 App 이 남지 않게). 리눅스 · 제한 없음은 아무것도 안 한다.
        실패하면 프로세스를 죽이고 RuntimeError 다(제한 없이 잰 숫자가 제한한 숫자로 읽히면 안 된다).
        """
        job = self._attachInternal(process)
        try:
            yield
        finally:
            closeJobHandle(job)

    def _attachInternal(self, process: subprocess.Popen) -> object | None:
        """Job 을 만들어 프로세스를 넣고 깨운 뒤 핸들을 돌려준다(닫기는 `attach` 몫). 해당 없으면 None."""
        if not self.bActive or sys.platform != "win32":
            return None
        import ctypes
        import ctypes.wintypes as wintypes

        class IoCounters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_ulonglong) for name in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                                                                "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

        class BasicLimit(ctypes.Structure):
            _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong), ("LimitFlags", wintypes.DWORD),
                        ("MinimumWorkingSetSize", ctypes.c_size_t), ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                        ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]

        class ExtendedLimit(ctypes.Structure):
            _fields_ = [("BasicLimitInformation", BasicLimit), ("IoInfo", IoCounters), ("ProcessMemoryLimit", ctypes.c_size_t),
                        ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        ntdll = ctypes.WinDLL("ntdll")
        kernel32.CreateJobObjectW.restype = wintypes.HANDLE
        job = kernel32.CreateJobObjectW(None, None)
        limit = ExtendedLimit()
        limit.BasicLimitInformation.LimitFlags = _kJobObjectLimitKillOnJobClose
        if self.cpuCount > 0:
            systemCount = os.cpu_count() or self.cpuCount
            limit.BasicLimitInformation.LimitFlags |= _kJobObjectLimitAffinity
            limit.BasicLimitInformation.Affinity = (1 << min(self.cpuCount, systemCount, 64)) - 1
        if self.memoryMegabytes > 0:
            limit.BasicLimitInformation.LimitFlags |= _kJobObjectLimitProcessMemory
            limit.ProcessMemoryLimit = self.memoryMegabytes * 1024 * 1024
        processHandle = wintypes.HANDLE(int(process._handle))  # noqa: SLF001 — Popen 이 든 프로세스 핸들(공개 API 가 없다)
        bOk = bool(job) and bool(kernel32.SetInformationJobObject(wintypes.HANDLE(job), _kJobObjectExtendedLimitInformation, ctypes.byref(limit),
                                                                      ctypes.sizeof(limit)))
        bOk = bOk and bool(kernel32.AssignProcessToJobObject(wintypes.HANDLE(job), processHandle))
        if not bOk:
            error = ctypes.get_last_error()
            process.kill()
            raise RuntimeError(f"could not put the process in a limited job object (error {error})")
        ntdll.NtResumeProcess(processHandle)
        return job
