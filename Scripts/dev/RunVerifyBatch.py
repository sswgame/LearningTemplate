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
    py -3 -m Scripts verify-batch --owner integD --fail-fast      # 앞 단계(빌드 → 쿠킹 → ctest → 테스트 → 골든 → 측정)가 지면 뒤는 돌지 않는다
    py -3 -m Scripts verify-batch --owner integD --measure        # 정합성 검증에 측정(성능에 닿는 변경에서만)을 더한다
    py -3 -m Scripts verify-batch --owner integD --measure-only   # 측정과 그 구성의 빌드 · 쿠킹만

기본 실행은 **정합성 검증뿐**이다: `measure` 종류 항목은 `functional`(통과 · 실패를 판정하는 측정)이 아니면 `--measure` · `--measure-only` 가 있을 때만 돈다
(`--only` · `--failed-from` 으로 이름을 부른 항목은 거르지 않는다). 항목의 `requires` 선행이 이번 실행에서 통과하지 못했으면 그 항목은 돌지 않고 `건너뜀 선행 …` 으로 표에 오른다.
`shards` 항목은 같은 필터를 `--test_shard=k/n` 조각 n 개로 나눠 돌리고 결과 하나로 합친다 — 조각끼리는 그 실행 파일의 정책에 같은 `Bin` 잠금(`hostResourceLock`)이 없을 때만
겹치고, 겹쳐 도는 수는 정책의 `gpuSlots` 와 `--jobs` 를 넘지 않는다. `ctest` 항목은 혼자 돈다(`ctest -j` 둘이 겹치면 CPU 경합으로 린트 제한 시간이 가짜로 진다).

항목의 정본은 `Test/QA/VerifyBatch.json` 이고 형식 · 검증은 `common/VerifyManifest.py` 가 가진다. 이 명령은 실행기다 — 새 측정 코드를 쓰지 않고 `full-build` ·
`ctest` · `test` · `perf` · `load` · `perf-compare` · `soak` · `scene-load-bench` · `golden` 을 부른다. 순서는 구성(매니페스트의 `configs` 순서)마다
종류 → GPU 방식이다. GPU 방식이 같은 이어진 항목은 잠금을 한 번만 잡는다: `shared` 는 GPU 공유 칸, `exclusive` 는 GPU 와 빌드 칸을 혼자(측정이 다른 빌드에
오염되지 않게 — 작업 방식 2 절). 한 항목이 져도 나머지는 계속 돈다. 다만 `build` 항목이 지면 같은 구성의 나머지는 `막힘` 이다(없는 바이너리를 돌리지 않는다).
`need_command` 항목은 돌지 않고 이유를 표에 적으며, `wsl` 항목은 사용자가 요청할 때만 하므로 돌지 않는다. 덮개(`covered`) 항목은 덮개 항목의 결과를 따른다.

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
import re
import sys
import threading
import time
from dataclasses import dataclass, field
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
from common.VerifyManifest import (ItemResult, ShardPlan, VerifyItem, VerifyManifest, VerifyManifestError, buildItemCommand, checkVerifyPending,  # noqa: E402
                                   combineShardResults, formatReport, getStageIndex, getStageName, groupByGpuMode, interpretOutcome, kMapLockByGpuMode,
                                   kSkipFailFastMarker, kSkipRequiresMarker, kStatusBlocked, kStatusFailed, kStatusPassed, makeFailFastSkipResult,
                                   makeNotRunResult, makeRequiresSkipResult, makeResult, makeShardArguments, orderItems, planShards, readGpuSlots,
                                   readManifest, resolveCoveredResult)
from common.Worktree import findMainRoot  # noqa: E402

#: 결과를 남기는 폴더(저장소 기준) — `build/` 아래라 추적되지 않는다.
kResultRelDir = "build/verify-batch"
#: 결과 폴더의 시각표 — 항목 시작 · 끝과 항목이 띄운 App 의 시작 · 끝이 시각순으로 한 줄씩 든다(`common/Timeline.py`).
kTimelineFileName = "timeline.log"
#: 혼자 돌아야 하는 종류 — 빌드 · 쿠킹 · 골든은 작업 트리를 바꾸고 측정은 CPU 를 다른 일이 쓰면 값이 오염된다(이유는 `09_Decisions.md` 검증 묶음 병렬 행).
#: `ctest` 도 혼자다: `ctest -j`(코어 수만큼)가 둘 겹치면 CPU 가 갑절로 다퉈 시간 제한이 빠듯한 린트 항목이 가짜로 진다(2026-10-27 `ctest-lint` 와 `ctest-nogpu` 가 같은 초에 시작해
#: `CheckTestSuites` · `CheckTextureFolders` 가 15 초 제한을 넘긴 실패 두 번, 혼자 돌면 통과). 린트의 제한은 혼자 돌 때의 값으로 정해져 있다.
kAloneKindSet = frozenset({"build", "cook", "ctest", "golden", "measure"})
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


#: 다시 돌 항목의 줄 — 졌거나 막힌 것, 그리고 선행이 안 통과했거나 앞 단계가 져서 돌지 못한 것(그 줄들이 `끝 <id>: 건너뜀 선행 …` · `건너뜀 앞 단계 …` 로 찍힌다).
_kFailedLineRe = re.compile(rf"끝 (?P<id>[\w.-]+): (?:실패|막힘|건너뜀 (?:{kSkipRequiresMarker}|{kSkipFailFastMarker}))")


def readFailedIds(logPath: Path) -> list[str]:
    """이전 `verify-batch` 출력 로그에서 다시 돌 항목 id 를 모은다(실패 · 막힘 · 선행 실패나 앞 단계 실패로 건너뜀). 없으면 `VerifyManifestError`(없는 것을 전부 다시 돌리지 않는다)."""
    listId = list(dict.fromkeys(_kFailedLineRe.findall(logPath.read_text(encoding="utf-8", errors="replace"))))
    if not listId:
        raise VerifyManifestError(f"--failed-from: {logPath} 에 실패 · 막힌 항목 줄이 없습니다")
    return listId


def addPrerequisiteItems(manifest: VerifyManifest, listItem: list[VerifyItem], listKind: tuple[str, ...] = ("build",)) -> list[VerifyItem]:
    """고른 항목의 구성에 맞는 빌드(와 `listKind` 의 다른 종류) 항목을 앞에 붙인다(최신이면 몇 초면 끝나고, 낡았으면 실패 원인이 되기 때문이다)."""
    setConfig = {item.config for item in listItem}
    setId = {item.itemId for item in listItem}
    listAdded = [item for item in manifest.listItem if item.kind in listKind and item.config in setConfig and item.itemId not in setId and item.needCommand == ""]
    return [item for item in manifest.listItem if item in listAdded or item in listItem]


def selectProfileItems(manifest: VerifyManifest, listItem: list[VerifyItem], *, bExplicitSelection: bool, bMeasure: bool, bMeasureOnly: bool) -> list[VerifyItem]:
    """병합 검증의 기본 실행에서 측정을 뺀다 — 측정은 성능에 닿는 변경에서만 `--measure`(더하기) · `--measure-only`(측정과 그 구성의 빌드 · 쿠킹만)로 부른다.

    `--only` · `--failed-from` 으로 고른 항목은 사용자가 이름으로 부른 것이라 거르지 않는다. 통과 · 실패를 판정하는 측정 종류 항목(`functional`)은 기본 실행에 든다.
    """
    if bMeasureOnly:
        listMeasure = [item for item in listItem if item.kind == "measure"]
        return addPrerequisiteItems(manifest, listMeasure, ("build", "cook"))
    if bMeasure or bExplicitSelection:
        return listItem
    return [item for item in listItem if item.bIncludedByDefault]


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


def runItemInternal(item: VerifyItem, preset: str, repositoryRoot: Path, owner: str, outputDir: Path, shardIndex: int | None = None,
                    listShardArg: tuple[str, ...] = ()) -> ItemResult:
    """항목 하나(샤드 항목이면 그 조각 하나)를 돌린다. 출력은 `logs/<id>.log`(조각은 `logs/<id>.shard<k>of<n>.log`)로 간다."""
    shardTag = f".shard{shardIndex + 1}of{item.shardCount}" if shardIndex is not None else ""
    logPath = outputDir / "logs" / f"{item.itemId}{shardTag}.log"
    workDir = outputDir / "work" / f"{item.itemId}{shardTag}"
    workDir.mkdir(parents=True, exist_ok=True)
    try:
        command = buildItemCommand(item, preset, repositoryRoot, owner, workDir, listShardArg)
    except VerifyManifestError as error:
        return makeResult(item.itemId, kStatusFailed, str(error), formatNow())
    printLineInternal(f"[VerifyBatch] 시작 {item.itemId}{shardTag} ({item.kind}, {item.config}): {' '.join(command)}")
    timelinePath = outputDir / kTimelineFileName
    startedAt = datetime.datetime.now().isoformat(timespec="seconds")
    appendTimelineLine(timelinePath, "시작", item.itemId, f"{item.config}{(' 샤드 ' + str(shardIndex + 1) + '/' + str(item.shardCount)) if shardIndex is not None else ''}")
    startTime = time.perf_counter()
    # 하위 스크립트의 App 실행(`AppRun.runApp`)이 같은 시각표에 줄을 더하도록 경로 · 항목 id 를 환경 변수로 넘긴다(항목이 겹쳐 돌아도 항목마다 따로).
    process = runProcess(command, cwd=repositoryRoot, timeoutSeconds=item.timeoutMinutes * 60.0, stdoutPath=logPath,
                         env={**os.environ, kTimelineEnvName: str(timelinePath), kTimelineItemEnvName: item.itemId})
    seconds = time.perf_counter() - startTime
    outputText = logPath.read_text(encoding="utf-8", errors="replace") if process.bLaunched and logPath.is_file() else process.output
    result = interpretOutcome(item, process.returnCode, process.bLaunched, process.bTimedOut, outputText, seconds, logPath.as_posix(), formatNow())
    result.startedAt = startedAt
    appendTimelineLine(timelinePath, "끝", item.itemId, f"{item.config} {result.status} {seconds:.0f}초")
    # 샤드 조각의 끝 줄은 `끝 <id> 샤드 k/n:` 이라 `--failed-from` 이 매니페스트에 없는 id 로 읽지 않는다 — 항목 전체의 끝 줄은 조각이 다 끝났을 때 한 번 찍는다(`recordResultInternal`).
    label = f"{item.itemId} 샤드 {shardIndex + 1}/{item.shardCount}" if shardIndex is not None else item.itemId
    printLineInternal(f"[VerifyBatch] 끝 {label}: {result.status} {result.detail} ({seconds:.0f} 초)")
    return result


@dataclass(frozen=True)
class RunUnit:
    """겹침을 따지는 단위 — 잠금을 한 번 잡고 이어 돌리는 같은 GPU 방식의 항목들(잠금 없는 `none` 은 항목 하나가 단위다).

    `shards` 항목은 조각마다 단위 하나다(`shardIndex` 가 조각 번호, `shardPlan` 이 어느 실행 파일의 정책으로 겹쳐 돌지) — 조각 단위는 항목 하나만 든다.
    """

    configName: str
    gpuMode: str
    listItem: tuple[VerifyItem, ...]
    shardIndex: int | None = None
    shardPlan: ShardPlan | None = None

    @property
    def setResource(self) -> frozenset[str]:
        """이 단위가 쥐는 잠금 자원 이름들(`kMapLockByGpuMode`)."""
        return frozenset(resource for resource, _ in kMapLockByGpuMode[self.gpuMode])

    @property
    def bAlone(self) -> bool:
        """혼자 돌아야 하는가 — 작업 트리를 바꾸거나(빌드 · 쿠킹 · 골든) CPU 를 다 쓰거나(`ctest -j`) 측정이거나(CPU 를 다른 일이 쓰면 값이 오염된다) 독점 잠금이다."""
        return self.gpuMode == "exclusive" or any(item.kind in kAloneKindSet for item in self.listItem)

    @property
    def bShard(self) -> bool:
        return self.shardIndex is not None

    @property
    def setItemId(self) -> frozenset[str]:
        return frozenset(item.itemId for item in self.listItem)

    @property
    def setRequired(self) -> frozenset[str]:
        """이 단위의 항목들이 선행으로 적은 항목 id 들."""
        return frozenset(requiredId for item in self.listItem for requiredId in item.listRequires)


def hasConflict(first: RunUnit, second: RunUnit) -> bool:
    """두 단위가 같이 돌 수 없는가 — 뒤가 앞의 결과를 읽거나(`requires`), 한쪽이 혼자여야 하거나, 같은 잠금 자원을 쥔다(공유끼리도 이 묶음 안에서는 겹치지 않는다: 백엔드를 나눌 수 없다).

    예외는 한 `shards` 항목의 조각끼리다: 그 실행 파일의 정책에 같은 `Bin` 을 지키는 잠금(`hostResourceLock`)이 없으면 겹친다(GPU 칸 수는 `runUnitInternal` 의 세마포어가 지킨다).
    """
    if first.bShard and second.bShard and first.listItem[0].itemId == second.listItem[0].itemId:
        return not first.shardPlan.bMayOverlap
    if first.setItemId & second.setRequired:
        return True
    return first.bAlone or second.bAlone or bool(first.setResource & second.setResource)


def makeGroupUnits(configName: str, gpuMode: str, listItem: list[VerifyItem]) -> list[RunUnit]:
    """샤드가 아닌 이어진 항목들의 단위 — 잠금이 있는 GPU 방식이면 잠금 한 번에 이어 돌리는 단위 하나, 잠금 없는 방식이면 항목마다."""
    if not listItem:
        return []
    if kMapLockByGpuMode[gpuMode]:
        return [RunUnit(configName, gpuMode, tuple(listItem))]
    return [RunUnit(configName, gpuMode, (item,)) for item in listItem]


def planUnits(configName: str, listRun: list[VerifyItem], mapShardPlan: dict[str, ShardPlan] | None = None) -> list[RunUnit]:
    """한 구성의 돌 항목(이미 종류 → GPU 방식 순서)을 단위로 나눈다 — 이어진 같은 GPU 방식 묶음이 잠금 한 번(`jobs` 1 이면 옛 순서 · 옛 잠금 그대로), 잠금 없는 묶음은 항목마다.

    `shards` 항목은 묶음에서 떼어 조각마다 단위로 편다(`mapShardPlan[id]` 가 있어야 한다).
    """
    listUnit: list[RunUnit] = []
    for gpuMode, listGroup in groupByGpuMode(listRun):
        listPending: list[VerifyItem] = []
        for item in listGroup:
            if item.shardCount > 1:
                listUnit += makeGroupUnits(configName, gpuMode, listPending)
                listPending = []
                listUnit += [RunUnit(configName, gpuMode, (item,), shardIndex, mapShardPlan[item.itemId]) for shardIndex in range(item.shardCount)]
            else:
                listPending.append(item)
        listUnit += makeGroupUnits(configName, gpuMode, listPending)
    return listUnit


@dataclass
class BatchContext:
    """한 번의 실행이 단위들 사이에서 나누는 것 — 결과 사전과 그것을 지키는 잠금, 샤드 조각을 모으는 칸, GPU 칸 세마포어."""

    repositoryRoot: Path
    mainRoot: Path
    owner: str
    waitSeconds: int
    outputDir: Path
    slotSemaphore: threading.BoundedSemaphore
    mapResult: dict[str, ItemResult] = field(default_factory=dict)
    mapBuildFailed: dict[str, bool] = field(default_factory=dict)
    mapShardPart: dict[str, dict[int, ItemResult]] = field(default_factory=dict)
    lock: threading.Lock = field(default_factory=threading.Lock)


def printNotRunInternal(item: VerifyItem, unit: RunUnit, result: ItemResult) -> None:
    """돌지 않은 결과(막힘 · 선행 실패로 건너뜀 · 앞 단계 실패로 건너뜀)도 끝 줄을 찍는다 — `--failed-from` 이 이 줄로 다시 돌 항목을 고른다."""
    label = f"{item.itemId} 샤드 {unit.shardIndex + 1}/{item.shardCount}" if unit.bShard else item.itemId
    printLineInternal(f"[VerifyBatch] 끝 {label}: {result.status} {result.detail}")


def recordResultInternal(unit: RunUnit, item: VerifyItem, result: ItemResult, context: BatchContext) -> None:
    """결과를 기록한다. 샤드 조각이면 모아 두었다가 마지막 조각이 끝났을 때 항목 결과 하나로 합쳐(`combineShardResults`) 끝 줄을 한 번 찍는다."""
    if not unit.bShard:
        context.mapResult[item.itemId] = result
        return
    with context.lock:
        mapPart = context.mapShardPart.setdefault(item.itemId, {})
        mapPart[unit.shardIndex] = result
        if len(mapPart) < item.shardCount:
            return
        listShard = [mapPart[index] for index in range(item.shardCount)]
    listStart = [shard.startedAt for shard in listShard if shard.startedAt]
    wallSeconds = (datetime.datetime.now() - datetime.datetime.fromisoformat(min(listStart))).total_seconds() if listStart else max(shard.seconds for shard in listShard)
    combined = combineShardResults(item, listShard, wallSeconds, formatNow())
    context.mapResult[item.itemId] = combined
    printLineInternal(f"[VerifyBatch] 끝 {item.itemId}: {combined.status} {combined.detail} ({wallSeconds:.0f} 초)")


def findBlockingResult(item: VerifyItem, unit: RunUnit, lockCode: int, context: BatchContext) -> ItemResult | None:
    """항목을 돌릴 수 없는 이유가 있으면 그 결과 — 잠금을 못 잡음(막힘), 같은 구성의 빌드가 졌음(막힘), 선행이 이번에 통과하지 못했음(건너뜀).

    선행은 이번 실행에서 고른 항목일 때만 본다(안 고른 선행은 이전 실행의 결과를 쓴다). 선행의 단위는 이 단위보다 앞서 끝나므로(`hasConflict`) 읽는 시점에 결과가 정해져 있다.
    """
    if lockCode != 0:
        return makeResult(item.itemId, kStatusBlocked, f"GPU 잠금을 잡지 못함(종료 코드 {lockCode})", formatNow())
    if context.mapBuildFailed[unit.configName] and item.kind != "build":
        return makeResult(item.itemId, kStatusBlocked, f"{unit.configName} 빌드가 졌다", formatNow())
    for requiredId in item.listRequires:
        required = context.mapResult.get(requiredId)
        if required is not None and required.status != kStatusPassed:
            return makeRequiresSkipResult(item, requiredId, required.status, formatNow())
    return None


def runUnitInternal(unit: RunUnit, preset: str, context: BatchContext) -> None:
    """단위 하나의 잠금을 잡고 항목을 차례로 돌려 결과를 기록한다. 빌드가 지면 그 구성의 뒤 단위는 막힌다(빌드는 혼자 도는 단위라 뒤 단위가 읽을 때는 이미 정해져 있다).

    겹쳐 도는 샤드 조각은 GPU 칸 수(`gpuSlots`) 만큼의 세마포어를 먼저 받는다(GPU 잠금을 쥔 채 칸을 기다리지 않게 잠금보다 앞이다).
    """
    with contextlib.ExitStack() as stack:
        if unit.bShard and unit.shardPlan.bMayOverlap:
            stack.enter_context(context.slotSemaphore)
        lockCode = stack.enter_context(holdLocksInternal(context.mainRoot, unit.gpuMode, context.owner, f"verify-batch {unit.configName}", context.waitSeconds))
        for item in unit.listItem:
            blocking = findBlockingResult(item, unit, lockCode, context)
            if blocking is not None:
                recordResultInternal(unit, item, blocking, context)
                printNotRunInternal(item, unit, blocking)
                continue
            if unit.bShard:
                result = runItemInternal(item, preset, context.repositoryRoot, context.owner, context.outputDir, unit.shardIndex,
                                         makeShardArguments(unit.shardPlan, unit.shardIndex, item.shardCount))
            else:
                result = runItemInternal(item, preset, context.repositoryRoot, context.owner, context.outputDir)
            recordResultInternal(unit, item, result, context)
            if item.kind == "build" and result.status == kStatusFailed:
                context.mapBuildFailed[unit.configName] = True


def splitIntoStages(listRun: list[VerifyItem], bFailFast: bool) -> list[tuple[int, list[VerifyItem]]]:
    """돌 항목을 `--fail-fast` 의 단계(`getStageIndex`)로 가른다. 켜지 않았으면 한 덩어리 — 단계 사이에 기다리지 않아 단위가 단계를 넘어 겹친다."""
    if not bFailFast:
        return [(0, listRun)]
    mapStage: dict[int, list[VerifyItem]] = {}
    for item in listRun:
        mapStage.setdefault(getStageIndex(item), []).append(item)
    return sorted(mapStage.items())


def runBatch(manifest: VerifyManifest, listItem: list[VerifyItem], repositoryRoot: Path, owner: str, waitSeconds: int, outputDir: Path,
             jobs: int = 1, *, bFailFast: bool = False, mapShardPlan: dict[str, ShardPlan] | None = None, gpuSlots: int | None = None) -> list[ItemResult]:
    """고른 항목을 구성 순서 → 종류 → GPU 방식 순서로 돌려 항목마다 결과를 낸다. 돌리지 않는 항목도 결과(건너뜀)를 가진다.

    구성 안에서는 서로 충돌하지 않는 단위(`hasConflict`)를 `jobs` 개까지 겹쳐 돌린다. 결과 목록은 겹침과 무관하게 `listItem` 순서다.
    `bFailFast` 면 단계(빌드 → 쿠킹 → ctest → 테스트 → 골든 → 측정) 하나가 끝났을 때 진 것이 있으면 뒤 단계 전부(뒤 구성 포함)를 돌리지 않고 `건너뜀 앞 단계 …` 로 표에 올린다.
    `mapShardPlan` · `gpuSlots` 는 `shards` 항목이 있을 때 정책에서 읽는다(주지 않으면 저장소의 `Test/TestPolicy.json`).
    """
    mainRoot = findMainRoot(repositoryRoot)
    if mapShardPlan is None:
        mapShardPlan = {item.itemId: planShards(repositoryRoot, item) for item in listItem if item.shardCount > 1 and item.bRunnable}
    slots = gpuSlots if gpuSlots is not None else (readGpuSlots(repositoryRoot) if mapShardPlan else 1)
    context = BatchContext(repositoryRoot, mainRoot, owner, waitSeconds, outputDir, threading.BoundedSemaphore(max(1, slots)))
    failedStage: tuple[str, str] | None = None
    for configName, listInConfig in orderItems(manifest, listItem):
        preset = manifest.findConfig(configName).preset
        context.mapBuildFailed[configName] = False
        for item in (item for item in listInConfig if not item.bRunnable and not item.bCovered):
            context.mapResult[item.itemId] = makeNotRunResult(item, formatNow())
        for stageIndex, listStageItem in splitIntoStages([item for item in listInConfig if item.bRunnable], bFailFast):
            if failedStage is not None:
                for item in listStageItem:
                    context.mapResult[item.itemId] = makeFailFastSkipResult(item, failedStage[0], failedStage[1], formatNow())
                    printLineInternal(f"[VerifyBatch] 끝 {item.itemId}: {context.mapResult[item.itemId].status} {context.mapResult[item.itemId].detail}")
                continue
            runConflictSchedule(planUnits(configName, listStageItem, mapShardPlan), hasConflict, lambda unit: runUnitInternal(unit, preset, context), jobs)
            if bFailFast:
                failedItem = next((item for item in listStageItem if context.mapResult[item.itemId].status in (kStatusFailed, kStatusBlocked)), None)
                if failedItem is not None:
                    failedStage = (getStageName(stageIndex), failedItem.itemId)
    for item in (item for item in listItem if item.bCovered):
        context.mapResult[item.itemId] = resolveCoveredResult(item, context.mapResult, formatNow())
    return [context.mapResult[item.itemId] for item in listItem]


def printList(manifest: VerifyManifest) -> None:
    """항목 표 — 돌 수 있는지(실행 · 덮개 · 명령 없음 · wsl), 일회성(pending), 기본 실행에 드는지(측정은 `--measure` 가 있을 때만), 선행 · 샤드까지."""
    listRow = [("id", "종류", "구성", "gpu", "상태", "일회성", "기본", "선행 · 샤드")]
    for item in manifest.listItem:
        state = "덮개" if item.bCovered and not item.needCommand else "명령 없음" if item.needCommand else "wsl" if item.platform == "wsl" else "실행"
        extra = " ".join(part for part in (f"선행 {','.join(item.listRequires)}" if item.listRequires else "", f"샤드 {item.shardCount}" if item.shardCount > 1 else "") if part)
        listRow.append((item.itemId, item.kind, item.config, item.gpu, state, "예" if item.bPending else "-", "예" if item.bIncludedByDefault else "--measure", extra or "-"))
    listWidth = [max(len(row[column]) for row in listRow) for column in range(len(listRow[0]))]
    for row in listRow:
        print("  ".join(cell.ljust(width) for cell, width in zip(row, listWidth)).rstrip())
    print(f"{len(manifest.listItem)} 항목")


def describeUnit(unit: RunUnit) -> str:
    """겹침 단위 하나의 모양 — `[혼자]` 표시, 항목 id 들, 샤드 조각이면 `#k/n`(겹쳐 돌 수 없으면 `순서대로`), 선행이 있으면 `<-`."""
    ids = ",".join(item.itemId for item in unit.listItem)
    shardText = f"#{unit.shardIndex + 1}/{unit.listItem[0].shardCount}{'' if unit.shardPlan.bMayOverlap else '(순서대로 — 실행 파일 정책에 hostResourceLock)'}" if unit.bShard else ""
    requiresText = f" <- {','.join(sorted(unit.setRequired))}" if unit.setRequired else ""
    return ("[혼자] " if unit.bAlone else "") + ids + shardText + requiresText


def printDryRun(manifest: VerifyManifest, listItem: list[VerifyItem], repositoryRoot: Path, owner: str, outputDir: Path, bFailFast: bool = False) -> None:
    """돌 순서와 항목마다의 명령 — 아무것도 돌리지 않는다."""
    mapShardPlan = {item.itemId: planShards(repositoryRoot, item) for item in listItem if item.shardCount > 1 and item.bRunnable}
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
                    shardArg = makeShardArguments(mapShardPlan[item.itemId], 0, item.shardCount) if item.shardCount > 1 else ()
                    shardNote = f"  (샤드 {item.shardCount} 조각으로 나눠 돈다 — 첫 조각의 명령)" if item.shardCount > 1 else ""
                    print(f"   {item.itemId}: {' '.join(buildItemCommand(item, preset, repositoryRoot, owner or '<owner>', outputDir / 'work' / item.itemId, shardArg))}{shardNote}")
                except VerifyManifestError as error:
                    print(f"   {item.itemId}: (명령을 아직 만들 수 없다 — {error})")
        print(f"-- 겹침 단위(--jobs 2 이상이면 충돌하지 않는 단위끼리 겹친다{' · --fail-fast 단계 사이에서 기다린다' if bFailFast else ''}):")
        for stageIndex, listStageItem in splitIntoStages([item for item in listInConfig if item.bRunnable], bFailFast):
            stageText = f"[{getStageName(stageIndex)}] " if bFailFast else ""
            print("   " + stageText + " | ".join(describeUnit(unit) for unit in planUnits(configName, listStageItem, mapShardPlan)))


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
    parser.add_argument("--failed-from", dest="failedFrom", type=Path, default=None,
                        help="이전 실행 출력 로그에서 실패 · 막힌 항목과 그 구성의 빌드만 다시 돈다(통과한 것은 다시 돌리지 않는다)")
    parser.add_argument("--skip-kind", nargs="+", default=[], help="이 종류의 항목은 뺀다(예: ctest)")
    parser.add_argument("--measure", dest="bMeasure", action="store_true",
                        help="측정(measure) 항목도 돈다. 기본 실행은 정합성 검증만이다 — 성능에 닿는 변경에서만 이 옵션이나 --measure-only 로 측정을 부른다(통과 · 실패를 판정하는 functional 측정은 기본에 든다)")
    parser.add_argument("--measure-only", dest="bMeasureOnly", action="store_true", help="측정 항목과 그 구성의 빌드 · 쿠킹만 돈다(정합성 검증은 이미 통과했다고 보고 성능만 잰다)")
    parser.add_argument("--fail-fast", dest="bFailFast", action="store_true",
                        help="단계(빌드 → 쿠킹 → ctest → 테스트 → 골든 → 측정)가 끝났을 때 진 것이 있으면 뒤 단계(뒤 구성 포함)는 돌리지 않고 `건너뜀 앞 단계 …` 로 표에 올린다")
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
        if args.bMeasure and args.bMeasureOnly:
            raise VerifyManifestError("--measure 와 --measure-only 는 함께 쓰지 않습니다(앞은 정합성 검증에 측정을 더하고 뒤는 측정만 돈다)")
        listOnly = args.only + (readFailedIds(args.failedFrom) if args.failedFrom else [])
        listItem = selectItems(manifest, listOnly, args.config)
        if args.failedFrom:
            listItem = addPrerequisiteItems(manifest, listItem)
        listItem = selectProfileItems(manifest, listItem, bExplicitSelection=bool(listOnly), bMeasure=args.bMeasure, bMeasureOnly=args.bMeasureOnly)
        listItem = [item for item in listItem if item.kind not in args.skip_kind]
    except VerifyManifestError as error:
        print(f"[VerifyBatch] 오류: {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    if args.bList:
        printList(manifest)
        return ExitCode.Passed
    outputDir = repositoryRoot / kResultRelDir / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    if args.bDryRun:
        try:
            printDryRun(manifest, listItem, repositoryRoot, args.owner, outputDir, args.bFailFast)
        except VerifyManifestError as error:
            print(f"[VerifyBatch] 오류: {error}", file=sys.stderr)
            return ExitCode.CheckInvalid
        return ExitCode.Passed
    if not args.owner:
        print("[VerifyBatch] 오류: 돌리려면 --owner <이름> 이 필요합니다(잠금에 적힌다)", file=sys.stderr)
        return ExitCode.CheckInvalid

    if args.jobs < 1:
        print("[VerifyBatch] 오류: --jobs 는 1 이상입니다", file=sys.stderr)
        return ExitCode.CheckInvalid
    outputDir.mkdir(parents=True, exist_ok=True)
    try:
        listResult = runBatch(manifest, listItem, repositoryRoot, args.owner, args.wait, outputDir, args.jobs, bFailFast=args.bFailFast)
    except VerifyManifestError as error:
        print(f"[VerifyBatch] 오류: {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    environment = describeEnvironment(repositoryRoot, args.owner)
    report = writeResultFiles(manifest, listResult, outputDir, environment)
    print("\n" + report)
    print(f"\n[VerifyBatch] 결과: {(outputDir / 'result.md').as_posix()}")
    return ExitCode.Violation if any(result.status in (kStatusFailed, kStatusBlocked) for result in listResult) else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
