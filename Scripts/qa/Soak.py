#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
봇 플레이 장시간 실행(soak) — 시험 게임의 자동 플레이를 오래 돌리며 메모리 · 핸들 증가와 프레임 p99 를 본다.

    py -3 -m Scripts soak --app build/Ninja-Debug-NileCity/Bin/App.exe --minutes 10
    py -3 -m Scripts soak --app <App> --backend vk --minutes 30 --report soak.json

App 은 `-gv_profileSeconds=<분 × 60>` 으로 그 시간을 재고 스스로 끝난다(프레임 수로 끊으면 장면마다 몇 초인지 모른다). 그동안 이 스크립트가
밖에서 자원을 잰다(사유 메모리 · 작업 집합 · 핸들 · GDI/USER 객체). 판정:
  - 종료 코드 0, `[Error]` 0 줄
  - 워밍업 뒤 사유 메모리 증가 기울기 ≤ `--max-private-mb-per-minute`, 핸들 증가 기울기 ≤ `--max-handles-per-minute`
  - (`--max-p99-ms` 를 주면) GT.Frame · RT.Frame 의 p99 ≤ 그 값
기울기는 최소제곱이다 — 처음 · 끝 두 점만 보면 GC · 캐시 채우기 같은 한 번의 계단을 누수로 읽는다. 언리얼 Gauntlet 의 soak 시험
(`-ExecCmds` 로 봇을 돌리고 `memreport` 로 비교)과 같은 자리이고, 여기서는 표본을 밖에서 계속 뜨는 것이 다르다.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import getProjectRoot  # noqa: E402
from common import BuildTree  # noqa: E402
from common.AppRun import kBackendSwitch, computeSlopePerMinute, loadGameTable, parseProfileTable, parseProfileWall, runApp  # noqa: E402

kMegabyte = 1024.0 * 1024.0


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Long autoplay run with memory/handle growth and frame-time checks")
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--game", default=None, help="default: SW_ACTIVE_GAME of the App's build")
    parser.add_argument("--backend", default="dx12", choices=list(kBackendSwitch))
    parser.add_argument("--minutes", type=float, default=10.0)
    parser.add_argument("--sample-seconds", type=float, default=5.0)
    parser.add_argument("--warmup-fraction", type=float, default=0.25, help="leading part of the run left out of the growth slope")
    parser.add_argument("--max-private-mb-per-minute", type=float, default=2.0)
    parser.add_argument("--max-handles-per-minute", type=float, default=10.0)
    parser.add_argument("--max-p99-ms", type=float, default=0.0, help="0 = report only")
    parser.add_argument("--report", type=Path, default=None, help="write the samples and verdict as JSON")
    args = parser.parse_args(listArgument)

    repositoryRoot = getProjectRoot()
    gameName = args.game or BuildTree.ofApp(args.app).readCacheValue("SW_ACTIVE_GAME")
    table = loadGameTable(repositoryRoot)
    if not gameName or gameName not in table["games"]:
        print(f"[Soak] unknown game '{gameName}' - add it to Test/Qa/Games.json", file=sys.stderr)
        return 1
    seconds = max(10, int(args.minutes * 60))
    listAppArgument = [*table["games"][gameName]["arguments"], f"-gv_profileSeconds={seconds}", kBackendSwitch[args.backend]]
    print(f"[Soak] {gameName} on {args.backend} for {seconds} s: App {' '.join(listAppArgument)}", flush=True)
    result = runApp(args.app, listAppArgument, timeoutSeconds=seconds + 180.0, sampleIntervalSeconds=args.sample_seconds)

    listFailure: list[str] = []
    if result.bBackendUnusable:
        print(f"[Soak] {args.backend} is unusable on this machine", flush=True)
        return 77
    if not result.bClean:
        detail = result.listErrorLine[:3] if result.listErrorLine else [f"exit {result.exitCode}, timed out={result.bTimedOut}"]
        listFailure.append(f"App did not finish cleanly: {detail}")

    warmupSeconds = seconds * args.warmup_fraction
    listSteady = [sample for sample in result.listSample if sample.seconds >= warmupSeconds]
    privateSlope = computeSlopePerMinute([(sample.seconds, sample.privateBytes / kMegabyte) for sample in listSteady])
    workingSetSlope = computeSlopePerMinute([(sample.seconds, sample.workingSetBytes / kMegabyte) for sample in listSteady])
    handleSlope = computeSlopePerMinute([(sample.seconds, float(sample.handleCount)) for sample in listSteady])
    guiSlope = computeSlopePerMinute([(sample.seconds, float(sample.guiObjectCount)) for sample in listSteady])
    if len(listSteady) < 3:
        listFailure.append(f"only {len(listSteady)} steady-state samples - the run ended early ({result.seconds:.0f} s)")
    if privateSlope > args.max_private_mb_per_minute:
        listFailure.append(f"private memory grows {privateSlope:.2f} MB/min (limit {args.max_private_mb_per_minute})")
    if handleSlope > args.max_handles_per_minute:
        listFailure.append(f"handles grow {handleSlope:.1f}/min (limit {args.max_handles_per_minute})")

    mapProfile = parseProfileTable(result.listLine)
    wall = parseProfileWall(result.listLine)
    listFrameRow = [mapProfile[scope] for scope in ("GT.Frame", "RT.Frame") if scope in mapProfile]
    if not listFrameRow:
        listFailure.append("no [Profile] table in the output (Shipping drops Info logs - soak a Debug/Release build)")
    for row in listFrameRow:
        if args.max_p99_ms > 0.0 and row.p99Micro / 1000.0 > args.max_p99_ms:
            listFailure.append(f"{row.scope} p99 {row.p99Micro / 1000.0:.2f} ms exceeds {args.max_p99_ms} ms")

    first = listSteady[0] if listSteady else None
    last = listSteady[-1] if listSteady else None
    print(f"[Soak] samples {len(result.listSample)} ({len(listSteady)} after {warmupSeconds:.0f} s warm-up), run {result.seconds:.0f} s", flush=True)
    if first is not None and last is not None:
        print(f"[Soak] private {first.privateBytes / kMegabyte:.1f} -> {last.privateBytes / kMegabyte:.1f} MB ({privateSlope:+.2f} MB/min), "
              f"working set {workingSetSlope:+.2f} MB/min, handles {first.handleCount} -> {last.handleCount} ({handleSlope:+.1f}/min), "
              f"GDI+USER {first.guiObjectCount} -> {last.guiObjectCount} ({guiSlope:+.1f}/min)", flush=True)
    for row in listFrameRow:
        print(f"[Soak] {row.scope}: p50 {row.p50Micro / 1000.0:.2f} ms, p99 {row.p99Micro / 1000.0:.2f} ms, max {row.maxMicro / 1000.0:.2f} ms", flush=True)
    if wall is not None:
        print(f"[Soak] wall: {wall[0]} frames in {wall[1] / 1000.0:.0f} s = {wall[2]} us/frame", flush=True)

    if args.report is not None:
        report = {
            "game": gameName, "backend": args.backend, "seconds": seconds, "exit_code": result.exitCode,
            "private_mb_per_minute": privateSlope, "working_set_mb_per_minute": workingSetSlope,
            "handles_per_minute": handleSlope, "gui_objects_per_minute": guiSlope,
            "frame": {row.scope: {"p50_us": row.p50Micro, "p99_us": row.p99Micro, "max_us": row.maxMicro} for row in listFrameRow},
            "samples": [sample.__dict__ for sample in result.listSample],
            "failures": listFailure,
        }
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    for failure in listFailure:
        print(f"[Soak] FAIL {failure}", flush=True)
    if not listFailure:
        print("[Soak] PASS", flush=True)
    return 1 if listFailure else 0


if __name__ == "__main__":
    sys.exit(main())
