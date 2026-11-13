#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/dev/shipping_check.py

배포 구성 확인 — 배포 프리셋 셋(클라이언트 + 서버 · 클라이언트 전용 · 전용 서버)을 구성 · 빌드하고, 산출물을 잠깐 띄워 기동 · 종료를 본다.
배포 구성에는 테스트 장치(자동화 실행기 · 테스트용 전역 변수)가 없다 — 그래서 실행 인자로 조종하지 않고 운영과 같은 길(창 닫기 · 종료 신호)로 끝낸다.
통합 단계에서 한 번 돌린다(일상 개발 빌드는 Dev 프리셋이라 배포 구성의 컴파일 · 링크 · 쿠킹 결함이 여기서만 드러난다).

| 프리셋 | 띄우는 것 | 통과 |
| --- | --- | --- |
| `Ninja-Shipping` | `App -unattended` 를 `APP_RUN_SECONDS` 동안 돌린 뒤 창을 닫는다(WM_CLOSE) | 그동안 살아 있고 0 으로 끝난다 |
| `Ninja-Shipping` | `App -unattended -scenario=<아무 경로>` | 0 이 아닌 코드로 곧 끝나고, 로그에 자동화 없음 오류가 있다(배포 구성은 실행기를 짓지 않는다) |
| `Ninja-Shipping-Client` | 클라이언트 전용 `App`(Server 폴더를 짓지 않는다)을 같은 길로 띄우고 닫는다 | 그동안 살아 있고 0 으로 끝난다 |
| `Ninja-Shipping-Server` | `Server` 를 `SERVER_RUN_SECONDS` 동안 돌린 뒤 종료 신호(Windows Ctrl+Break · POSIX SIGTERM) | 그동안 살아 있고 0 으로 끝난다 |

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
from common import app_arguments  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.host_platform import is_windows_host, HOST  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from common.run_process import run_process  # noqa: E402

#: 배포 App 을 살려 두는 시간(초) — 기동 · 첫 씬 · 몇 프레임이 지나기에 넉넉하다.
APP_RUN_SECONDS = 8.0
#: 창을 닫은 뒤 · 서버 · 기동 오류 실행이 끝나기를 기다리는 시간(초).
EXIT_TIMEOUT_SECONDS = 60.0
#: 배포 서버를 살려 두는 시간(초) — 기동 · 몇 틱이 지나기에 넉넉하다. 그 뒤 종료 신호를 보낸다.
SERVER_RUN_SECONDS = 6.0
#: 배포 구성이 `-scenario` 를 받았을 때 남기는 오류(`App::startAutomation` — 실행기의 자기 등록 `IAutomationRuntime` 이 없을 때).
NO_AUTOMATION_ERROR = "Shipping has no automation runner"
LOG_LINE_PATTERN = re.compile(r"\[(Error|Warning)\]")
#: 테스트용 전역 변수 정의(`SW_TEST_GLOBAL_VARIABLE( 형, gv_이름, …)`) — 배포 실행 파일에 이 이름이 남으면 안 된다.
TEST_VARIABLE_PATTERN = re.compile(r"SW_TEST_GLOBAL_VARIABLE\(\s*[^,]+?,\s*(gv_\w+)")
EXECUTABLE_SUFFIX = HOST.executable_suffix


@dataclass
class SmokeResult:
    """띄운 실행 하나의 결과."""

    name: str
    log_path: Path
    passed: bool
    reason: str
    list_expected_error: list[str] = field(default_factory=list)


# 빌드의 쿠킹 단계(CookAssets — cmake/Engine/AssetAndToolTargets.cmake 의 `-log-file`)가 남기는 로그 이름이다. 두 곳이 같은 이름을 쓴다.
COOK_LOG_NAME = "Cook.log"


def find_cook_log_path(project_root: Path, preset: str) -> Path:
    return project_root / "build" / preset / "Testing" / "Logs" / COOK_LOG_NAME


def report_cook_log(project_root: Path, preset: str) -> None:
    """실패했을 때 그 프리셋의 쿠킹 로그 [Error] · [Warning] 줄을 함께 찍는다 — 팩에 빠진 것(셰이더 바이너리)이 굽기에서 왔는지 본다."""
    cook_log = find_cook_log_path(project_root, preset)
    print(f"[shipping-check] cook log of {preset}: {cook_log}", flush=True)
    report_log_lines(SmokeResult(f"Cook ({preset})", cook_log, True, ""))


def find_bin_dir(project_root: Path, preset: str) -> Path:
    return project_root / "build" / preset / "Bin"


def make_log_path(project_root: Path, preset: str, name: str) -> Path:
    log_dir = project_root / "build" / preset / "Testing" / "Logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"ShippingCheck.{name}.log"
    # 지난 실행의 로그를 지운다 — 배포 로거는 경고 이상만 쓰고 쓸 줄이 없으면 파일을 만들지 않아, 남은 파일을 이번 결과로 읽게 된다.
    log_path.unlink(missing_ok=True)
    return log_path


def build_preset(project_root: Path, preset: str) -> bool:
    """프리셋 하나를 구성하고 빌드한다(빌드에 쿠킹이 든다)."""
    for command in (["cmake", "--preset", preset], ["cmake", "--build", "--preset", preset]):
        print(f"[shipping-check] {' '.join(command)}", flush=True)
        if run_process(command, cwd=project_root, capture=False).return_code != 0:
            print(f"[shipping-check] FAILED {' '.join(command)}", flush=True)
            return False
    return True


def close_process_window(process: subprocess.Popen[bytes]) -> None:
    """창에 닫기를 보낸다 — 사용자가 창을 닫은 것과 같은 길(강제 종료가 아니다)."""
    if is_windows_host():
        subprocess.run(["taskkill", "/PID", str(process.pid)], capture_output=True, check=False)
    else:
        process.terminate()


def run_app(project_root: Path, preset: str = "Ninja-Shipping") -> SmokeResult:
    """배포 App 을 띄우고, 살아 있는지 본 뒤 창을 닫아 정상 종료를 본다."""
    log_path = make_log_path(project_root, preset, "App")
    executable = find_bin_dir(project_root, preset) / f"App{EXECUTABLE_SUFFIX}"
    if executable.is_file() is False:
        return SmokeResult(f"App ({preset})", log_path, False, f"{executable} is missing")
    process = subprocess.Popen([str(executable), app_arguments.flag("unattended"), app_arguments.option("log-file", log_path)], cwd=project_root)
    try:
        process.wait(timeout=APP_RUN_SECONDS)
        return SmokeResult("App", log_path, False, f"exited early with {process.returncode}")
    except subprocess.TimeoutExpired:
        pass
    close_process_window(process)
    try:
        exit_code = process.wait(timeout=EXIT_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        return SmokeResult("App", log_path, False, f"did not exit within {EXIT_TIMEOUT_SECONDS}s after close")
    return SmokeResult("App", log_path, exit_code == 0, f"exit code {exit_code}")


def send_shutdown_signal(process: subprocess.Popen) -> None:
    """운영과 같은 종료 신호를 보낸다 — Windows 는 새 프로세스 그룹에 Ctrl+Break(콘솔 그룹 전체로 가지 않게), POSIX 는 SIGTERM."""
    if is_windows_host():
        os.kill(process.pid, signal.CTRL_BREAK_EVENT)
    else:
        process.send_signal(signal.SIGTERM)


def run_client_app(project_root: Path) -> SmokeResult:
    """클라이언트 전용 배포 App(Server 폴더 · 서버 서비스 구현이 없다)을 같은 길로 띄우고 닫는다."""
    return run_app(project_root, "Ninja-Shipping-Client")


def run_server(project_root: Path) -> SmokeResult:
    """배포 서버를 띄우고, 살아 있는지 본 뒤 종료 신호를 보내 정상 종료를 본다."""
    preset = "Ninja-Shipping-Server"
    log_path = make_log_path(project_root, preset, "Server")
    executable = find_bin_dir(project_root, preset) / f"Server{EXECUTABLE_SUFFIX}"
    if executable.is_file() is False:
        return SmokeResult("Server", log_path, False, f"{executable} is missing")
    creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if is_windows_host() else 0
    process = subprocess.Popen([str(executable), app_arguments.option("log-file", log_path)], cwd=project_root, creationflags=creation_flags)
    try:
        process.wait(timeout=SERVER_RUN_SECONDS)
        return SmokeResult("Server", log_path, False, f"exited early with {process.returncode}")
    except subprocess.TimeoutExpired:
        pass
    send_shutdown_signal(process)
    try:
        exit_code = process.wait(timeout=EXIT_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        return SmokeResult("Server", log_path, False, f"did not exit within {EXIT_TIMEOUT_SECONDS}s after the shutdown signal")
    return SmokeResult("Server", log_path, exit_code == 0, f"exit code {exit_code}")


def run_no_automation_scenario(project_root: Path) -> SmokeResult:
    """배포 App 이 `-scenario` 를 기동 오류로 거절하는지 본다 — 배포 구성은 자동화 실행기를 짓지 않는다."""
    preset = "Ninja-Shipping"
    log_path = make_log_path(project_root, preset, "Scenario")
    executable = find_bin_dir(project_root, preset) / f"App{EXECUTABLE_SUFFIX}"
    if executable.is_file() is False:
        return SmokeResult("NoAutomation", log_path, False, f"{executable} is missing")
    result = run_process([executable, app_arguments.flag("unattended"), app_arguments.option("scenario", "Test/Scenario/none.xml"), app_arguments.option("log-file", log_path)], cwd=project_root,
                        timeout_seconds=EXIT_TIMEOUT_SECONDS)
    logged = log_path.is_file() and NO_AUTOMATION_ERROR in log_path.read_text(encoding="utf-8", errors="replace")
    passed = result.launched and not result.timed_out and result.return_code != 0 and logged
    return SmokeResult("NoAutomation", log_path, passed, f"exit code {result.return_code}, startup error logged: {logged}", [NO_AUTOMATION_ERROR])


def collect_test_variable_names(project_root: Path) -> list[str]:
    """`Source/` 의 테스트용 전역 변수 이름(테스트 폴더 밖 — 테스트 폴더는 배포 구성이 짓지 않는다)."""
    setName: set[str] = set()
    for path in (project_root / "Source").rglob("*.cpp"):
        if "Tests" in path.parts:
            continue
        setName.update(TEST_VARIABLE_PATTERN.findall(path.read_text(encoding="utf-8", errors="replace")))
    return sorted(setName)


def check_no_test_variables(project_root: Path) -> SmokeResult:
    """배포 실행 파일(App · Server)에 테스트용 전역 변수 이름이 남지 않았는지 본다."""
    list_name = collect_test_variable_names(project_root)
    list_left: list[str] = []
    # 이름 모두를 한 정규식(긴 이름 먼저)으로 실행 파일을 한 번만 훑는다 — 이름마다 실행 파일 전체를 훑으면 이름 수 × 실행 파일 크기다.
    # 하나라도 남았는지(통과 · 실패)는 이름마다 찾던 것과 같다. 다른 이름을 품은 긴 이름이 남으면 긴 이름만 적힌다.
    name_pattern = re.compile(b"|".join(re.escape(name.encode("ascii")) for name in sorted(list_name, key=len, reverse=True))) if list_name else None
    for preset, stem in (("Ninja-Shipping", "App"), ("Ninja-Shipping-Client", "App"), ("Ninja-Shipping-Server", "Server")):
        executable = find_bin_dir(project_root, preset) / f"{stem}{EXECUTABLE_SUFFIX}"
        if executable.is_file() is False:
            return SmokeResult("NoTestVariables", executable, False, f"{executable} is missing")
        if name_pattern is not None:
            list_left += [f"{stem}:{name}" for name in sorted({match.decode("ascii") for match in name_pattern.findall(executable.read_bytes())})]
    reason = f"{len(list_name)} test variable names checked" if not list_left else f"left in the shipping binaries: {', '.join(list_left[:20])}"
    return SmokeResult("NoTestVariables", find_bin_dir(project_root, "Ninja-Shipping"), not list_left, reason)


def report_log_lines(result: SmokeResult) -> int:
    """로그의 [Error] · [Warning] 줄을 같은 줄끼리 세어 찍고, 기대하지 않은 [Error] 줄 수를 돌려준다."""
    if result.log_path.is_file() is False:
        print(f"  (no log at {result.log_path})")
        return 0
    counts: dict[str, int] = {}
    for line in result.log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if LOG_LINE_PATTERN.search(line):
            body = line.split("] ", 1)[1] if line.startswith("[") and "] " in line else line  # 앞의 시각을 뗀다
            counts[body] = counts.get(body, 0) + 1
    unexpected_error_count = 0
    for body, count in sorted(counts.items(), key=lambda item: -item[1]):
        is_expected = any(expected in body for expected in result.list_expected_error)
        if "[Error]" in body and is_expected is False:
            unexpected_error_count += count
        print(f"  {count:4} {body}{'  (expected)' if is_expected else ''}")
    return unexpected_error_count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="배포 프리셋을 빌드하고 산출물의 기동 · 종료를 본다")
    parser.add_argument("--skip-build", action="store_true", help="빌드를 건너뛰고 지어 둔 산출물만 띄운다")
    args = parser.parse_args(argv)
    project_root = get_project_root()

    list_run = ((run_app, "Ninja-Shipping"), (run_client_app, "Ninja-Shipping-Client"), (run_server, "Ninja-Shipping-Server"), (run_no_automation_scenario, "Ninja-Shipping"))
    if args.skip_build is False:
        list_failed_build = [preset for preset in dict.fromkeys(preset for _, preset in list_run) if build_preset(project_root, preset) is False]
        if list_failed_build:
            print(f"[shipping-check] FAILED build: {', '.join(list_failed_build)}", flush=True)
            for preset in list_failed_build:
                report_cook_log(project_root, preset)
            return ExitCode.VIOLATION

    all_passed = True
    variable_check = check_no_test_variables(project_root)
    print(f"[shipping-check] {variable_check.name}: {'PASS' if variable_check.passed else 'FAIL'} - {variable_check.reason}", flush=True)
    all_passed = variable_check.passed
    list_failed_preset: list[str] = []
    for run, preset in list_run:
        result = run(project_root)
        print(f"[shipping-check] {result.name}: {'PASS' if result.passed else 'FAIL'} - {result.reason} ({result.log_path})", flush=True)
        unexpected_error_count = report_log_lines(result)
        if result.passed is False or unexpected_error_count > 0:
            all_passed = False
            list_failed_preset.append(preset)
    for preset in dict.fromkeys(list_failed_preset):
        report_cook_log(project_root, preset)  # 실행이 진 까닭이 팩(빠진 셰이더 · 데이터)이면 굽기 로그에 그 줄이 있다
    return ExitCode.PASSED if all_passed else ExitCode.VIOLATION


if __name__ == "__main__":
    sys.exit(main())
