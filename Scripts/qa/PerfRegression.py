#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
성능 회귀 — Release App 으로 시험 게임의 자동 플레이를 여러 판 재고, 구간별 p50 · p99 를 이 기계의 기준과 견준다.

    py -3 -m Scripts perf --app build/Ninja-Release/Bin/App.exe                    # 비교(기준이 없으면 77 = 건너뜀)
    py -3 -m Scripts perf --app build/Ninja-Release/Bin/App.exe --record           # 이 기계의 기준을 새로 뜬다

기준: `Test/Qa/Perf/<게임>.json` 의 `machines.<기계 키>` — **기계마다 다르다**(CPU · GPU · 드라이버가 숫자를 정한다). 기계 키는 호스트 이름과
프로세서 이름이다. 판마다 `-gv_profileFrames=N`(워밍업 60 프레임은 엔진이 버린다)으로 재고, 판들의 중앙값을 쓴다 — 한 판의 max 는 잡음이다
(백로그 3 절 "측정"). 판정: 값 > 기준 × (1 + 허용) + 바닥(us) 이면 회귀다. 개선은 알리기만 한다.

Debug 숫자로는 재지 않는다 — Debug 는 경합 검출기 · 할당 추적으로 컨테이너 코드가 과장된다(빌드 형식이 Release 가 아니면 멈춘다).
"""

from __future__ import annotations

import argparse
import datetime
import json
import platform
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import getProjectRoot  # noqa: E402
from common.AppRun import (kBackendSwitch, kSkipExitCode, findBuildDirOfApp, loadGameTable, parseProfileTable,  # noqa: E402
                           parseProfileWall, readCMakeCacheValue, runApp)

#: 기준으로 들고 있는 구간 — 프레임 전체(게임 · 렌더 스레드 · GPU)와 그 아래 큰 덩어리. `GPU.Frame` 은 네 백엔드의 타임스탬프(프레임 첫 명령 ~ 마지막 패스 끝)다 —
#: 기준에 없는 구간은 비교하지 않으므로(`compareInternal` 은 기준을 돈다) 새로 뜬 기준부터 잡힌다.
kTrackedScope: tuple[str, ...] = ("GT.Frame", "RT.Frame", "GPU.Frame", "GT.Scene.tick", "GT.Game.update", "RT.ExecutePacket")


def makeMachineKey() -> str:
    return f"{platform.node()}|{platform.processor() or platform.machine()}"


def measureInternal(appPath: Path, listArgument: list[str], runCount: int) -> tuple[dict[str, dict[str, float]], list[str]]:
    """판마다 재서 구간별 (p50, p99, avg) 중앙값을 돌려줍니다. 실패한 판은 이유를 모은다."""
    mapSample: dict[str, dict[str, list[float]]] = {}
    listFailure: list[str] = []
    for runIndex in range(runCount):
        result = runApp(appPath, listArgument, timeoutSeconds=600.0)
        if result.bBackendUnusable:
            listFailure.append("backend unusable on this machine")
            break
        if not result.bClean:
            listFailure.append(f"run {runIndex}: App did not finish cleanly ({(result.listErrorLine or [f'exit {result.exitCode}'])[0]})")
            continue
        mapProfile = parseProfileTable(result.listLine)
        wall = parseProfileWall(result.listLine)
        for scope in kTrackedScope:
            row = mapProfile.get(scope)
            if row is None:
                continue
            bucket = mapSample.setdefault(scope, {"p50_us": [], "p99_us": [], "avg_us": []})
            bucket["p50_us"].append(row.p50Micro)
            bucket["p99_us"].append(row.p99Micro)
            bucket["avg_us"].append(row.avgMicro)
        if wall is not None:
            mapSample.setdefault("Wall.Frame", {"p50_us": [], "p99_us": [], "avg_us": []})["avg_us"].append(wall[2])
        print(f"[Perf] run {runIndex + 1}/{runCount}: " + ", ".join(
            f"{scope} p50 {mapProfile[scope].p50Micro} p99 {mapProfile[scope].p99Micro}" for scope in kTrackedScope[:2] if scope in mapProfile),
            flush=True)
    mapMedian = {scope: {name: statistics.median(values) for name, values in bucket.items() if values}
                 for scope, bucket in mapSample.items()}
    return mapMedian, listFailure


def compareInternal(baseline: dict[str, dict[str, float]], measured: dict[str, dict[str, float]], tolerance: dict[str, float],
                    floorMicro: float) -> tuple[list[str], list[str]]:
    listRegression: list[str] = []
    listNote: list[str] = []
    for scope, values in baseline.items():
        for name, expected in values.items():
            actual = measured.get(scope, {}).get(name)
            if actual is None:
                listNote.append(f"{scope} {name}: not measured this time")
                continue
            limit = expected * (1.0 + tolerance.get(name, 0.2)) + floorMicro
            if actual > limit:
                listRegression.append(f"{scope} {name}: {actual:.0f} us > {limit:.0f} us (baseline {expected:.0f})")
            elif actual < expected * (1.0 - tolerance.get(name, 0.2)) - floorMicro:
                listNote.append(f"{scope} {name}: faster {actual:.0f} us (baseline {expected:.0f}) - re-record to lock it in")
    return listRegression, listNote


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Per-game frame-time p50/p99 against a stored per-machine baseline (Release)")
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--game", default=None, help="default: SW_ACTIVE_GAME of the App's build")
    parser.add_argument("--backend", default="dx12", choices=list(kBackendSwitch))
    parser.add_argument("--frames", type=int, default=600, help="measured frames per run (after the 60-frame warm-up)")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--record", action="store_true", help="store this machine's baseline")
    parser.add_argument("--tolerance-p50", type=float, default=0.15)
    parser.add_argument("--tolerance-p99", type=float, default=0.35)
    parser.add_argument("--floor-us", type=float, default=150.0, help="absolute slack added to every limit")
    parser.add_argument("--allow-debug", action="store_true", help="measure a non-Release build anyway (numbers mean little)")
    args = parser.parse_args(listArgument)

    repositoryRoot = getProjectRoot()
    buildDir = findBuildDirOfApp(args.app)
    gameName = args.game or readCMakeCacheValue(buildDir, "SW_ACTIVE_GAME")
    buildType = readCMakeCacheValue(buildDir, "CMAKE_BUILD_TYPE")
    table = loadGameTable(repositoryRoot)
    if not gameName or gameName not in table["games"]:
        print(f"[Perf] unknown game '{gameName}' - add it to Test/Qa/Games.json", file=sys.stderr)
        return 1
    if buildType != "Release" and not args.allow_debug:
        print(f"[Perf] {buildDir.name} is a {buildType} build - measure Release (or pass --allow-debug)", file=sys.stderr)
        return 1

    listAppArgument = [*table["games"][gameName]["arguments"], f"-gv_profileFrames={args.frames}", kBackendSwitch[args.backend]]
    measured, listFailure = measureInternal(args.app, listAppArgument, max(1, args.runs))
    if listFailure:
        for failure in listFailure:
            print(f"[Perf] FAIL {failure}", flush=True)
        return kSkipExitCode if listFailure == ["backend unusable on this machine"] else 1
    if "GT.Frame" not in measured:
        print("[Perf] FAIL no [Profile] table in the output", flush=True)
        return 1

    baselinePath = repositoryRoot / "Test/Qa/Perf" / f"{gameName}.json"
    document = json.loads(baselinePath.read_text(encoding="utf-8")) if baselinePath.is_file() else {"game": gameName, "machines": {}}
    machineKey = makeMachineKey()
    entryKey = f"{machineKey}|{args.backend}"
    for scope, values in measured.items():
        print(f"[Perf] {gameName} {args.backend} {scope}: " + ", ".join(f"{name} {value:.0f}" for name, value in sorted(values.items())), flush=True)

    if args.record:
        document["machines"][entryKey] = {
            "recorded": datetime.date.today().isoformat(), "frames": args.frames, "runs": args.runs,
            "arguments": listAppArgument, "scopes": measured,
        }
        baselinePath.parent.mkdir(parents=True, exist_ok=True)
        baselinePath.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"[Perf] baseline recorded for {entryKey} -> {baselinePath.relative_to(repositoryRoot)}", flush=True)
        return 0

    entry = document["machines"].get(entryKey)
    if entry is None:
        print(f"[Perf] no baseline for this machine ({entryKey}) - record one with --record", flush=True)
        return kSkipExitCode
    if entry.get("frames") != args.frames or entry.get("arguments") != listAppArgument:
        print("[Perf] the baseline was measured with other arguments - re-record it", flush=True)
        return 1
    tolerance = {"p50_us": args.tolerance_p50, "p99_us": args.tolerance_p99, "avg_us": args.tolerance_p50}
    listRegression, listNote = compareInternal(entry["scopes"], measured, tolerance, args.floor_us)
    for note in listNote:
        print(f"[Perf] note {note}", flush=True)
    for regression in listRegression:
        print(f"[Perf] REGRESSION {regression}", flush=True)
    print("[Perf] PASS" if not listRegression else "[Perf] FAIL", flush=True)
    return 1 if listRegression else 0


if __name__ == "__main__":
    sys.exit(main())
