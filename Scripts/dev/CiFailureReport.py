#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file CiFailureReport.py
@brief CI 실패를 GitHub 주석(::error)으로 올립니다 — 작업 로그 · 아티팩트는 저장소 관리자만 받을 수 있어(공개 저장소도 API 가 403) 밖에서 보이는 곳은 주석뿐입니다.

    python Scripts/dev/CiFailureReport.py tests --ctest-dir build/CI-Debug            # 진 시험: 실패 줄 · 마지막 [ RUN ] · 끝 25 줄 · 크래시 스택
    python Scripts/dev/CiFailureReport.py configure --log build/configure.log         # 구성 실패: vcpkg 가 가리킨 로그들의 끝

크래시 스택은 시험 실행 파일의 크래시 핸들러가 로그 폴더(`Bin/Saved/Logs/crash_<세션>.stack.txt`)에 남긴 것입니다.
주석은 단계마다 error 10 개 · 잡마다 50 개까지 보인다(GitHub 한도) — 시험은 20 개, 로그 파일은 8 개까지만 올린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import useUtf8Stdout  # noqa: E402

kNewline = "\n"
_kAnsi = re.compile(r"\x1b\[[0-9;]*m")
# 시험 틀이 stdout 에 찍는 실패 줄만 고른다. 로거가 같은 내용을 "[날짜] …" 로 한 번 더 찍으므로 그 줄은 뺀다.
_kTestMarker = re.compile(r"^\[  FAILED  \]|^  \[FAILED\]|^    Condition:|^    Message  :|verified nothing|^ Tests failed|^   - |no cases in this executable|"
                          r"selected no test|^Invalid|^Unknown --")
_kVcpkgLogLine = re.compile(r"^\s*(/\S+\.log|[A-Za-z]:[\\/]\S+\.log)\s*$")
_kMaxTestCount = 20
_kMaxLogCount = 8
_kMaxStackCount = 3
_kMaxStackChars = 4000
_kTailLineCount = 25
_kLogTailLineCount = 60


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
    """시험 실행 파일의 크래시 핸들러가 남긴 스택(`Bin/Saved/**/crash_*.stack.txt`)을 새것부터 셋까지."""
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


def main(listArgument: list[str] | None = None) -> int:
    useUtf8Stdout()
    parser = argparse.ArgumentParser(description="CI 실패를 GitHub 주석으로 올린다")
    listSub = parser.add_subparsers(dest="kind", required=True)
    tests = listSub.add_parser("tests", help="진 시험 · 크래시 스택")
    tests.add_argument("--ctest-dir", type=Path, required=True)
    configure = listSub.add_parser("configure", help="구성(vcpkg 설치) 실패")
    configure.add_argument("--log", type=Path, required=True)
    args = parser.parse_args(listArgument)
    return reportTests(args.ctest_dir) if args.kind == "tests" else reportConfigure(args.log)


if __name__ == "__main__":
    sys.exit(main())
