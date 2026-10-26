#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunVerifyBatch.py
@brief 통합에서 돌릴 모든 확인(빌드 · ctest · 테스트 필터 · 쿠킹 · 골든 · 측정)을 매니페스트 한 곳에서 한 명령으로 돌리고 결과 표 하나를 남깁니다.

    py -3 -m Scripts verify-batch --list                          # 항목 표(종류 · 구성 · GPU · 돌 수 있는가 · 일회성)
    py -3 -m Scripts verify-batch --check                         # 매니페스트가 읽히는지, 가리키는 파일 · 프리셋 · 테스트가 있는지, 문서의 대기 줄과 맞는지
    py -3 -m Scripts verify-batch --dry-run                       # 돌 순서와 명령만 찍는다
    py -3 -m Scripts verify-batch --owner integD                  # 전부: Debug → Shipping → Release 순으로 이어서 돌린다
    py -3 -m Scripts verify-batch --owner integD --config Release # 한 구성만
    py -3 -m Scripts verify-batch --owner integD --only perf-frame net-udp-bench

항목의 정본은 `Test/QA/VerifyBatch.json` 이고 형식 · 검증은 `common/VerifyManifest.py` 가 가진다. 이 명령은 실행기다 — 새 측정 코드를 쓰지 않고 `full-build` ·
`ctest` · `test` · `perf` · `load` · `perf-compare` · `soak` · `scene-load-bench` · `golden` 을 부른다. 순서는 구성(매니페스트의 `configs` 순서)마다
종류 → GPU 방식이다. GPU 방식이 같은 이어진 항목은 잠금을 한 번만 잡는다: `shared` 는 GPU 공유 칸, `exclusive` 는 GPU 와 빌드 칸을 혼자(측정이 다른 빌드에
오염되지 않게 — 작업 방식 2 절). 한 항목이 져도 나머지는 계속 돈다. 다만 `build` 항목이 지면 같은 구성의 나머지는 `막힘` 이다(없는 바이너리를 돌리지 않는다).
`manual`(사람이 해야 한다) · `need_command`(명령을 아직 못 만들었다) 항목은 돌지 않고 이유를 표에 적으며, `wsl` 항목은 사용자가 요청할 때만 하므로 돌지 않는다. 덮개(`covered`) 항목은 덮개 항목의 결과를 따른다.

`--jobs N`(기본 2)이면 한 구성 안에서 서로 충돌하지 않는 단위를 N 개까지 겹쳐 돌린다(`hasConflict`). 단위는 잠금 한 번에 이어 도는 같은 GPU 방식 묶음이고, 잠금 없는
`none` 은 항목 하나가 단위다. 빌드 · 쿠킹 · 골든 · 측정 · 독점 항목은 혼자 돌고, 같은 잠금 자원을 쥔 단위(GPU 공유끼리)는 순서대로 돈다. 그래서 겹치는 것은 잠금 없는
`ctest` · `test` 항목끼리와 GPU 공유 항목뿐이다. `--jobs 1` 이면 옛 순서 그대로다. 결과 표 · `result.json` 은 겹침과 무관하게 매니페스트 순서이고, 항목 출력은
`logs/<id>.log` 로 따로 간다. 한 항목이 져도 나머지를 끝까지 돌려 모은다.

결과는 `build/verify-batch/<시각>/` 의 `result.md`(표 하나) · `result.json` · `logs/<id>.log` · `timeline.log` 에 남고 표는 끝에 한 번 더 찍는다.
`timeline.log` 는 `HH:MM:SS 시작|끝|앱시작|앱끝 <항목 id> <내용>` 한 줄씩이다(항목 시작 · 끝, 항목이 `AppRun.runApp` 으로 띄운 App 의 pid · 게임 · 시나리오 · 백엔드 · `-gv_*`). 종료 코드는 실패나 막힘이 있으면 1 이다.
빌드 · 쿠킹 · 골든 다시 뜨기는 작업 트리를 바꾼다(골든은 `Test/QA/Golden` 의 기준 파일) — 돌린 뒤 `git status` 로 본다.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime
import json
import os
import platform
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ConflictSchedule import runConflictSchedule  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Process import runProcess  # noqa: E402
from common.SharedLock import holdLock, kDefaultWaitSeconds  # noqa: E402
from common.Timeline import appendTimelineLine, kTimelineEnvName, kTimelineItemEnvName  # noqa: E402
from common.VerifyManifest import (ItemResult, VerifyItem, VerifyManifest, VerifyManifestError, buildItemCommand, checkVerifyPending,  # noqa: E402
                                   formatReport, groupByGpuMode, interpretOutcome, kMapLockByGpuMode, kStatusBlocked, kStatusFailed,
                                   makeNotRunResult, makeResult, orderItems, readManifest, resolveCoveredResult)
from common.Worktree import findMainRoot  # noqa: E402

#: 결과를 남기는 폴더(저장소 기준) — `build/` 아래라 추적되지 않는다.
kResultRelDir = "build/verify-batch"
#: 결과 폴더의 시각표 — 항목 시작 · 끝과 항목이 띄운 App 의 시작 · 끝이 시각순으로 한 줄씩 든다(`common/Timeline.py`).
kTimelineFileName = "timeline.log"
#: 혼자 돌아야 하는 종류 — 빌드 · 쿠킹 · 골든은 작업 트리를 바꾸고 측정은 CPU 를 다른 일이 쓰면 값이 오염된다(이유는 `09_Decisions.md` 검증 묶음 병렬 행).
kAloneKindSet = frozenset({"build", "cook", "golden", "measure"})
#: `--jobs` 기본값 — 항목이 자식 프로세스라 코어 수만큼 띄우면 서로 경합한다(`common/Parallel.py` 머리말). 잠금 없는 일 하나와 GPU 잠금 일 하나가 겹칠 만큼만 둔다.
kDefaultJobs = 2
#: 동시에 도는 항목이 시작 · 끝 줄을 찍을 때 한 줄이 섞이지 않게 하는 잠금(항목 출력은 `logs/<id>.log` 로 따로 간다).
_kPrintLock = threading.Lock()


def printLineInternal(text: str) -> None:
    with _kPrintLock:
        print(text, flush=True)


def selectItems(manifest: VerifyManifest, listOnlyId: list[str], listConfigName: list[str]) -> list[VerifyItem]:
    """`--only` · `--config` 로 항목을 고른다(둘 다 없으면 전부). 모르는 id · 구성 이름은 `VerifyManifestError`."""
    for itemId in listOnlyId:
        if manifest.findItem(itemId) is None:
            raise VerifyManifestError(f"--only: 항목 `{itemId}` 가 매니페스트에 없습니다")
    for name in listConfigName:
        manifest.findConfig(name)
    return [item for item in manifest.listItem
            if (not listOnlyId or item.itemId in listOnlyId) and (not listConfigName or item.config in listConfigName)]


def describeEnvironment(repositoryRoot: Path, owner: str) -> str:
    """결과 표의 `측정 환경` 머리 — PC 이름 · CPU · 커밋 · 소유자."""
    commitResult = runGit(["rev-parse", "--short", "HEAD"], cwd=repositoryRoot)
    commit = commitResult.stdout.strip() if commitResult.bSucceeded else "커밋 미상"
    return f"PC {platform.node()} · {platform.processor() or platform.machine()} · {commit} · {owner or '-'}"


def formatNow() -> str:
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M")


@contextlib.contextmanager
def holdLocksInternal(mainRoot: Path, gpuMode: str, owner: str, purpose: str, waitSeconds: int) -> Iterator[int]:
    """GPU 방식에 맞는 잠금들을 잡고 `0` 을 내준다(못 잡으면 그 종료 코드를 내주고 이미 잡은 것을 놓는다). 끝나면 반드시 놓는다."""
    with contextlib.ExitStack() as stack:
        for resource, bShared in kMapLockByGpuMode[gpuMode]:
            code = stack.enter_context(holdLock(mainRoot, resource, bShared, owner, purpose, waitSeconds))
            if code != ExitCode.Passed:
                yield code
                return
        yield ExitCode.Passed


def runItemInternal(item: VerifyItem, preset: str, repositoryRoot: Path, owner: str, outputDir: Path) -> ItemResult:
    """항목 하나를 돌린다. 출력은 `logs/<id>.log` 로 간다."""
    logPath = outputDir / "logs" / f"{item.itemId}.log"
    workDir = outputDir / "work" / item.itemId
    workDir.mkdir(parents=True, exist_ok=True)
    try:
        command = buildItemCommand(item, preset, repositoryRoot, owner, workDir)
    except VerifyManifestError as error:
        return makeResult(item.itemId, kStatusFailed, str(error), formatNow())
    printLineInternal(f"[VerifyBatch] 시작 {item.itemId} ({item.kind}, {item.config}): {' '.join(command)}")
    timelinePath = outputDir / kTimelineFileName
    startedAt = datetime.datetime.now().isoformat(timespec="seconds")
    appendTimelineLine(timelinePath, "시작", item.itemId, item.config)
    startTime = time.perf_counter()
    # 하위 스크립트의 App 실행(`AppRun.runApp`)이 같은 시각표에 줄을 더하도록 경로 · 항목 id 를 환경 변수로 넘긴다(항목이 겹쳐 돌아도 항목마다 따로).
    process = runProcess(command, cwd=repositoryRoot, timeoutSeconds=item.timeoutMinutes * 60.0, stdoutPath=logPath,
                         env={**os.environ, kTimelineEnvName: str(timelinePath), kTimelineItemEnvName: item.itemId})
    seconds = time.perf_counter() - startTime
    outputText = logPath.read_text(encoding="utf-8", errors="replace") if process.bLaunched and logPath.is_file() else process.output
    result = interpretOutcome(item, process.returnCode, process.bLaunched, process.bTimedOut, outputText, seconds, logPath.as_posix(), formatNow())
    result.startedAt = startedAt
    appendTimelineLine(timelinePath, "끝", item.itemId, f"{item.config} {result.status} {seconds:.0f}초")
    printLineInternal(f"[VerifyBatch] 끝 {item.itemId}: {result.status} {result.detail} ({seconds:.0f} 초)")
    return result


@dataclass(frozen=True)
class RunUnit:
    """겹침을 따지는 단위 — 잠금을 한 번 잡고 이어 돌리는 같은 GPU 방식의 항목들(잠금 없는 `none` 은 항목 하나가 단위다)."""

    configName: str
    gpuMode: str
    listItem: tuple[VerifyItem, ...]

    @property
    def setResource(self) -> frozenset[str]:
        """이 단위가 쥐는 잠금 자원 이름들(`kMapLockByGpuMode`)."""
        return frozenset(resource for resource, _ in kMapLockByGpuMode[self.gpuMode])

    @property
    def bAlone(self) -> bool:
        """혼자 돌아야 하는가 — 작업 트리를 바꾸거나(빌드 · 쿠킹 · 골든) 측정이거나(CPU 를 다른 일이 쓰면 값이 오염된다) 독점 잠금이다."""
        return self.gpuMode == "exclusive" or any(item.kind in kAloneKindSet for item in self.listItem)


def hasConflict(first: RunUnit, second: RunUnit) -> bool:
    """두 단위가 같이 돌 수 없는가 — 한쪽이 혼자여야 하거나 같은 잠금 자원을 쥔다(공유끼리도 이 묶음 안에서는 겹치지 않는다: 백엔드를 나눌 수 없다)."""
    return first.bAlone or second.bAlone or bool(first.setResource & second.setResource)


def planUnits(configName: str, listRun: list[VerifyItem]) -> list[RunUnit]:
    """한 구성의 돌 항목(이미 종류 → GPU 방식 순서)을 단위로 나눈다 — 이어진 같은 GPU 방식 묶음이 잠금 한 번(`jobs` 1 이면 옛 순서 · 옛 잠금 그대로), 잠금 없는 묶음은 항목마다."""
    listUnit: list[RunUnit] = []
    for gpuMode, listGroup in groupByGpuMode(listRun):
        if kMapLockByGpuMode[gpuMode]:
            listUnit.append(RunUnit(configName, gpuMode, tuple(listGroup)))
        else:
            listUnit += [RunUnit(configName, gpuMode, (item,)) for item in listGroup]
    return listUnit


def runUnitInternal(unit: RunUnit, preset: str, repositoryRoot: Path, mainRoot: Path, owner: str, waitSeconds: int, outputDir: Path,
                    mapResult: dict[str, ItemResult], mapBuildFailed: dict[str, bool]) -> None:
    """단위 하나의 잠금을 잡고 항목을 차례로 돌려 `mapResult` 에 쓴다. 빌드가 지면 그 구성의 뒤 단위는 막힌다(빌드는 혼자 도는 단위라 뒤 단위가 읽을 때는 이미 정해져 있다)."""
    with holdLocksInternal(mainRoot, unit.gpuMode, owner, f"verify-batch {unit.configName}", waitSeconds) as lockCode:
        for item in unit.listItem:
            if lockCode != 0:
                mapResult[item.itemId] = makeResult(item.itemId, kStatusBlocked, f"GPU 잠금을 잡지 못함(종료 코드 {lockCode})", formatNow())
            elif mapBuildFailed[unit.configName] and item.kind != "build":
                mapResult[item.itemId] = makeResult(item.itemId, kStatusBlocked, f"{unit.configName} 빌드가 졌다", formatNow())
            else:
                mapResult[item.itemId] = runItemInternal(item, preset, repositoryRoot, owner, outputDir)
                if item.kind == "build" and mapResult[item.itemId].status == kStatusFailed:
                    mapBuildFailed[unit.configName] = True


def runBatch(manifest: VerifyManifest, listItem: list[VerifyItem], repositoryRoot: Path, owner: str, waitSeconds: int, outputDir: Path,
             jobs: int = 1) -> list[ItemResult]:
    """고른 항목을 구성 순서 → 종류 → GPU 방식 순서로 돌려 항목마다 결과를 낸다. 돌리지 않는 항목도 결과(건너뜀)를 가진다.

    구성 안에서는 서로 충돌하지 않는 단위(`hasConflict`)를 `jobs` 개까지 겹쳐 돌린다. 결과 목록은 겹침과 무관하게 `listItem` 순서다.
    """
    mainRoot = findMainRoot(repositoryRoot)
    mapResult: dict[str, ItemResult] = {}
    mapBuildFailed: dict[str, bool] = {}
    for configName, listInConfig in orderItems(manifest, listItem):
        preset = manifest.findConfig(configName).preset
        mapBuildFailed[configName] = False
        for item in (item for item in listInConfig if not item.bRunnable and not item.bCovered):
            mapResult[item.itemId] = makeNotRunResult(item, formatNow())
        runConflictSchedule(planUnits(configName, [item for item in listInConfig if item.bRunnable]), hasConflict,
                            lambda unit: runUnitInternal(unit, preset, repositoryRoot, mainRoot, owner, waitSeconds, outputDir, mapResult, mapBuildFailed), jobs)
    for item in (item for item in listItem if item.bCovered):
        mapResult[item.itemId] = resolveCoveredResult(item, mapResult, formatNow())
    return [mapResult[item.itemId] for item in listItem]


def printList(manifest: VerifyManifest) -> None:
    """항목 표 — 돌 수 있는지(실행 · 덮개 · 명령 없음 · wsl)와 일회성(pending) 여부까지."""
    listRow = [("id", "종류", "구성", "gpu", "상태", "일회성")]
    for item in manifest.listItem:
        state = ("사람 확인" if item.manual else "명령 없음" if item.needCommand else "덮개" if item.bCovered else "wsl" if item.platform == "wsl" else "실행")
        listRow.append((item.itemId, item.kind, item.config, item.gpu, state, "예" if item.bPending else "-"))
    listWidth = [max(len(row[column]) for row in listRow) for column in range(len(listRow[0]))]
    for row in listRow:
        print("  ".join(cell.ljust(width) for cell, width in zip(row, listWidth)).rstrip())
    print(f"{len(manifest.listItem)} 항목")


def printDryRun(manifest: VerifyManifest, listItem: list[VerifyItem], repositoryRoot: Path, owner: str, outputDir: Path) -> None:
    """돌 순서와 항목마다의 명령 — 아무것도 돌리지 않는다."""
    for configName, listInConfig in orderItems(manifest, listItem):
        preset = manifest.findConfig(configName).preset
        print(f"\n== {configName} ({preset})")
        for gpuMode, listGroup in groupByGpuMode(listInConfig):
            lockText = ", ".join(f"{resource}{'(공유)' if bShared else '(독점)'}" for resource, bShared in kMapLockByGpuMode[gpuMode]) or "잠금 없음"
            print(f"-- gpu {gpuMode}: {lockText}")
            for item in listGroup:
                if item.bCovered or not item.bRunnable:
                    reason = "덮개 " + item.coveredBy if item.bCovered and not item.bUnrunnable else item.manual or item.needCommand or "wsl"
                    print(f"   {item.itemId}: 돌지 않음 — {reason}")
                    continue
                try:
                    print(f"   {item.itemId}: {' '.join(buildItemCommand(item, preset, repositoryRoot, owner or '<owner>', outputDir / 'work' / item.itemId))}")
                except VerifyManifestError as error:
                    print(f"   {item.itemId}: (명령을 아직 만들 수 없다 — {error})")
        print("-- 겹침 단위(--jobs 2 이상이면 충돌하지 않는 단위끼리 겹친다): "
              + " | ".join(("[혼자] " if unit.bAlone else "") + ",".join(item.itemId for item in unit.listItem)
                           for unit in planUnits(configName, [item for item in listInConfig if item.bRunnable])))


def writeResultFiles(manifest: VerifyManifest, listResult: list[ItemResult], outputDir: Path, environment: str) -> str:
    """`result.md` · `result.json` 을 쓰고 표 글을 돌려준다."""
    report = formatReport(manifest, listResult, f"검증 묶음 결과 {formatNow()}", environment)
    (outputDir / "result.md").write_text(report, encoding="utf-8")
    listRecord = [{"id": result.itemId, "status": result.status, "detail": result.detail, "value": result.valueText, "seconds": round(result.seconds, 1),
                   "log": result.logPath, "started_at": result.startedAt, "finished_at": result.finishedAt} for result in listResult]
    (outputDir / "result.json").write_text(json.dumps({"environment": environment, "results": listRecord}, ensure_ascii=False, indent=2), encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="통합 검증 · 측정 항목(Test/QA/VerifyBatch.json)을 구성별로 모아 한 번에 돌리고 결과 표 하나를 남긴다.")
    parser.add_argument("--list", dest="bList", action="store_true", help="항목 표만 보인다")
    parser.add_argument("--check", dest="bCheck", action="store_true", help="매니페스트와 문서의 대기 줄을 점검한다(틀리면 종료 1)")
    parser.add_argument("--dry-run", dest="bDryRun", action="store_true", help="돌 순서와 명령만 찍는다")
    parser.add_argument("--owner", default="", help="소유자 이름(워크트리 이름) — 빌드 · GPU 잠금에 적힌다. 돌릴 때 필요하다")
    parser.add_argument("--config", action="append", default=[], help="이 구성만(여러 번 가능: Debug · Shipping · Release)")
    parser.add_argument("--only", nargs="+", default=[], help="이 id 의 항목만")
    parser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"잠금을 기다릴 최대 초(기본 {kDefaultWaitSeconds}). 넘으면 그 묶음은 막힘")
    parser.add_argument("--jobs", type=int, default=kDefaultJobs, help=f"동시에 돌 항목 수(기본 {kDefaultJobs}). 1 이면 매니페스트 순서로 하나씩. 빌드 · 쿠킹 · 골든 · 측정 · 독점 잠금 항목은 언제나 혼자")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)
    repositoryRoot = args.root or getProjectRoot()

    if args.bCheck:
        listViolation = checkVerifyPending(repositoryRoot)
        print("\n".join(listViolation))
        print(f"검증 묶음 위반 {len(listViolation)} 건")
        return ExitCode.Violation if listViolation else ExitCode.Passed
    try:
        manifest = readManifest(repositoryRoot)
        listItem = selectItems(manifest, args.only, args.config)
    except VerifyManifestError as error:
        print(f"[VerifyBatch] 오류: {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    if args.bList:
        printList(manifest)
        return ExitCode.Passed
    outputDir = repositoryRoot / kResultRelDir / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    if args.bDryRun:
        printDryRun(manifest, listItem, repositoryRoot, args.owner, outputDir)
        return ExitCode.Passed
    if not args.owner:
        print("[VerifyBatch] 오류: 돌리려면 --owner <이름> 이 필요합니다(잠금에 적힌다)", file=sys.stderr)
        return ExitCode.CheckInvalid

    if args.jobs < 1:
        print("[VerifyBatch] 오류: --jobs 는 1 이상입니다", file=sys.stderr)
        return ExitCode.CheckInvalid
    outputDir.mkdir(parents=True, exist_ok=True)
    listResult = runBatch(manifest, listItem, repositoryRoot, args.owner, args.wait, outputDir, args.jobs)
    environment = describeEnvironment(repositoryRoot, args.owner)
    report = writeResultFiles(manifest, listResult, outputDir, environment)
    print("\n" + report)
    print(f"\n[VerifyBatch] 결과: {(outputDir / 'result.md').as_posix()}")
    return ExitCode.Violation if any(result.status in (kStatusFailed, kStatusBlocked) for result in listResult) else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
