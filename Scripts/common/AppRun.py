#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
빌드된 App 을 한 판 돌리는 자리 — 출력 줄 모으기 · 이 기계에서 못 도는 백엔드 판정 · 프로파일 표 읽기 · 프로세스 자원 재기.

QA 러너 셋(`Scripts/qa/GoldenImages.py` · `Soak.py` · `PerfRegression.py`)이 같은 규칙을 쓴다. 규칙은 `Test/AppTest/TestAppSmoke.cpp`
(`AppSmokeTest`)와 같다 — 종료 코드 0, `[Error]` 0 줄, 백엔드가 없거나 드라이버가 기능을 안 주면 그 판은 결함이 아니라 환경이다.

자원은 **밖에서** 잰다(엔진은 주기적으로 메모리 · 핸들을 남기지 않는다): Windows 는 `GetProcessMemoryInfo` · `GetProcessHandleCount` ·
`GetGuiResources`, 리눅스는 `/proc/<pid>/status` · `/proc/<pid>/fd`. 외부 패키지(psutil)는 쓰지 않는다.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

from .BuildTree import BuildTree
from .CookContract import CookContractSpec

#: 백엔드 명령줄 이름 → App 스위치. 표는 Config/Engine/CookContract.json 의 `command_line_name`(표 순서).
kBackendSwitch: dict[str, str] = CookContractSpec.load().mapBackendSwitch


def addAppRunArguments(parser: argparse.ArgumentParser, *, bMultipleBackends: bool, defaultBackend: str = "dx12") -> None:
    """App 을 돌리는 QA · dev 스크립트의 공통 인자 — `--app`(빌드된 App), `--game`(기본: 그 빌드의 SW_ACTIVE_GAME), 백엔드(표: CookContract.json)."""
    parser.add_argument("--app", type=Path, required=True, help="빌드된 App 실행 파일(그 빌드가 게임을 정한다)")
    parser.add_argument("--game", default=None, help="게임 이름(기본: App 빌드의 SW_ACTIVE_GAME)")
    if bMultipleBackends:
        parser.add_argument("--backends", nargs="+", default=list(kBackendSwitch), choices=list(kBackendSwitch), help="돌릴 백엔드들")
    else:
        parser.add_argument("--backend", default=defaultBackend, choices=list(kBackendSwitch), help="돌릴 백엔드")

#: App 이 "이 기계에서는 이 백엔드를 못 돌린다"(빌드에 없다 · 드라이버가 기능을 안 준다)로 끝날 때의 종료 코드
#: (`kRHIUnusableHereExitCode`, `Source/Engine/Graphics/RHI/RHIInitResult.h` 와 같다).
kAppRhiUnusableExitCode = 77

#: ctest 가 "건너뜀" 으로 읽는 종료 코드(`SKIP_RETURN_CODE`).
kSkipExitCode = 77

#: 프로파일 표의 한 줄 — `[Profile] <구간>  avg  p50  p99  min  max  per_frame`(`FrameProfiler::report`).
_kProfileRowRe = re.compile(r"\[Profile\]\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\.(\d)\s*$")
#: `[Profile] wall  N frames in M ms  = U us/frame`
_kProfileWallRe = re.compile(r"\[Profile\] wall\s+(\d+) frames in (\d+) ms\s+=\s+(\d+) us/frame")


@dataclass
class ResourceSample:
    """프로세스 자원 한 번 잰 값."""

    seconds: float
    privateBytes: int
    workingSetBytes: int
    handleCount: int
    guiObjectCount: int


@dataclass
class AppRunResult:
    exitCode: int | None = None
    listLine: list[str] = field(default_factory=list)
    listErrorLine: list[str] = field(default_factory=list)
    listSample: list[ResourceSample] = field(default_factory=list)
    bLaunched: bool = False
    bBackendUnusable: bool = False
    bTimedOut: bool = False
    seconds: float = 0.0

    @property
    def bClean(self) -> bool:
        return self.bLaunched and not self.bTimedOut and self.exitCode == 0 and not self.listErrorLine


@dataclass(frozen=True)
class ProfileRow:
    scope: str
    avgMicro: int
    p50Micro: int
    p99Micro: int
    minMicro: int
    maxMicro: int
    perFrame: float


def parseProfileTable(listLine: list[str]) -> dict[str, ProfileRow]:
    """App 출력에서 프로파일 표를 읽습니다(구간 이름 → 행). 표가 없으면 빈 사전입니다."""
    mapRow: dict[str, ProfileRow] = {}
    for line in listLine:
        match = _kProfileRowRe.search(line)
        if match is None:
            continue
        scope = match.group(1)
        values = [int(match.group(index)) for index in range(2, 7)]
        mapRow[scope] = ProfileRow(scope, *values, float(f"{match.group(7)}.{match.group(8)}"))
    return mapRow


def parseProfileWall(listLine: list[str]) -> tuple[int, int, int] | None:
    """(프레임 수, 밀리초, 프레임당 us) — 측정 구간의 벽시계. 없으면 None."""
    for line in listLine:
        match = _kProfileWallRe.search(line)
        if match is not None:
            return int(match.group(1)), int(match.group(2)), int(match.group(3))
    return None


def findUsableBackends(tree: BuildTree) -> list[str]:
    """그 빌드의 App 이 받는 백엔드 — Dev 는 넷 다(모듈), Shipping 은 링크한 하나뿐이다(다른 스위치는 기동 오류다)."""
    if not tree.bShipping:
        return list(kBackendSwitch)
    backend = CookContractSpec.load().findBackend(tree.readCacheValue("SW_SHIPPING_RHI_BACKEND") or "")
    return [backend.commandLineName] if backend else []


def loadGameTable(repositoryRoot: Path) -> dict:
    """QA 러너가 함께 쓰는 게임 표(`Test/Qa/Games.json`) — 자동 플레이 인자 · 캡처 프레임."""
    return json.loads((repositoryRoot / "Test/Qa/Games.json").read_text(encoding="utf-8"))


# ------------------------------------------------------------------------------
# 프로세스 자원
# ------------------------------------------------------------------------------
def sampleProcessResources(process: subprocess.Popen, startSeconds: float) -> ResourceSample | None:
    """도는 프로세스의 자원을 한 번 잽니다. 이미 끝났거나 잴 수 없으면 None 입니다."""
    if process.poll() is not None:
        return None
    elapsed = time.monotonic() - startSeconds
    if sys.platform == "win32":
        return sampleWindowsInternal(process.pid, elapsed)
    return sampleLinuxInternal(process.pid, elapsed)


def sampleWindowsInternal(pid: int, elapsed: float) -> ResourceSample | None:
    import ctypes
    import ctypes.wintypes as wintypes

    class ProcessMemoryCountersEx(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    handle = kernel32.OpenProcess(0x1000 | 0x0010, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
    if not handle:
        return None
    try:
        counters = ProcessMemoryCountersEx()
        counters.cb = ctypes.sizeof(ProcessMemoryCountersEx)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return None
        handleCount = wintypes.DWORD(0)
        kernel32.GetProcessHandleCount(handle, ctypes.byref(handleCount))
        guiCount = user32.GetGuiResources(handle, 0) + user32.GetGuiResources(handle, 1)  # GDI + USER
        return ResourceSample(elapsed, int(counters.PrivateUsage), int(counters.WorkingSetSize), int(handleCount.value), int(guiCount))
    finally:
        kernel32.CloseHandle(handle)


def sampleLinuxInternal(pid: int, elapsed: float) -> ResourceSample | None:
    try:
        status = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
        handleCount = len(list(Path(f"/proc/{pid}/fd").iterdir()))
    except OSError:
        return None
    values: dict[str, int] = {}
    for line in status.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] in ("VmRSS:", "RssAnon:"):
            values[parts[0]] = int(parts[1]) * 1024
    return ResourceSample(elapsed, values.get("RssAnon:", 0), values.get("VmRSS:", 0), handleCount, 0)


# ------------------------------------------------------------------------------
# 한 판 돌리기
# ------------------------------------------------------------------------------
def runApp(appPath: Path, listArgument: list[str], *, cwd: Path | None = None, timeoutSeconds: float = 300.0,
           sampleIntervalSeconds: float = 0.0, echo: bool = False) -> AppRunResult:
    """
    App 을 띄워 끝날 때까지 기다립니다. 출력은 별도 스레드가 계속 읽는다(파이프가 차서 App 이 멈추지 않게).
    `sampleIntervalSeconds` 가 0 보다 크면 그 간격으로 자원을 잰다. 시간을 넘기면 죽이고 `bTimedOut` 이다.
    작업 폴더는 기본으로 App 이 있는 `Bin` 이다(App 은 거기서 위로 `Resource/` 를 찾는다).
    """
    result = AppRunResult()
    workingDirectory = Path(cwd) if cwd is not None else Path(appPath).resolve().parent
    startSeconds = time.monotonic()
    try:
        process = subprocess.Popen([str(appPath), *listArgument], cwd=str(workingDirectory), stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace")
    except OSError as error:
        result.listErrorLine.append(f"cannot launch {appPath}: {error}")
        return result
    result.bLaunched = True

    def readOutputInternal() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            line = line.rstrip("\r\n")
            result.listLine.append(line)
            if echo:
                print(line)

    reader = threading.Thread(target=readOutputInternal, daemon=True)
    reader.start()
    nextSample = startSeconds
    while process.poll() is None:
        now = time.monotonic()
        if now - startSeconds > timeoutSeconds:
            result.bTimedOut = True
            process.kill()
            break
        if sampleIntervalSeconds > 0.0 and now >= nextSample:
            sample = sampleProcessResources(process, startSeconds)
            if sample is not None:
                result.listSample.append(sample)
            nextSample = now + sampleIntervalSeconds
        time.sleep(0.05 if sampleIntervalSeconds <= 0.0 else min(0.25, sampleIntervalSeconds))
    process.wait()
    reader.join(timeout=10.0)
    result.exitCode = process.returncode
    result.seconds = time.monotonic() - startSeconds
    result.bBackendUnusable = result.exitCode == kAppRhiUnusableExitCode
    for line in result.listLine:
        if "[Error]" in line:
            result.listErrorLine.append(line)
    return result


def computeSlopePerMinute(listPoint: list[tuple[float, float]]) -> float:
    """(초, 값) 점들의 최소제곱 기울기를 분당으로 돌려줍니다. 점이 둘 미만이면 0 입니다."""
    if len(listPoint) < 2:
        return 0.0
    count = len(listPoint)
    meanX = sum(point[0] for point in listPoint) / count
    meanY = sum(point[1] for point in listPoint) / count
    denominator = sum((point[0] - meanX) ** 2 for point in listPoint)
    if denominator == 0.0:
        return 0.0
    return 60.0 * sum((point[0] - meanX) * (point[1] - meanY) for point in listPoint) / denominator
