#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/ShippingCheck.py

배포 구성 확인 — 배포 프리셋 셋(클라이언트 · 전용 서버 · 자동화를 뺀 클라이언트)을 구성 · 빌드하고, 산출물을 잠깐 띄워 기동 · 종료를 본다.
통합 단계에서 한 번 돌린다(일상 개발 빌드는 Dev 프리셋이라 배포 구성의 컴파일 · 링크 · 쿠킹 결함이 여기서만 드러난다).

| 프리셋 | 띄우는 것 | 통과 |
| --- | --- | --- |
| `Ninja-Shipping` | `App -unattended` 를 `kAppRunSeconds` 동안 돌린 뒤 창을 닫는다(WM_CLOSE) | 그동안 살아 있고 0 으로 끝난다 |
| `Ninja-Shipping-Server` | `Server -gv_serverExitAfterTicks=<틱>` | 0 으로 끝난다 |
| `Ninja-Shipping-NoAutomation` | `App -unattended -scenario=<아무 경로>` | 0 이 아닌 코드로 곧 끝나고, 로그에 자동화 없음 오류가 있다 |

로그는 `build/<프리셋>/Testing/Logs/ShippingCheck.<이름>.log` 이다. 끝에 로그마다 [Error] · [Warning] 줄을 모아 찍는다 — 기대한 줄이 아닌 [Error] 가 있으면 실패다.

사용법:
    py -3 -m Scripts shipping-check                 # 빌드 + 실행
    py -3 -m Scripts shipping-check --skip-build    # 지어 둔 산출물만 띄운다
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.RunProcess import runProcess  # noqa: E402

#: 배포 App 을 살려 두는 시간(초) — 기동 · 첫 씬 · 몇 프레임이 지나기에 넉넉하다.
kAppRunSeconds = 8.0
#: 창을 닫은 뒤 · 서버 · 기동 오류 실행이 끝나기를 기다리는 시간(초).
kExitTimeoutSeconds = 60.0
#: 전용 서버가 스스로 끝날 틱 수.
kServerExitTicks = 120
#: 자동화를 뺀 구성이 `-scenario` 를 받았을 때 남기는 오류(`AppAutomationHostOff.cpp`).
kNoAutomationError = "need a build with SW_ENABLE_AUTOMATION=ON"
kLogLinePattern = re.compile(r"\[(Error|Warning)\]")
kExecutableSuffix = ".exe" if sys.platform == "win32" else ""


@dataclass
class SmokeResult:
    """띄운 실행 하나의 결과."""

    name: str
    logPath: Path
    bPassed: bool
    reason: str
    listExpectedError: list[str] = field(default_factory=list)


def findBinDir(projectRoot: Path, preset: str) -> Path:
    return projectRoot / "build" / preset / "Bin"


def makeLogPath(projectRoot: Path, preset: str, name: str) -> Path:
    logDir = projectRoot / "build" / preset / "Testing" / "Logs"
    logDir.mkdir(parents=True, exist_ok=True)
    return logDir / f"ShippingCheck.{name}.log"


def buildPreset(projectRoot: Path, preset: str) -> bool:
    """프리셋 하나를 구성하고 빌드한다(빌드에 쿠킹이 든다)."""
    for command in (["cmake", "--preset", preset], ["cmake", "--build", "--preset", preset]):
        print(f"[shipping-check] {' '.join(command)}", flush=True)
        if runProcess(command, cwd=projectRoot, bCapture=False).returnCode != 0:
            print(f"[shipping-check] FAILED {' '.join(command)}", flush=True)
            return False
    return True


def closeProcessWindow(process: subprocess.Popen[bytes]) -> None:
    """창에 닫기를 보낸다 — 사용자가 창을 닫은 것과 같은 길(강제 종료가 아니다)."""
    if sys.platform == "win32":
        subprocess.run(["taskkill", "/PID", str(process.pid)], capture_output=True, check=False)
    else:
        process.terminate()


def runApp(projectRoot: Path) -> SmokeResult:
    """배포 App 을 띄우고, 살아 있는지 본 뒤 창을 닫아 정상 종료를 본다."""
    preset = "Ninja-Shipping"
    logPath = makeLogPath(projectRoot, preset, "App")
    executable = findBinDir(projectRoot, preset) / f"App{kExecutableSuffix}"
    if executable.is_file() is False:
        return SmokeResult("App", logPath, False, f"{executable} is missing")
    process = subprocess.Popen([str(executable), "-unattended", f"-log-file={logPath}"], cwd=projectRoot)
    try:
        process.wait(timeout=kAppRunSeconds)
        return SmokeResult("App", logPath, False, f"exited early with {process.returncode}")
    except subprocess.TimeoutExpired:
        pass
    closeProcessWindow(process)
    try:
        exitCode = process.wait(timeout=kExitTimeoutSeconds)
    except subprocess.TimeoutExpired:
        process.kill()
        return SmokeResult("App", logPath, False, f"did not exit within {kExitTimeoutSeconds}s after close")
    return SmokeResult("App", logPath, exitCode == 0, f"exit code {exitCode}")


def runServer(projectRoot: Path) -> SmokeResult:
    """배포 서버를 정해진 틱만 돌린다."""
    preset = "Ninja-Shipping-Server"
    logPath = makeLogPath(projectRoot, preset, "Server")
    executable = findBinDir(projectRoot, preset) / f"Server{kExecutableSuffix}"
    if executable.is_file() is False:
        return SmokeResult("Server", logPath, False, f"{executable} is missing")
    result = runProcess([executable, f"-gv_serverExitAfterTicks={kServerExitTicks}", f"-log-file={logPath}"], cwd=projectRoot,
                        timeoutSeconds=kExitTimeoutSeconds)
    return SmokeResult("Server", logPath, result.bSucceeded, f"exit code {result.returnCode}{' (timed out)' if result.bTimedOut else ''}")


def runNoAutomationScenario(projectRoot: Path) -> SmokeResult:
    """자동화를 뺀 배포 App 이 `-scenario` 를 기동 오류로 거절하는지 본다."""
    preset = "Ninja-Shipping-NoAutomation"
    logPath = makeLogPath(projectRoot, preset, "Scenario")
    executable = findBinDir(projectRoot, preset) / f"App{kExecutableSuffix}"
    if executable.is_file() is False:
        return SmokeResult("NoAutomation", logPath, False, f"{executable} is missing")
    result = runProcess([executable, "-unattended", "-scenario=Test/Scenario/none.xml", f"-log-file={logPath}"], cwd=projectRoot,
                        timeoutSeconds=kExitTimeoutSeconds)
    bLogged = logPath.is_file() and kNoAutomationError in logPath.read_text(encoding="utf-8", errors="replace")
    bPassed = result.bLaunched and not result.bTimedOut and result.returnCode != 0 and bLogged
    return SmokeResult("NoAutomation", logPath, bPassed, f"exit code {result.returnCode}, startup error logged: {bLogged}", [kNoAutomationError])


def reportLogLines(result: SmokeResult) -> int:
    """로그의 [Error] · [Warning] 줄을 같은 줄끼리 세어 찍고, 기대하지 않은 [Error] 줄 수를 돌려준다."""
    if result.logPath.is_file() is False:
        print(f"  (no log at {result.logPath})")
        return 0
    counts: dict[str, int] = {}
    for line in result.logPath.read_text(encoding="utf-8", errors="replace").splitlines():
        if kLogLinePattern.search(line):
            body = line.split("] ", 1)[1] if line.startswith("[") and "] " in line else line  # 앞의 시각을 뗀다
            counts[body] = counts.get(body, 0) + 1
    unexpectedErrorCount = 0
    for body, count in sorted(counts.items(), key=lambda item: -item[1]):
        bExpected = any(expected in body for expected in result.listExpectedError)
        if "[Error]" in body and bExpected is False:
            unexpectedErrorCount += count
        print(f"  {count:4} {body}{'  (expected)' if bExpected else ''}")
    return unexpectedErrorCount


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="배포 프리셋을 빌드하고 산출물의 기동 · 종료를 본다")
    parser.add_argument("--skip-build", action="store_true", help="빌드를 건너뛰고 지어 둔 산출물만 띄운다")
    args = parser.parse_args(argv)
    projectRoot = getProjectRoot()

    listRun = ((runApp, "Ninja-Shipping"), (runServer, "Ninja-Shipping-Server"), (runNoAutomationScenario, "Ninja-Shipping-NoAutomation"))
    if args.skip_build is False:
        listFailedBuild = [preset for _, preset in listRun if buildPreset(projectRoot, preset) is False]
        if listFailedBuild:
            print(f"[shipping-check] FAILED build: {', '.join(listFailedBuild)}", flush=True)
            return ExitCode.Violation

    bAllPassed = True
    for run, _ in listRun:
        result = run(projectRoot)
        print(f"[shipping-check] {result.name}: {'PASS' if result.bPassed else 'FAIL'} - {result.reason} ({result.logPath})", flush=True)
        unexpectedErrorCount = reportLogLines(result)
        if result.bPassed is False or unexpectedErrorCount > 0:
            bAllPassed = False
    return ExitCode.Passed if bAllPassed else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
