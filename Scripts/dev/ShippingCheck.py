#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/ShippingCheck.py

배포 구성 확인 — 배포 프리셋 셋(클라이언트 + 서버 · 클라이언트 전용 · 전용 서버)을 구성 · 빌드하고, 산출물을 잠깐 띄워 기동 · 종료를 본다.
배포 구성에는 테스트 장치(자동화 실행기 · 테스트용 전역 변수)가 없다 — 그래서 실행 인자로 조종하지 않고 운영과 같은 길(창 닫기 · 종료 신호)로 끝낸다.
통합 단계에서 한 번 돌린다(일상 개발 빌드는 Dev 프리셋이라 배포 구성의 컴파일 · 링크 · 쿠킹 결함이 여기서만 드러난다).

| 프리셋 | 띄우는 것 | 통과 |
| --- | --- | --- |
| `Ninja-Shipping` | `App -unattended` 를 `kAppRunSeconds` 동안 돌린 뒤 창을 닫는다(WM_CLOSE) | 그동안 살아 있고 0 으로 끝난다 |
| `Ninja-Shipping` | `App -unattended -scenario=<아무 경로>` | 0 이 아닌 코드로 곧 끝나고, 로그에 자동화 없음 오류가 있다(배포 구성은 실행기를 짓지 않는다) |
| `Ninja-Shipping-Client` | 클라이언트 전용 `App`(Server 폴더를 짓지 않는다)을 같은 길로 띄우고 닫는다 | 그동안 살아 있고 0 으로 끝난다 |
| `Ninja-Shipping-Server` | `Server` 를 `kServerRunSeconds` 동안 돌린 뒤 종료 신호(Windows Ctrl+Break · POSIX SIGTERM) | 그동안 살아 있고 0 으로 끝난다 |

배포 실행 파일에 테스트용 전역 변수 이름(`SW_TEST_GLOBAL_VARIABLE` 의 `gv_…`)이 하나라도 남아 있으면 실패다 — 배포 구성에서 그 변수는 상수라
등록(이름 글)도 읽는 분기도 접혀 사라져야 한다. 남았다면 상수 접기가 안 되는 자리(주소를 넘기는 곳 · 쓰는 곳)이니 그 자리를 배포 구성에서 뺀다.

로그는 `build/<프리셋>/Testing/Logs/ShippingCheck.<이름>.log` 이다. 끝에 로그마다 [Error] · [Warning] 줄을 모아 찍는다 — 기대한 줄이 아닌 [Error] 가 있으면 실패다.

사용법:
    py -3 -m Scripts shipping-check                 # 빌드 + 실행
    py -3 -m Scripts shipping-check --skip-build    # 지어 둔 산출물만 띄운다
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import AppArguments  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.HostPlatform import isWindowsHost, kHost  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.RunProcess import runProcess  # noqa: E402

#: 배포 App 을 살려 두는 시간(초) — 기동 · 첫 씬 · 몇 프레임이 지나기에 넉넉하다.
kAppRunSeconds = 8.0
#: 창을 닫은 뒤 · 서버 · 기동 오류 실행이 끝나기를 기다리는 시간(초).
kExitTimeoutSeconds = 60.0
#: 배포 서버를 살려 두는 시간(초) — 기동 · 몇 틱이 지나기에 넉넉하다. 그 뒤 종료 신호를 보낸다.
kServerRunSeconds = 6.0
#: 배포 구성이 `-scenario` 를 받았을 때 남기는 오류(`App::startAutomation` — 실행기의 자기 등록 `IAutomationRuntime` 이 없을 때).
kNoAutomationError = "Shipping has no automation runner"
kLogLinePattern = re.compile(r"\[(Error|Warning)\]")
#: 테스트용 전역 변수 정의(`SW_TEST_GLOBAL_VARIABLE( 형, gv_이름, …)`) — 배포 실행 파일에 이 이름이 남으면 안 된다.
kTestVariablePattern = re.compile(r"SW_TEST_GLOBAL_VARIABLE\(\s*[^,]+?,\s*(gv_\w+)")
kExecutableSuffix = kHost.executableSuffix


@dataclass
class SmokeResult:
    """띄운 실행 하나의 결과."""

    name: str
    logPath: Path
    bPassed: bool
    reason: str
    listExpectedError: list[str] = field(default_factory=list)


# 빌드의 쿠킹 단계(CookAssets — cmake/Engine/AssetAndToolTargets.cmake 의 `-log-file`)가 남기는 로그 이름이다. 두 곳이 같은 이름을 쓴다.
kCookLogName = "Cook.log"


def findCookLogPath(projectRoot: Path, preset: str) -> Path:
    return projectRoot / "build" / preset / "Testing" / "Logs" / kCookLogName


def reportCookLog(projectRoot: Path, preset: str) -> None:
    """실패했을 때 그 프리셋의 쿠킹 로그 [Error] · [Warning] 줄을 함께 찍는다 — 팩에 빠진 것(셰이더 바이너리)이 굽기에서 왔는지 본다."""
    cookLog = findCookLogPath(projectRoot, preset)
    print(f"[shipping-check] cook log of {preset}: {cookLog}", flush=True)
    reportLogLines(SmokeResult(f"Cook ({preset})", cookLog, True, ""))


def findBinDir(projectRoot: Path, preset: str) -> Path:
    return projectRoot / "build" / preset / "Bin"


def makeLogPath(projectRoot: Path, preset: str, name: str) -> Path:
    logDir = projectRoot / "build" / preset / "Testing" / "Logs"
    logDir.mkdir(parents=True, exist_ok=True)
    logPath = logDir / f"ShippingCheck.{name}.log"
    # 지난 실행의 로그를 지운다 — 배포 로거는 경고 이상만 쓰고 쓸 줄이 없으면 파일을 만들지 않아, 남은 파일을 이번 결과로 읽게 된다.
    logPath.unlink(missing_ok=True)
    return logPath


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
    if isWindowsHost():
        subprocess.run(["taskkill", "/PID", str(process.pid)], capture_output=True, check=False)
    else:
        process.terminate()


def runApp(projectRoot: Path, preset: str = "Ninja-Shipping") -> SmokeResult:
    """배포 App 을 띄우고, 살아 있는지 본 뒤 창을 닫아 정상 종료를 본다."""
    logPath = makeLogPath(projectRoot, preset, "App")
    executable = findBinDir(projectRoot, preset) / f"App{kExecutableSuffix}"
    if executable.is_file() is False:
        return SmokeResult(f"App ({preset})", logPath, False, f"{executable} is missing")
    process = subprocess.Popen([str(executable), AppArguments.flag("unattended"), AppArguments.option("log-file", logPath)], cwd=projectRoot)
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


def sendShutdownSignal(process: subprocess.Popen) -> None:
    """운영과 같은 종료 신호를 보낸다 — Windows 는 새 프로세스 그룹에 Ctrl+Break(콘솔 그룹 전체로 가지 않게), POSIX 는 SIGTERM."""
    if isWindowsHost():
        os.kill(process.pid, signal.CTRL_BREAK_EVENT)
    else:
        process.send_signal(signal.SIGTERM)


def runClientApp(projectRoot: Path) -> SmokeResult:
    """클라이언트 전용 배포 App(Server 폴더 · 서버 서비스 구현이 없다)을 같은 길로 띄우고 닫는다."""
    return runApp(projectRoot, "Ninja-Shipping-Client")


def runServer(projectRoot: Path) -> SmokeResult:
    """배포 서버를 띄우고, 살아 있는지 본 뒤 종료 신호를 보내 정상 종료를 본다."""
    preset = "Ninja-Shipping-Server"
    logPath = makeLogPath(projectRoot, preset, "Server")
    executable = findBinDir(projectRoot, preset) / f"Server{kExecutableSuffix}"
    if executable.is_file() is False:
        return SmokeResult("Server", logPath, False, f"{executable} is missing")
    creationFlags = subprocess.CREATE_NEW_PROCESS_GROUP if isWindowsHost() else 0
    process = subprocess.Popen([str(executable), AppArguments.option("log-file", logPath)], cwd=projectRoot, creationflags=creationFlags)
    try:
        process.wait(timeout=kServerRunSeconds)
        return SmokeResult("Server", logPath, False, f"exited early with {process.returncode}")
    except subprocess.TimeoutExpired:
        pass
    sendShutdownSignal(process)
    try:
        exitCode = process.wait(timeout=kExitTimeoutSeconds)
    except subprocess.TimeoutExpired:
        process.kill()
        return SmokeResult("Server", logPath, False, f"did not exit within {kExitTimeoutSeconds}s after the shutdown signal")
    return SmokeResult("Server", logPath, exitCode == 0, f"exit code {exitCode}")


def runNoAutomationScenario(projectRoot: Path) -> SmokeResult:
    """배포 App 이 `-scenario` 를 기동 오류로 거절하는지 본다 — 배포 구성은 자동화 실행기를 짓지 않는다."""
    preset = "Ninja-Shipping"
    logPath = makeLogPath(projectRoot, preset, "Scenario")
    executable = findBinDir(projectRoot, preset) / f"App{kExecutableSuffix}"
    if executable.is_file() is False:
        return SmokeResult("NoAutomation", logPath, False, f"{executable} is missing")
    result = runProcess([executable, AppArguments.flag("unattended"), AppArguments.option("scenario", "Test/Scenario/none.xml"), AppArguments.option("log-file", logPath)], cwd=projectRoot,
                        timeoutSeconds=kExitTimeoutSeconds)
    bLogged = logPath.is_file() and kNoAutomationError in logPath.read_text(encoding="utf-8", errors="replace")
    bPassed = result.bLaunched and not result.bTimedOut and result.returnCode != 0 and bLogged
    return SmokeResult("NoAutomation", logPath, bPassed, f"exit code {result.returnCode}, startup error logged: {bLogged}", [kNoAutomationError])


def collectTestVariableNames(projectRoot: Path) -> list[str]:
    """`Source/` 의 테스트용 전역 변수 이름(테스트 폴더 밖 — 테스트 폴더는 배포 구성이 짓지 않는다)."""
    setName: set[str] = set()
    for path in (projectRoot / "Source").rglob("*.cpp"):
        if "Tests" in path.parts:
            continue
        setName.update(kTestVariablePattern.findall(path.read_text(encoding="utf-8", errors="replace")))
    return sorted(setName)


def checkNoTestVariables(projectRoot: Path) -> SmokeResult:
    """배포 실행 파일(App · Server)에 테스트용 전역 변수 이름이 남지 않았는지 본다."""
    listName = collectTestVariableNames(projectRoot)
    listLeft: list[str] = []
    # 이름 모두를 한 정규식(긴 이름 먼저)으로 실행 파일을 한 번만 훑는다 — 이름마다 실행 파일 전체를 훑으면 이름 수 × 실행 파일 크기다.
    # 하나라도 남았는지(통과 · 실패)는 이름마다 찾던 것과 같다. 다른 이름을 품은 긴 이름이 남으면 긴 이름만 적힌다.
    namePattern = re.compile(b"|".join(re.escape(name.encode("ascii")) for name in sorted(listName, key=len, reverse=True))) if listName else None
    for preset, stem in (("Ninja-Shipping", "App"), ("Ninja-Shipping-Client", "App"), ("Ninja-Shipping-Server", "Server")):
        executable = findBinDir(projectRoot, preset) / f"{stem}{kExecutableSuffix}"
        if executable.is_file() is False:
            return SmokeResult("NoTestVariables", executable, False, f"{executable} is missing")
        if namePattern is not None:
            listLeft += [f"{stem}:{name}" for name in sorted({match.decode("ascii") for match in namePattern.findall(executable.read_bytes())})]
    reason = f"{len(listName)} test variable names checked" if not listLeft else f"left in the shipping binaries: {', '.join(listLeft[:20])}"
    return SmokeResult("NoTestVariables", findBinDir(projectRoot, "Ninja-Shipping"), not listLeft, reason)


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

    listRun = ((runApp, "Ninja-Shipping"), (runClientApp, "Ninja-Shipping-Client"), (runServer, "Ninja-Shipping-Server"), (runNoAutomationScenario, "Ninja-Shipping"))
    if args.skip_build is False:
        listFailedBuild = [preset for preset in dict.fromkeys(preset for _, preset in listRun) if buildPreset(projectRoot, preset) is False]
        if listFailedBuild:
            print(f"[shipping-check] FAILED build: {', '.join(listFailedBuild)}", flush=True)
            for preset in listFailedBuild:
                reportCookLog(projectRoot, preset)
            return ExitCode.Violation

    bAllPassed = True
    variableCheck = checkNoTestVariables(projectRoot)
    print(f"[shipping-check] {variableCheck.name}: {'PASS' if variableCheck.bPassed else 'FAIL'} - {variableCheck.reason}", flush=True)
    bAllPassed = variableCheck.bPassed
    listFailedPreset: list[str] = []
    for run, preset in listRun:
        result = run(projectRoot)
        print(f"[shipping-check] {result.name}: {'PASS' if result.bPassed else 'FAIL'} - {result.reason} ({result.logPath})", flush=True)
        unexpectedErrorCount = reportLogLines(result)
        if result.bPassed is False or unexpectedErrorCount > 0:
            bAllPassed = False
            listFailedPreset.append(preset)
    for preset in dict.fromkeys(listFailedPreset):
        reportCookLog(projectRoot, preset)  # 실행이 진 까닭이 팩(빠진 셰이더 · 데이터)이면 굽기 로그에 그 줄이 있다
    return ExitCode.Passed if bAllPassed else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
