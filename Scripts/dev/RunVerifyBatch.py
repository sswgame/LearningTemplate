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
`need_command` 항목은 돌지 않고 이유를 표에 적으며, `wsl` 항목은 사용자가 요청할 때만 하므로 돌지 않는다. 덮개(`covered`) 항목은 덮개 항목의 결과를 따른다.

결과는 `build/verify-batch/<시각>/` 의 `result.md`(표 하나) · `result.json` · `logs/<id>.log` 에 남고 표는 끝에 한 번 더 찍는다. 종료 코드는 실패나 막힘이 있으면 1 이다.
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
import time
from pathlib import Path
from typing import Iterator

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Host import runGit  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Process import runProcess  # noqa: E402
from common.SharedLock import acquire, getSharedSlotCount, kDefaultWaitSeconds, kMapSharedSlotCount, releaseHeld  # noqa: E402
from common.VerifyManifest import (ItemResult, VerifyItem, VerifyManifest, VerifyManifestError, buildItemCommand, checkVerifyPending,  # noqa: E402
                                   formatReport, groupByGpuMode, interpretOutcome, kMapLockByGpuMode, kStatusBlocked, kStatusFailed,
                                   makeNotRunResult, makeResult, orderItems, readManifest, resolveCoveredResult)
from common.Worktree import findMainRoot  # noqa: E402

#: 결과를 남기는 폴더(저장소 기준) — `build/` 아래라 추적되지 않는다.
kResultRelDir = "build/verify-batch"


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
    listHeld: list[argparse.Namespace] = []
    try:
        for resource, bShared in kMapLockByGpuMode[gpuMode]:
            args = argparse.Namespace(resource=resource, owner=owner, purpose=purpose, shared=bShared, pid=os.getpid(), wait=waitSeconds,
                                      slots=getSharedSlotCount(resource) if bShared else kMapSharedSlotCount.get(resource, 0), belowNormal=False)
            code = acquire(mainRoot, args)
            if code != 0:
                yield code
                return
            listHeld.append(args)
        yield 0
    finally:
        for args in reversed(listHeld):
            releaseHeld(mainRoot, args)


def runItemInternal(item: VerifyItem, preset: str, repositoryRoot: Path, owner: str, outputDir: Path) -> ItemResult:
    """항목 하나를 돌린다. 출력은 `logs/<id>.log` 로 간다."""
    logPath = outputDir / "logs" / f"{item.itemId}.log"
    workDir = outputDir / "work" / item.itemId
    workDir.mkdir(parents=True, exist_ok=True)
    try:
        command = buildItemCommand(item, preset, repositoryRoot, owner, workDir)
    except VerifyManifestError as error:
        return makeResult(item.itemId, kStatusFailed, str(error), formatNow())
    print(f"[VerifyBatch] 시작 {item.itemId} ({item.kind}, {item.config}): {' '.join(command)}", flush=True)
    startTime = time.perf_counter()
    process = runProcess(command, cwd=repositoryRoot, timeoutSeconds=item.timeoutMinutes * 60.0, stdoutPath=logPath)
    seconds = time.perf_counter() - startTime
    outputText = logPath.read_text(encoding="utf-8", errors="replace") if process.bLaunched and logPath.is_file() else process.output
    result = interpretOutcome(item, process.returnCode, process.bLaunched, process.bTimedOut, outputText, seconds, logPath.as_posix(), formatNow())
    print(f"[VerifyBatch] 끝 {item.itemId}: {result.status} {result.detail} ({seconds:.0f} 초)", flush=True)
    return result


def runBatch(manifest: VerifyManifest, listItem: list[VerifyItem], repositoryRoot: Path, owner: str, waitSeconds: int, outputDir: Path) -> list[ItemResult]:
    """고른 항목을 구성 순서 → 종류 → GPU 방식 순서로 돌려 항목마다 결과를 낸다. 돌리지 않는 항목도 결과(건너뜀)를 가진다."""
    mainRoot = findMainRoot(repositoryRoot)
    mapResult: dict[str, ItemResult] = {}
    for configName, listInConfig in orderItems(manifest, listItem):
        preset = manifest.findConfig(configName).preset
        bBuildFailed = False
        for gpuMode, listGroup in groupByGpuMode(listInConfig):
            for item in (item for item in listGroup if not item.bRunnable and not item.bCovered):
                mapResult[item.itemId] = makeNotRunResult(item, formatNow())
            listRun = [item for item in listGroup if item.bRunnable]
            if not listRun:
                continue
            with holdLocksInternal(mainRoot, gpuMode, owner, f"verify-batch {configName}", waitSeconds) as lockCode:
                for item in listRun:
                    if lockCode != 0:
                        mapResult[item.itemId] = makeResult(item.itemId, kStatusBlocked, f"GPU 잠금을 잡지 못함(종료 코드 {lockCode})", formatNow())
                    elif bBuildFailed and item.kind != "build":
                        mapResult[item.itemId] = makeResult(item.itemId, kStatusBlocked, f"{configName} 빌드가 졌다", formatNow())
                    else:
                        mapResult[item.itemId] = runItemInternal(item, preset, repositoryRoot, owner, outputDir)
                        bBuildFailed = bBuildFailed or (item.kind == "build" and mapResult[item.itemId].status == kStatusFailed)
    for item in (item for item in listItem if item.bCovered):
        mapResult[item.itemId] = resolveCoveredResult(item, mapResult, formatNow())
    return [mapResult[item.itemId] for item in listItem]


def printList(manifest: VerifyManifest) -> None:
    """항목 표 — 돌 수 있는지(실행 · 덮개 · 명령 없음 · wsl)와 일회성(pending) 여부까지."""
    listRow = [("id", "종류", "구성", "gpu", "상태", "일회성")]
    for item in manifest.listItem:
        state = "덮개" if item.bCovered and not item.needCommand else "명령 없음" if item.needCommand else "wsl" if item.platform == "wsl" else "실행"
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
                    reason = "덮개 " + item.coveredBy if item.bCovered and not item.needCommand else item.needCommand or "wsl"
                    print(f"   {item.itemId}: 돌지 않음 — {reason}")
                    continue
                try:
                    print(f"   {item.itemId}: {' '.join(buildItemCommand(item, preset, repositoryRoot, owner or '<owner>', outputDir / 'work' / item.itemId))}")
                except VerifyManifestError as error:
                    print(f"   {item.itemId}: (명령을 아직 만들 수 없다 — {error})")


def writeResultFiles(manifest: VerifyManifest, listResult: list[ItemResult], outputDir: Path, environment: str) -> str:
    """`result.md` · `result.json` 을 쓰고 표 글을 돌려준다."""
    report = formatReport(manifest, listResult, f"검증 묶음 결과 {formatNow()}", environment)
    (outputDir / "result.md").write_text(report, encoding="utf-8")
    listRecord = [{"id": result.itemId, "status": result.status, "detail": result.detail, "value": result.valueText, "seconds": round(result.seconds, 1),
                   "log": result.logPath, "finished_at": result.finishedAt} for result in listResult]
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
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 스크립트의 저장소)")
    args = parser.parse_args(argv)
    repositoryRoot = args.root or getProjectRoot()

    if args.bCheck:
        listViolation = checkVerifyPending(repositoryRoot)
        print("\n".join(listViolation))
        print(f"검증 묶음 위반 {len(listViolation)} 건")
        return 1 if listViolation else 0
    try:
        manifest = readManifest(repositoryRoot)
        listItem = selectItems(manifest, args.only, args.config)
    except VerifyManifestError as error:
        print(f"[VerifyBatch] 오류: {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    if args.bList:
        printList(manifest)
        return 0
    outputDir = repositoryRoot / kResultRelDir / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    if args.bDryRun:
        printDryRun(manifest, listItem, repositoryRoot, args.owner, outputDir)
        return 0
    if not args.owner:
        print("[VerifyBatch] 오류: 돌리려면 --owner <이름> 이 필요합니다(잠금에 적힌다)", file=sys.stderr)
        return ExitCode.CheckInvalid

    outputDir.mkdir(parents=True, exist_ok=True)
    listResult = runBatch(manifest, listItem, repositoryRoot, args.owner, args.wait, outputDir)
    environment = describeEnvironment(repositoryRoot, args.owner)
    report = writeResultFiles(manifest, listResult, outputDir, environment)
    print("\n" + report)
    print(f"\n[VerifyBatch] 결과: {(outputDir / 'result.md').as_posix()}")
    return 1 if any(result.status in (kStatusFailed, kStatusBlocked) for result in listResult) else 0


if __name__ == "__main__":
    sys.exit(main())
