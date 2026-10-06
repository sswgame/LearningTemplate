#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
린트 전체(게이트 + 셀프테스트)를 빌드 폴더 없이 한 번에 돌리고 **걸린 시간**을 남깁니다 — CI 린트 잡과 손으로 돌릴 때.

CTest 의 `lint` 라벨은 configure 한 빌드 폴더가 있어야 돈다. CI 린트 잡은 빌드 폴더 없이 빨리 끝나야 하므로 이 스크립트가 CTest 와
**같은 목록 · 같은 인자**(`LintCatalog.discoverLintTargets`)를 린트마다 하위 프로세스 하나로 돌린다 — CTest 항목 하나와 같은 모양이다.
빌드 폴더가 있어야 하는 린트(인자에 `${CMAKE_BINARY_DIR}`)는 `--build-dir` 이 없으면 건너뛴다.

**시간 상한.** 린트마다 CTest TIMEOUT(`timeoutSeconds`)이 이미 있다. 그 **절반**을 넘기면 경고한다 — 느린 기계에서 곧 시간 초과로 질
린트다. 상한을 따로 적은 표는 두지 않는다(표가 둘이면 어긋난다).
`--hook-sample 1,10` 은 커밋 훅을 **임시 인덱스**(`GIT_INDEX_FILE`)로 흉내 내 staged 파일 수별 시간을 잰다 — 진짜 인덱스 · 작업 트리는
건드리지 않는다.

  py -3 Scripts/lint/RunLintSuite.py [--root <repo>] [--build-dir <dir>] [--json <out>] [--hook-sample 1,10]
  py -3 -m Scripts lint-suite ...
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))          # Scripts/lint — LintCatalog
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))      # Scripts — common

from common import getCpuCount, getProjectRoot, mapConcurrent, runGit, runProcess  # noqa: E402
from LintCatalog import LintTarget, discoverLintTargets  # noqa: E402

#: 경고 문턱 — CTest TIMEOUT 의 이 비율을 넘기면 경고한다.
kWarnTimeoutRatio = 0.5
#: 훅 시간 경고 문턱(초) — staged 파일 수 → 상한. 8 코어 개발 PC 에서 잰 값의 두 배쯤이다. CI 첫 기록을 보고 고친다.
kHookBudgetSeconds: dict[int, float] = {1: 8.0, 10: 12.0, 100: 20.0}
#: 훅 표본 — 이 폴더의 C++ 파일을 이름순으로 앞에서부터 고른다(결정적이다. 셰이더는 고르지 않는다 — 훅이 쿠킹을 부른다).
_kHookSampleRoot = "Source/Engine"
#: git 의 빈 blob — staged 파일의 인덱스 내용으로 쓴다(HEAD 와 다르기만 하면 된다. 게이트는 작업 트리를 읽는다).
_kEmptyBlobArgument = ["hash-object", "-w", "--stdin"]


@dataclass
class LintTiming:
    """린트 하나를 돌린 결과."""

    name: str
    seconds: float
    returnCode: int
    timeoutSeconds: int
    outputTail: str


def resolveArgumentsInternal(target: LintTarget, repositoryRoot: Path, buildDir: Path | None) -> list[str] | None:
    """CMake 변수 참조를 풀어 인자를 만든다. 빌드 폴더가 필요한데 없으면 None(건너뛴다)."""
    listArgument = ["--root", str(repositoryRoot)]
    for argument in target.listCtestArgument:
        if "${CMAKE_BINARY_DIR}" in argument:
            if buildDir is None:
                return None
            argument = argument.replace("${CMAKE_BINARY_DIR}", str(buildDir))
        listArgument.append(argument.replace("${CMAKE_SOURCE_DIR}", str(repositoryRoot)))
    return listArgument


def runLintInternal(target: LintTarget, repositoryRoot: Path, listArgument: list[str]) -> LintTiming:
    start = time.perf_counter()
    # 자식의 stdout 이 파이프면 콘솔 코드 페이지로 쓰는 스크립트가 있다 — UTF-8 로 맞춰야 실패 꼬리 · JSON 의 한글이 깨지지 않는다.
    environment = dict(os.environ, PYTHONIOENCODING="utf-8")
    result = runProcess([sys.executable, repositoryRoot / target.scriptRelPath, *listArgument], cwd=repositoryRoot, env=environment,
                        bMergeStderr=True)
    seconds = time.perf_counter() - start
    return LintTiming(target.name, seconds, result.returnCode, target.timeoutSeconds, "\n".join(result.stdout.strip().splitlines()[-15:]))


def measureHookInternal(repositoryRoot: Path, fileCount: int) -> float | None:
    """staged 파일 `fileCount` 개짜리 커밋의 훅 시간. 임시 인덱스를 쓰고 지운다. 표본이 모자라면 None."""
    listCandidate = sorted(path for path in (repositoryRoot / _kHookSampleRoot).rglob("*") if path.suffix in (".cpp", ".h"))[:fileCount]
    if len(listCandidate) < fileCount:
        return None
    indexPath = Path(runGit(["rev-parse", "--git-path", "index"], cwd=repositoryRoot).stdout.strip())
    if not indexPath.is_absolute():
        indexPath = repositoryRoot / indexPath
    emptyBlob = runProcess(["git", *_kEmptyBlobArgument], cwd=repositoryRoot, stdinText="").stdout.strip()
    tempDir = Path(tempfile.mkdtemp(prefix="swHookSample"))
    try:
        tempIndex = tempDir / "index"
        shutil.copyfile(indexPath, tempIndex)
        environment = dict(os.environ, GIT_INDEX_FILE=str(tempIndex))
        for path in listCandidate:
            relPath = path.relative_to(repositoryRoot).as_posix()
            if not runProcess(["git", "update-index", "--cacheinfo", f"100644,{emptyBlob},{relPath}"], cwd=repositoryRoot,
                              env=environment).bSucceeded:
                return None
        start = time.perf_counter()
        runProcess([sys.executable, "-m", "Scripts", "lint"], cwd=repositoryRoot, env=environment)
        return time.perf_counter() - start
    finally:
        shutil.rmtree(tempDir, ignore_errors=True)


def emitWarningInternal(message: str) -> None:
    # GitHub Actions 는 이 줄을 잡의 경고 주석으로 올린다. 다른 곳에서는 그냥 한 줄이다.
    prefix = "::warning title=lint time::" if os.environ.get("GITHUB_ACTIONS") else "[경고] "
    print(f"{prefix}{message}")


def writeStepSummaryInternal(listTiming: list[LintTiming], totalSeconds: float, mapHookSeconds: dict[int, float | None]) -> None:
    summaryPath = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summaryPath:
        return
    lines = [f"### 린트 시간 (총 {totalSeconds:.1f} s, 동시 실행)", "", "| 린트 | 초 | TIMEOUT | 결과 |", "|---|---:|---:|---|"]
    lines += [f"| {timing.name} | {timing.seconds:.2f} | {timing.timeoutSeconds} | {'OK' if timing.returnCode == 0 else 'FAIL'} |"
              for timing in listTiming]
    for fileCount, seconds in mapHookSeconds.items():
        lines.append(f"\n커밋 훅(staged {fileCount}개): {'-' if seconds is None else f'{seconds:.2f} s'}")
    with open(summaryPath, "a", encoding="utf-8") as summaryFile:
        summaryFile.write("\n".join(lines) + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="린트 전체(게이트 + 셀프테스트)를 동시에 돌리고 린트마다 걸린 시간을 남긴다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트")
    parser.add_argument("--build-dir", type=Path, default=None, help="빌드 폴더가 필요한 린트(CheckSourceGlob)까지 돌릴 때 (상대 경로는 저장소 루트 기준)")
    parser.add_argument("--json", type=Path, default=None, help="시간 기록을 JSON 으로")
    parser.add_argument("--hook-sample", default="", help="커밋 훅을 staged 파일 수별로 잰다 (예: 1,10)")
    args = parser.parse_args(argv)
    repositoryRoot = (args.root or getProjectRoot()).resolve()
    buildDir = None if args.build_dir is None else (args.build_dir if args.build_dir.is_absolute() else repositoryRoot / args.build_dir)

    listJob: list[tuple[LintTarget, list[str]]] = []
    for target in discoverLintTargets():
        listArgument = resolveArgumentsInternal(target, repositoryRoot, buildDir)
        if listArgument is None:
            print(f"[RunLintSuite] {target.name} 건너뜀 (빌드 폴더가 필요하다 — --build-dir)")
            continue
        listJob.append((target, listArgument))

    # 린트는 대개 한 코어를 쓰고 `CheckCodeConventions` · `CheckLintsAreAlive` 는 스스로 여럿을 쓴다 — 코어 수의 절반만 함께 띄운다.
    start = time.perf_counter()
    listTiming = list(mapConcurrent(lambda job: runLintInternal(job[0], repositoryRoot, job[1]), listJob,
                                    workerCount=max(1, getCpuCount() // 2)))
    totalSeconds = time.perf_counter() - start
    listTiming.sort(key=lambda timing: -timing.seconds)

    listFailed = [timing for timing in listTiming if timing.returnCode != 0]
    for timing in listTiming:
        print(f"  {timing.name:34} {timing.seconds:6.2f} s  (TIMEOUT {timing.timeoutSeconds:3})  {'OK' if timing.returnCode == 0 else 'FAIL'}")
        if timing.seconds > timing.timeoutSeconds * kWarnTimeoutRatio:
            emitWarningInternal(f"{timing.name} 가 {timing.seconds:.1f} s — CTest TIMEOUT {timing.timeoutSeconds} s 의 절반을 넘었습니다")
    print(f"[RunLintSuite] {len(listTiming)}개, 총 {totalSeconds:.1f} s (동시 실행)")

    mapHookSeconds: dict[int, float | None] = {}
    for item in filter(None, args.hook_sample.split(",")):
        fileCount = int(item)
        seconds = measureHookInternal(repositoryRoot, fileCount)
        mapHookSeconds[fileCount] = seconds
        print(f"[RunLintSuite] 커밋 훅 staged {fileCount}개: {'표본 부족' if seconds is None else f'{seconds:.2f} s'}")
        budget = kHookBudgetSeconds.get(fileCount)
        if seconds is not None and budget is not None and seconds > budget:
            emitWarningInternal(f"커밋 훅(staged {fileCount}개)이 {seconds:.1f} s — 상한 {budget:.0f} s 를 넘었습니다")

    if args.json is not None:
        args.json.write_text(json.dumps({"totalSeconds": totalSeconds, "lints": [asdict(timing) for timing in listTiming],
                                         "hook": {str(count): seconds for count, seconds in mapHookSeconds.items()}},
                                        ensure_ascii=False, indent=1), encoding="utf-8")
    writeStepSummaryInternal(listTiming, totalSeconds, mapHookSeconds)

    for timing in listFailed:
        print(f"\n[RunLintSuite] {timing.name} 실패 (종료 코드 {timing.returnCode}):\n{timing.outputTail}", file=sys.stderr)
    return 1 if listFailed else 0


if __name__ == "__main__":
    sys.exit(main())
