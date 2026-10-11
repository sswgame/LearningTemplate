#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file CiFailureReport.py
@brief CI 실패를 GitHub 주석(::error)으로 올립니다 — 작업 로그 · 아티팩트는 저장소 관리자만 받을 수 있어(공개 저장소도 API 가 403) 밖에서 보이는 곳은 주석뿐입니다.

    python Scripts/dev/CiFailureReport.py tests --ctest-dir build/CI-Debug            # 진 테스트: 실패 줄 · 마지막 [ RUN ] · 끝 25 줄 · 크래시 스택
    python Scripts/dev/CiFailureReport.py configure --log build/configure.log         # 구성 실패: vcpkg 가 가리킨 로그들의 끝
    python Scripts/dev/CiFailureReport.py build --log build/build.log                 # 빌드 실패: 오류 줄(컴파일 · 링크)과 앞뒤 몇 줄, 없으면 끝 60 줄

크래시 스택은 테스트 실행 파일의 크래시 핸들러가 로그 폴더(`Bin/Saved/Logs/crash_<세션>.stack.txt`)에 남긴 것입니다.
주석은 단계마다 error 10 개 · 잡마다 50 개까지 보인다(GitHub 한도) — 테스트는 20 개, 로그 파일은 8 개까지만 올린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)

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


def reportTests(ctestDir: Path) -> int:
    tempDir = ctestDir / "Testing" / "Temporary"
    listFailed = [line.split(":", 1)[1].strip() for line in readTextInternal(tempDir / "LastTestsFailed.log").splitlines() if ":" in line]
    log = readTextInternal(tempDir / "LastTest.log")
    if not listFailed:
        print("::warning title=ctest::LastTestsFailed.log is empty or missing - no test result to report")
    for name in listFailed[:_kMaxTestCount]:
        match = re.search(r"Test: " + re.escape(name) + r"\s*$(.*?)^\"" + re.escape(name) + r"\" end time", log, re.S | re.M)
        listLine = [_kAnsi.sub("", line) for line in match.group(1).splitlines()] if match else []
        listMarked = [line[:300] for line in listLine if _kTestMarker.search(line)][:40]
        listRun = [line for line in listLine if line.startswith("[ RUN")]
        listTail = [line[:300] for line in listLine[-_kTailLineCount:]]
        emitErrorInternal("ctest " + name, kNewline.join(listMarked + ["--- last RUN: " + (listRun[-1] if listRun else "(none)"), "--- tail:"] + listTail))
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


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="CI 실패를 GitHub 주석으로 올린다")
    listSub = parser.add_subparsers(dest="kind", required=True)
    tests = listSub.add_parser("tests", help="진 시험 · 크래시 스택")
    tests.add_argument("--ctest-dir", type=Path, required=True)
    configure = listSub.add_parser("configure", help="구성(vcpkg 설치) 실패")
    configure.add_argument("--log", type=Path, required=True)
    build = listSub.add_parser("build", help="빌드 실패")
    build.add_argument("--log", type=Path, required=True)
    args = parser.parse_args(listArgument)
    if args.kind == "tests":
        return reportTests(args.ctest_dir)
    return reportBuild(args.log) if args.kind == "build" else reportConfigure(args.log)


if __name__ == "__main__":
    sys.exit(main())
