"""
@file LogDigest.py
@brief 긴 빌드 · 테스트 출력을 요약해 보인다 — 전체는 파일에 남긴다.

에이전트와 세션은 도구 출력을 매번 입력으로 다시 읽는다. 통과한 빌드의 수천 줄은 정보가 없다.
그래서 래퍼(`RunFullBuild` · `RunTests`)는 명령을 **로그 파일로** 돌리고(`runDigested`), 끝나면

- 성공: 마지막 몇 줄(`kTailLineCount`)과 소요 시간
- 실패: 실패 원인 줄(`error` · `FAILED` · 실패한 테스트 이름)과 그 앞뒤, 그리고 마지막 몇 줄

만 보인다. 두 경우 모두 로그 경로를 한 줄로 알린다. 전부 보려면 `--verbose` — 지금까지처럼 콘솔로 흘린다.
요약 만들기(`digestLog`)는 순수 함수라 가짜 출력으로 테스트한다.

로그는 공통 .git 아래 `sw-build-logs/<워크트리 이름>-<이름>.log`(UTF-8, 추적하지 않는다)다. 같은 이름은 덮어쓴다.
"""

from __future__ import annotations

import re
import sys
import time
from pathlib import Path

from .Host import runGit
from .Process import ProcessResult, runProcess

#: 공통 .git 아래 로그 폴더.
kLogDirName = "sw-build-logs"
#: 성공일 때 보이는 마지막 줄 수 — 실패일 때도 끝에 붙는다.
kTailLineCount = 5
#: 실패 원인 줄 앞뒤로 같이 보이는 줄 수(컴파일러가 원인 아래에 소스 줄 · 캐럿을 붙인다).
kContextBefore = 2
kContextAfter = 4
#: 실패 요약의 최대 줄 수(원인 줄이 수천 개여도 이 안에서 멈추고 나머지 수를 알린다).
kMaxFailureLines = 120
#: 실패 원인 줄: 컴파일 · 링크 오류, ninja `FAILED:`, 테스트 `FAILED` · `***Failed`, 단언.
kFailureLineRegex = re.compile(r"\berror\b|\bFAILED\b|\bFAIL\b|\*\*\*|\bfatal\b|Assertion|\bCheck failed\b|\bLNK\d+\b|\bTimeout\b", re.IGNORECASE)


def digestLog(listLine: list[str], returnCode: int, seconds: float, logPath: Path | None = None) -> list[str]:
    """
    출력 줄들 → 보일 줄들. 성공(`returnCode == 0`)이면 마지막 `kTailLineCount` 줄, 실패면 원인 줄 + 앞뒤 + 마지막 줄.
    끝에 소요 시간과 로그 경로 줄이 붙는다. 빈 줄은 세지 않는다.
    """
    listKept = [line.rstrip() for line in listLine if line.strip()]
    listTail = listKept[-kTailLineCount:]
    if returnCode == 0:
        listShown = list(listTail)
    else:
        listFailure = _pickFailureLines(listKept)
        listNewTail = [line for line in listTail if line not in set(listFailure)]
        listShown = listFailure + (["    ..."] if listFailure and listNewTail else []) + listNewTail
    listShown.append(f"[종료 {returnCode}] {seconds:.1f} s · 출력 {len(listKept)}줄 중 {len(listShown)}줄만 보임" if returnCode != 0
                     else f"[성공] {seconds:.1f} s · 출력 {len(listKept)}줄 중 {min(len(listKept), kTailLineCount)}줄만 보임")
    if logPath is not None:
        listShown.append(f"[로그] 전체 출력: {logPath}")
    return listShown


def _pickFailureLines(listKept: list[str]) -> list[str]:
    """실패 원인 줄과 그 앞뒤. 겹치는 구간은 합치고, 합친 구간 사이는 `...` 로 끊는다. 최대 `kMaxFailureLines`."""
    listRange: list[list[int]] = []
    for index, line in enumerate(listKept):
        if not kFailureLineRegex.search(line):
            continue
        start, end = max(0, index - kContextBefore), min(len(listKept), index + kContextAfter + 1)
        if listRange and start <= listRange[-1][1]:
            listRange[-1][1] = max(listRange[-1][1], end)
        else:
            listRange.append([start, end])

    listResult: list[str] = []
    for position, (start, end) in enumerate(listRange):
        if position > 0:
            listResult.append("    ...")
        listResult.extend(listKept[start:end])
    if len(listResult) > kMaxFailureLines:
        hiddenCount = len(listResult) - kMaxFailureLines
        listResult = listResult[:kMaxFailureLines] + [f"    ... (원인 구간 {hiddenCount}줄 더 — 로그에서 본다)"]
    return listResult


def getLogPath(repositoryRoot: Path, name: str) -> Path:
    """로그 파일 경로 — 공통 .git 아래라 워크트리를 지워도 남고 추적되지 않는다. 폴더는 `runDigested` 가 만든다."""
    result = runGit(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=repositoryRoot)
    if not result.bSucceeded:
        raise RuntimeError(f"git 저장소가 아닙니다: {repositoryRoot}\n{result.stderr.strip()}")
    safeName = re.sub(r"[^\w.-]+", "_", name)
    return Path(result.stdout.strip()) / kLogDirName / f"{repositoryRoot.name}-{safeName}.log"


def runDigested(command: list[str], repositoryRoot: Path, name: str, *, bVerbose: bool, cwd: Path | None = None,
                bBelowNormalPriority: bool = False) -> ProcessResult:
    """
    `command` 를 돌린다. `bVerbose` 면 출력이 콘솔로 그대로 흐르고, 아니면 로그 파일로 가고 끝나서 요약만 찍는다.
    돌려주는 결과의 `returnCode` · `bLaunched` 는 어느 쪽이든 같다.
    """
    if bVerbose:
        return runProcess(command, cwd=cwd, bCapture=False, bBelowNormalPriority=bBelowNormalPriority)
    logPath = getLogPath(repositoryRoot, name)
    startTime = time.perf_counter()
    result = runProcess(command, cwd=cwd, stdoutPath=logPath, bBelowNormalPriority=bBelowNormalPriority)
    seconds = time.perf_counter() - startTime
    if not result.bLaunched:
        print(f"[LogDigest] {result.stderr}", file=sys.stderr)
        return result
    printDigest(logPath, result.returnCode, seconds)
    return result


def printDigest(logPath: Path, returnCode: int, seconds: float) -> None:
    """로그 파일을 읽어 요약을 찍는다. 실패 요약은 stderr, 성공 요약은 stdout."""
    listLine = logPath.read_text(encoding="utf-8", errors="replace").splitlines()
    for line in digestLog(listLine, returnCode, seconds, logPath):
        print(line, file=sys.stderr if returnCode != 0 else sys.stdout)
    sys.stdout.flush()
