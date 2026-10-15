#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file CiFailureReport.py
@brief CI 실패를 GitHub 주석(::error)으로 올립니다 — 작업 로그 · 아티팩트는 저장소 관리자만 받을 수 있어(공개 저장소도 API 가 403) 밖에서 보이는 곳은 주석뿐입니다.

    python Scripts/dev/CiFailureReport.py tests --ctest-dir build/CI-Debug            # 진 테스트: 결과(Timeout · 경과 시간 · 종료 코드) · 실패 줄 · 마지막 [ RUN ] · 끝 25 줄 · 크래시 스택
    python Scripts/dev/CiFailureReport.py configure --log build/configure.log         # 구성 실패: vcpkg 가 가리킨 로그들의 끝
    python Scripts/dev/CiFailureReport.py build --log build/build.log                 # 빌드 실패: 오류 줄(컴파일 · 링크)과 앞뒤 몇 줄, 없으면 끝 60 줄
    python Scripts/dev/CiFailureReport.py fuzz --log fuzz/Material/log.txt --target Material   # 퍼징 실패: 새니타이저 보고 · 스택 · 재현 입력(Base64)
    python Scripts/dev/CiFailureReport.py load --results build/CI-Release/LoadResults/nightly.json  # 부하 실패: 조합마다 병목 갈래 · 예산 · 회귀 · 지표 · 재현 명령

크래시 스택은 테스트 실행 파일의 크래시 핸들러가 로그 폴더(`Bin/Saved/Logs/crash_<세션>.stack.txt`)에 남긴 것입니다.
진 테스트마다 제목에 결과를 붙입니다. `Timeout` 은 ctest 가 한도(`TIMEOUT` 속성, `ctest --show-only=json-v1` 로 읽음)에서 죽인 것이고,
`exit N` 은 TestHost 가 스스로 끝내며 남긴 종료 코드, `no exit line` 은 한도 전에 끝났지만 종료 줄이 없는 것(크래시)입니다.
주석은 단계마다 error 10 개 · 잡마다 50 개까지 보인다(GitHub 한도) — 테스트는 20 개, 로그 파일은 8 개까지만 올린다.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

kNewline = "\n"
_kAnsi = re.compile(r"\x1b\[[0-9;]*m")
# 테스트 틀이 stdout 에 찍는 실패 줄만 고른다. 로거가 같은 내용을 "[날짜] …" 로 한 번 더 찍으므로 그 줄은 뺀다.
# 새니타이저 보고(케이스는 모두 통과해도 종료 코드로 조각을 떨어뜨린다)도 고른다. 틀 테스트가 일부러 지게 만든 안쪽 케이스 줄은
# "(nested) " 로 시작하므로 여기에 걸리지 않는다.
_kTestMarker = re.compile(r"^\[  FAILED  \]|^  \[FAILED\]|^    Condition:|^    Message  :|verified nothing|^ Tests failed|^   - |no cases in this executable|"
                          r"selected no test|^Invalid|^Unknown --|^WARNING: \w+Sanitizer|^SUMMARY: \w+Sanitizer|^==\d+==ERROR: \w+Sanitizer|"
                          r"runtime error:")
_kVcpkgLogLine = re.compile(r"^\s*(/\S+\.log|[A-Za-z]:[\\/]\S+\.log)\s*$")
_kMaxTestCount = 20
_kMaxLogCount = 8
_kMaxStackCount = 3
_kMaxStackChars = 4000
_kTailLineCount = 25
_kLogTailLineCount = 60
# 빌드 로그에서 오류를 고르는 줄: 컴파일러 `error:`, 링커 `undefined`/`duplicate symbol`, ninja `FAILED:`/`build stopped`.
_kBuildErrorMarker = re.compile(r"\berror\b[:\s]|\bundefined (reference|symbol)\b|duplicate symbol|^FAILED:|ninja: build stopped|\bfatal error\b")
_kMaxBuildErrorCount = 10  # 주석은 단계마다 error 10 개까지 보인다
_kBuildContextLines = 2
# libFuzzer 로그에서 고르는 줄: 새니타이저 · libFuzzer 보고(`==N==ERROR`, `SUMMARY`, `ALARM`(타임아웃), `out-of-memory`), 스택 프레임, 기록한 입력 경로.
_kFuzzMarker = re.compile(r"^==\d+==|^SUMMARY:|^ALARM:|out-of-memory|deadly signal|^\s+#\d+ 0x|Test unit written to|runtime error:")
_kMaxFuzzLineCount = 60
# ctest 가 LastTest.log 에 남기는 경과 시간과 TestHost 가 끝낼 때 찍는 종료 코드.
_kTestTime = re.compile(r"^Test time =\s*([0-9.]+) sec", re.M)
_kTestHostExit = re.compile(r"\[TestHost\] shut down - exit code (-?\d+)")
# 한도에서 죽인 테스트의 경과 시간은 한도를 조금 넘는다. 시계 오차를 감안해 한도의 98 % 부터 Timeout 으로 본다.
_kTimeoutRatio = 0.98


def escapeAnnotationInternal(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "").replace(kNewline, "%0A")


def emitErrorInternal(title: str, body: str) -> None:
    print(f"::error title={title}::{escapeAnnotationInternal(body)}")
    print(f"===== {title} ====={kNewline}{body}{kNewline}")


def readTextInternal(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def collectCrashStacksInternal(ctestDir: Path) -> list[str]:
    """테스트 실행 파일의 크래시 핸들러가 남긴 스택(`Bin/Saved/**/crash_*.stack.txt`)을 새것부터 셋까지."""
    savedDir = ctestDir / "Bin" / "Saved"
    if not savedDir.is_dir():
        return []
    listStack = sorted(savedDir.rglob("crash_*.stack.txt"), key=lambda path: path.stat().st_mtime, reverse=True)
    return [f"{path.name}:{kNewline}{readTextInternal(path)[:_kMaxStackChars]}" for path in listStack[:_kMaxStackCount]]


def readTestTimeouts(ctestDir: Path) -> dict[str, float]:
    """테스트 이름 → `TIMEOUT` 속성(초)입니다. ctest 를 못 부르거나 속성이 없는 테스트는 빠집니다."""
    result = common.runProcess(["ctest", "--test-dir", ctestDir, "--show-only=json-v1"], timeoutSeconds=60)
    if not result.bSucceeded:
        return {}
    try:
        payload = json.loads(result.stdout)
    except ValueError:
        return {}
    mapTimeout: dict[str, float] = {}
    for test in payload.get("tests", []):
        for prop in test.get("properties", []):
            if prop.get("name") == "TIMEOUT" and isinstance(prop.get("value"), (int, float)):
                mapTimeout[test.get("name", "")] = float(prop["value"])
    return mapTimeout


def describeTestResult(section: str, limitSeconds: float | None) -> str:
    """LastTest.log 의 한 테스트 구간에서 결과 한 줄 — `Timeout 180.0 s / 180 s` · `exit 1 after 12.3 s` · `no exit line after 4.0 s`."""
    timeMatch = _kTestTime.search(section)
    elapsed = float(timeMatch.group(1)) if timeMatch else None
    limit = f" / {limitSeconds:g} s" if limitSeconds is not None else ""
    if elapsed is None:
        return "no elapsed time"
    if limitSeconds is not None and elapsed >= limitSeconds * _kTimeoutRatio:
        return f"Timeout {elapsed:.1f} s{limit}"
    listExit = _kTestHostExit.findall(section)
    if listExit:
        return f"exit {listExit[-1]} after {elapsed:.1f} s{limit}"
    return f"no exit line after {elapsed:.1f} s{limit}"


def reportTests(ctestDir: Path) -> int:
    tempDir = ctestDir / "Testing" / "Temporary"
    listFailed = [line.split(":", 1)[1].strip() for line in readTextInternal(tempDir / "LastTestsFailed.log").splitlines() if ":" in line]
    log = readTextInternal(tempDir / "LastTest.log")
    if not listFailed:
        print("::warning title=ctest::LastTestsFailed.log is empty or missing - no test result to report")
    mapTimeout = readTestTimeouts(ctestDir) if listFailed else {}
    for name in listFailed[:_kMaxTestCount]:
        match = re.search(r"Test: " + re.escape(name) + r"\s*$(.*?)^\"" + re.escape(name) + r"\" end time", log, re.S | re.M)
        section = _kAnsi.sub("", match.group(1)) if match else ""
        listLine = section.splitlines()
        # 꼬리는 테스트 출력까지만 — ctest 가 덧붙이는 경과 시간 · 구분선은 결과 줄에 들어간다.
        listOutput = listLine[:next((index for index, line in enumerate(listLine) if line.startswith("<end of output>")), len(listLine))]
        listMarked = [line[:300] for line in listOutput if _kTestMarker.search(line)][:40]
        listRun = [line for line in listOutput if line.startswith("[ RUN")]
        listTail = [line[:300] for line in listOutput[-_kTailLineCount:]]
        result = describeTestResult(section, mapTimeout.get(name))
        emitErrorInternal(f"ctest {name} [{result}]",
                          kNewline.join(["--- result: " + result] + listMarked + ["--- last RUN: " + (listRun[-1] if listRun else "(none)"), "--- tail:"] + listTail))
    for stack in collectCrashStacksInternal(ctestDir):
        emitErrorInternal("crash stack", stack)
    return 0


def reportConfigure(logPath: Path) -> int:
    text = readTextInternal(logPath)
    listLogPath: list[Path] = []
    for line in text.splitlines():
        match = _kVcpkgLogLine.match(line)
        if match is None:
            continue
        path = Path(match.group(1))
        if path.is_file() and path not in listLogPath:
            listLogPath.append(path)
    if not listLogPath:
        # vcpkg 가 로그를 가리키지 않았다(cmake 자체 오류) — 구성 출력의 끝을 올린다.
        emitErrorInternal("configure", kNewline.join(text.splitlines()[-_kLogTailLineCount:]))
        return 0
    for path in listLogPath[:_kMaxLogCount]:
        emitErrorInternal("vcpkg " + path.name, kNewline.join(readTextInternal(path).splitlines()[-_kLogTailLineCount:]))
    return 0


def reportBuild(logPath: Path) -> int:
    lines = [_kAnsi.sub("", line) for line in readTextInternal(logPath).splitlines()]
    listHit = [index for index, line in enumerate(lines) if _kBuildErrorMarker.search(line) is not None]
    if not listHit:
        emitErrorInternal("build", kNewline.join(lines[-_kLogTailLineCount:]))
        return 0
    # `-k 0` 로 지으면 같은 파일의 오류가 쏟아진다 — 파일마다 첫 오류 하나만 올려 열 덩어리 안에 여러 파일이 들게 한다.
    listBlock: list[str] = []
    setSeenFile: set[str] = set()
    lastEnd = -1
    for index in listHit:
        if len(listBlock) >= _kMaxBuildErrorCount:
            break
        fileKey = re.split(r"[:(]\d", lines[index], maxsplit=1)[0]
        if fileKey in setSeenFile and _kBuildErrorMarker.search(lines[index]) is not None and "error" in lines[index]:
            continue
        setSeenFile.add(fileKey)
        begin = max(index - _kBuildContextLines, lastEnd + 1)
        end = min(index + _kBuildContextLines + 1, len(lines))
        if begin >= end:
            continue
        listBlock.append(kNewline.join(lines[begin:end]))
        lastEnd = end - 1
    for position, block in enumerate(listBlock):
        emitErrorInternal(f"build {position + 1}/{len(listBlock)}", block)
    return 0


def reportFuzz(logPath: Path, target: str) -> int:
    """libFuzzer 한 대상의 실패: 새니타이저 · libFuzzer 보고 줄, 스택 앞부분, 재현 입력(`Base64:`)을 올린다 — 아티팩트 없이 재현할 수 있게."""
    lines = [_kAnsi.sub("", line) for line in readTextInternal(logPath).splitlines()]
    listPicked = [line[:300] for line in lines if _kFuzzMarker.search(line)][:_kMaxFuzzLineCount]
    listBase64 = [line[:_kMaxStackChars] for line in lines if line.startswith("Base64:")]
    body = listPicked + listBase64[-1:] if listPicked else lines[-_kLogTailLineCount:]
    emitErrorInternal(f"fuzz {target}", kNewline.join(body))
    return 0


def reportLoad(resultsPath: Path) -> int:
    """부하 검증(`Scripts/qa/Load.py`)의 결과 JSON 에서 진 조합마다 주석 하나 — 병목 갈래가 붙은 예산 실패 · 기준선 회귀 · 실행 실패, 주요 지표, 재현 명령."""
    try:
        listResult = json.loads(readTextInternal(resultsPath) or "{}").get("results", [])
    except ValueError:
        listResult = []
    if not listResult:
        emitErrorInternal("load", f"no load results in {resultsPath} - the load run did not finish")
        return 0
    for result in listResult[:_kMaxTestCount]:
        if result.get("verdict") != "fail":
            continue
        listMetric = [f"{name} {values['median']:.1f} ({values['min']:.1f}..{values['max']:.1f})" for name, values in result.get("metrics", {}).items()
                      if name.endswith("p99_us") or name in ("error_percent", "converged", "private_mb.max", "hitch.worst_us")][:20]
        body = (["--- failures:"] + result.get("failures", []) + result.get("budget_failures", []) + result.get("baseline_regressions", []) +
                ["--- metrics (median, min..max over runs):"] + listMetric + ["--- command:", " ".join(result.get("command", []))])
        emitErrorInternal(f"load {result.get('scenario')} x {result.get('profile')}{' (emulated)' if result.get('emulated') else ''}", kNewline.join(body))
    return 0


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CI 실패를 GitHub 주석으로 올린다")
    listSub = parser.add_subparsers(dest="kind", required=True)
    tests = listSub.add_parser("tests", help="진 테스트 · 크래시 스택")
    tests.add_argument("--ctest-dir", type=Path, required=True)
    configure = listSub.add_parser("configure", help="구성(vcpkg 설치) 실패")
    configure.add_argument("--log", type=Path, required=True)
    build = listSub.add_parser("build", help="빌드 실패")
    build.add_argument("--log", type=Path, required=True)
    fuzz = listSub.add_parser("fuzz", help="libFuzzer 대상 하나의 크래시 · 타임아웃")
    fuzz.add_argument("--log", type=Path, required=True)
    fuzz.add_argument("--target", required=True)
    load = listSub.add_parser("load", help="부하 검증(py -3 -m Scripts load)의 진 조합")
    load.add_argument("--results", type=Path, required=True)
    args = parser.parse_args(listArgument)
    if args.kind == "tests":
        return reportTests(args.ctest_dir)
    if args.kind == "fuzz":
        return reportFuzz(args.log, args.target)
    if args.kind == "load":
        return reportLoad(args.results)
    return reportBuild(args.log) if args.kind == "build" else reportConfigure(args.log)


if __name__ == "__main__":
    sys.exit(main())
