#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
성능 변경의 전/후 비교를 한 번에 — 케이스마다 A(전) · B(후)를 번갈아 여러 판 재고, 구간 · 벤치 값의 중앙값을 표로 낸다.

    py -3 -m Scripts perf-compare --app build/Ninja-Release/Bin/App.exe                               # 같은 바이너리 스위치 A/B 만
    py -3 -m Scripts perf-compare --app <후>/App.exe --app-before <전>/App.exe                         # 바이너리 A/B 까지
    py -3 -m Scripts perf-compare --app <후>/App.exe --cases tickBatch fractureWarm --runs 5           # 고른 케이스만

케이스는 두 종류다. **스위치 A/B** 는 한 바이너리에서 `-gv_` 스위치만 바꾼다(전 바이너리가 없어도 된다). **바이너리 A/B** 는
`--app-before` 의 바이너리(전 커밋을 전체 빌드한 것)와 `--app` 을 같은 인자로 돌린다 — 없으면 그 케이스는 건너뛴다.
테스트 벤치 케이스는 App 옆의 `EngineTest` 를 `--test_filter` 로 돌려 `[Bench]` 줄(min · p50 · p90 · max us)을 읽는다.

재는 법은 docs/08_Verification.md 2 절을 따른다: Release, VSync 끔(기본), 전 · 후를 **번갈아**, 판들의 중앙값. 측정하는 동안 다른 빌드를 막는다.
값은 기계마다 다르므로 같은 PC 의 전 · 후만 견준다. 표 머리에 PC 이름과 CPU 를 적는다.
"""

from __future__ import annotations

import argparse
import platform
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common.AppRun import addAppRunArguments, kBackendSwitch, kBenchRowRe, parseProfileTable, runApp  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402


@dataclass(frozen=True)
class PerfCase:
    """비교 케이스 하나. `kind` 는 app · test, `mode` 는 switch(같은 바이너리) · binary(전 · 후 바이너리) · single(B 만 — 기준값)."""

    name: str
    kind: str
    mode: str
    note: str
    listCommon: tuple[str, ...]
    listBefore: tuple[str, ...] = ()
    listAfter: tuple[str, ...] = ()
    listScope: tuple[str, ...] = ()
    listCounter: tuple[str, ...] = ()


#: 이번 세션(2026-10-12 ~ 13)에 들어간 성능 변경과 이번에 만든 측정. 스위치 A 가 "전" 이다.
kListCase: tuple[PerfCase, ...] = (
    PerfCase("tickBatch", "app", "switch", "타입별 일괄 틱(TickBatchRegistry) — 무버 8000, -gv_tickBatch=0 이 전",
             ("-gv_benchMeshes=8000", "-gv_benchTickMovers=1"), ("-gv_tickBatch=0",), ("-gv_tickBatch=1",),
             ("GT.Frame", "GT.Scene.tick.components", "GT.Scene.tick.objectRange", "GT.Scene.tick.pendingSlotApply", "GT.Scene.tick.queuedTransforms"),
             ("GT.Scene.tick.objectEntries", "GT.Scene.tick.batchElements", "GT.Scene.tick.pendingSlots")),
    PerfCase("tickCostMovers", "app", "switch", "씬 틱 병렬 분배를 잰 비용으로 — 큰 씬(무버 8000), -gv_tickCostDispatch=0(항목 16 개 이상이면 늘 나눈다)이 전",
             ("-gv_benchMeshes=8000", "-gv_benchTickMovers=1"), ("-gv_tickCostDispatch=0",), ("-gv_tickCostDispatch=1",),
             ("GT.Frame", "GT.Scene.tick.components", "GT.Scene.tick.objectRange", "GT.Scene.tick.registry"), ("GT.Scene.tick.objectEntries", "GT.Scene.tick.batchElements")),
    PerfCase("tickCostSmallScene", "app", "switch", "씬 틱 병렬 분배 — 작은 씬(무버 300, 게임 대부분의 크기). 작은 그룹이 깨우는 비용을 치르는지 본다",
             ("-gv_benchMeshes=300", "-gv_benchTickMovers=1"), ("-gv_tickCostDispatch=0",), ("-gv_tickCostDispatch=1",),
             ("GT.Frame", "GT.Scene.tick.components", "GT.Scene.tick.objectRange", "GT.Scene.tick.registry"), ("GT.Scene.tick.objectEntries", "GT.Scene.tick.batchElements")),
    PerfCase("tickCostChurn", "app", "switch", "씬 틱 병렬 분배 — 스폰이 잦은 씬(무버 4000 + 프레임마다 20 개 교체, 총알 같은 패턴)",
             ("-gv_benchMeshes=4000", "-gv_benchTickMovers=1", "-gv_benchSpawnChurn=20"), ("-gv_tickCostDispatch=0",), ("-gv_tickCostDispatch=1",),
             ("GT.Frame", "GT.Scene.tick.components", "GT.Scene.tick.objectRange", "GT.Scene.tick.registry"), ("GT.Scene.tick.objectEntries", "GT.Scene.tick.batchElements")),
    PerfCase("tickCostRagdolls", "app", "switch", "씬 틱 병렬 분배 — 래그돌 켜기 · 기상이 계속 일어나는 군중(스켈레톤 100 + IK 기사 20, 60 프레임 주기)",
             ("-gv_benchRagdolls=100", "-gv_benchRig=20", "-gv_benchRagdollPeriod=60"), ("-gv_tickCostDispatch=0",), ("-gv_tickCostDispatch=1",),
             ("GT.Frame", "GT.Scene.tick.components", "GT.Scene.tick.registry", "GT.Animation.evaluate"), ("GT.Scene.tick.objectEntries",)),
    PerfCase("fractureWarm", "test", "switch", "파괴 잎 셰이프 미리 짓기 — -gv_fractureShapeWarmMicroseconds=-1(첫 프레임에 모두)이 전",
             ("--test_filter=FractureBenchTest.*",), ("-gv_fractureShapeWarmMicroseconds=-1",), ()),
    PerfCase("fractureDefer", "test", "switch", "파괴 잎 셰이프 — 0(처음 깨질 때 모두) 대 기본(나눠 미리 짓기)",
             ("--test_filter=FractureBenchTest.WallBreakFrameColdAndWarm",), ("-gv_fractureShapeWarmMicroseconds=0",), ()),
    PerfCase("physicsStep", "test", "binary", "물리 step · 질의 버퍼 재사용(8869d5996 · 09303824b)",
             ("--test_filter=PhysicsBenchTest.*",)),
    PerfCase("netHostCopy", "test", "binary", "NetHost 수신 · 순서만 채널 송신의 메시지 복사 줄이기(a108c1adf)",
             ("--test_filter=NetReplicationBenchTest.*",)),
    PerfCase("morphPool", "app", "binary", "모프 풀 준비의 메시 중복 제거를 해시 셋으로(eba2fc5a6) — 테스트 머리 64 개",
             ("-gv_benchFaces=64",), listScope=("RT.Frame", "RT.ExecutePacket")),
    PerfCase("uiPaintOrder", "app", "binary", "z 순서 패널 그리기 순서 스크래치(25557b1d9) — UI 위젯 1 만 · 글 10 칸/프레임",
             ("-gv_benchUiWidgets=10000", "-gv_benchUiChurn=10"), listScope=("GT.UI.Paint", "GT.UI.Layout", "RT.Canvas.Upload")),
    PerfCase("uiNameplates", "app", "switch", "월드 공간 UI 대량 — RTS 이름표 · HP 막대 2000 개(없음이 A). 게임 스레드 GT.UI.* 합 0.5 ms 가 기준",
             ("-gv_benchMeshes=2000",), (), ("-gv_benchUiNameplates=2000",),
             ("GT.Frame", "GT.UI.Layout", "GT.UI.Paint", "RT.Canvas.Upload", "RT.Canvas.Draw"), ("UI.LayoutWidgets", "UI.PaintWidgets", "UI.CanvasQuads")),
    PerfCase("hangulGlyphs", "app", "switch", "한글 장문 첫 프레임 — 새 음절 400 개(없음이 A). 최악 프레임(max) 4 ms 가 기준",
             ("-lang=ko", "-gv_benchMeshes=100", "-gv_benchProbeFrame=180"), (), ("-gv_benchUiHangulGlyphs=400",),
             ("GT.Frame", "GT.UI.Paint", "RT.Canvas.Upload", "RT.Frame")),
    PerfCase("deferredBloom", "app", "binary", "디퍼드 블룸의 밝은 부분을 반해상도 패스로(ee6fff232) — 큐브 2000. 전 GPU.PostBloom 대 후 GPU.BloomDownsample + GPU.PostBloom. 전 바이너리는 ee6fff232^ 의 Release 빌드",
             ("-gv_deferred=1", "-gv_benchMeshes=2000"), listScope=("GPU.Frame", "GPU.PostBloom", "GPU.BloomDownsample")),
    PerfCase("shadowPassShare", "app", "single", "그림자 패스의 몫(기준값, GraphicsMeasurements M3) — 큐브 4000. GPU.Shadow 가 GPU.Frame 의 한 자리 퍼센트면 캐스케이드별 컬링은 이득이 없다",
             ("-gv_deferred=1", "-gv_benchMeshes=4000"), listScope=("GPU.Frame", "GPU.Shadow")),
    PerfCase("hangulGlyphsCPU", "test", "single", "한글 새 글리프 400 개의 CPU 래스터화 · SDF(기준값, 시스템 한글 글꼴이 없으면 건너뜀)",
             ("--test_filter=GlyphBenchTest.*",)),
) + tuple(
    # 클러스터드 라이팅 측정(ClusteredLighting.md S3) — 디퍼드, 큐브 4000, 라이트 16 · 128 · 1024. A 는 그리드 없음(gv_lightCulling 0), B 는 CPU(1) · GPU(2) 생산자.
    # 디퍼드 조명 패스의 이름은 deferredpipeline.xml 의 "Shading" 이다(GPU.<패스 이름>). GPU 생산자의 디스패치는 컴퓨트 프리패스(GPU.Compute)에 든다.
    PerfCase(f"lightCulling{producer}{lightCount}", "app", "switch",
             f"클러스터드 라이팅 — 라이트 {lightCount}, 그리드 없음(A) 대 {producer} 생산자(B)",
             ("-gv_deferred=1", "-gv_benchMeshes=4000", f"-gv_benchLights={lightCount}"), ("-gv_lightCulling=0",), (f"-gv_lightCulling={mode}",),
             ("GPU.Frame", "GPU.Shading", "GPU.Compute", "RT.Frame"))
    for lightCount in (16, 128, 1024)
    for producer, mode in (("Cpu", 1), ("Gpu", 2))
)


@dataclass
class CaseSample:
    """한 쪽(A 또는 B)의 판별 값. 키는 '구간 열' · '벤치 이름 열'."""

    mapValue: dict[str, list[float]] = field(default_factory=dict)
    listFailure: list[str] = field(default_factory=list)

    def add(self, key: str, value: float) -> None:
        self.mapValue.setdefault(key, []).append(value)


def findTestExecutableInternal(appPath: Path) -> Path:
    """`EngineTest` 의 자리 — 빌드는 테스트 실행 파일을 App 옆(`Bin`)이 아니라 형제 `TestBin` 에 둔다."""
    binFolder = appPath.resolve().parent
    candidate = binFolder / f"EngineTest{appPath.suffix}"
    if candidate.exists():
        return candidate
    return binFolder.parent / "TestBin" / candidate.name


def collectInternal(case: PerfCase, listLine: list[str], sample: CaseSample) -> None:
    if case.kind == "test":
        for line in listLine:
            match = kBenchRowRe.search(line)
            if match is None:
                continue
            label = match.group(1).strip()
            sample.add(f"{label} | p50", float(match.group(3)))
            sample.add(f"{label} | p99", float(match.group(5)))
            sample.add(f"{label} | max", float(match.group(6)))
        return
    mapProfile = parseProfileTable(listLine)
    for scope in case.listScope:
        row = mapProfile.get(scope)
        if row is None:
            continue
        sample.add(f"{scope} | p50", float(row.p50Micro))
        sample.add(f"{scope} | p99", float(row.p99Micro))
        sample.add(f"{scope} | max", float(row.maxMicro))
    for counter in case.listCounter:
        row = mapProfile.get(counter)
        if row is not None:
            sample.add(f"{counter} | per_frame", row.perFrame)


def runSideInternal(case: PerfCase, executable: Path, listArgument: list[str], sample: CaseSample, timeoutSeconds: float) -> None:
    # TestBin 의 테스트 실행 파일은 Engine.dll 이 있는 형제 Bin 에서 돌린다(없으면 DLL 을 못 찾아 시작하자마자 죽는다).
    workingFolder = executable.resolve().parent.parent / "Bin" if executable.resolve().parent.name == "TestBin" else None
    result = runApp(executable, listArgument, cwd=workingFolder, timeoutSeconds=timeoutSeconds)
    if result.bBackendUnusable:
        sample.listFailure.append("backend unusable on this machine")
        return
    if result.bTimedOut or result.exitCode != 0:
        sample.listFailure.append(f"{executable.name} exit {result.exitCode}{' (timeout)' if result.bTimedOut else ''}")
        return
    collectInternal(case, result.listLine, sample)


def measureCaseInternal(case: PerfCase, args: argparse.Namespace) -> tuple[CaseSample, CaseSample] | str:
    """A · B 를 번갈아 잽니다. 건너뛰면 이유 문자열을 돌려줍니다."""
    appAfter: Path = args.app
    appBefore: Path | None = args.app_before
    if case.mode == "binary" and appBefore is None:
        return "--app-before 가 없다(바이너리 A/B)"
    if case.kind == "test":
        executableAfter = findTestExecutableInternal(appAfter)
        executableBefore = findTestExecutableInternal(appBefore) if case.mode == "binary" and appBefore is not None else executableAfter
        listBase = list(case.listCommon)
    else:
        executableAfter = appAfter
        executableBefore = appBefore if case.mode == "binary" and appBefore is not None else appAfter
        listBase = [*case.listCommon, f"-gv_profileFrames={args.frames}", kBackendSwitch[args.backend]]
    for executable in (executableBefore, executableAfter):
        if not executable.is_file():
            return f"{executable} 이 없다"
    sampleBefore = CaseSample()
    sampleAfter = CaseSample()
    for runIndex in range(max(1, args.runs)):
        # 판마다 순서를 바꿔 기계 상태의 흐름이 한쪽에만 실리지 않게 한다.
        listOrder = [(executableBefore, case.listBefore, sampleBefore), (executableAfter, case.listAfter, sampleAfter)]
        if case.mode == "single":
            listOrder = listOrder[1:]
        elif runIndex % 2 == 1:
            listOrder.reverse()
        for executable, listExtra, sample in listOrder:
            runSideInternal(case, executable, [*listBase, *listExtra], sample, args.timeout)
        print(f"[PerfCompare] {case.name} run {runIndex + 1}/{args.runs}", flush=True)
    return sampleBefore, sampleAfter


def formatTableInternal(listRow: list[tuple[str, str, str, str, str]]) -> str:
    listHeader = ("case", "metric (us · per_frame)", "A (before)", "B (after)", "B/A")
    listWidth = [max(len(listHeader[column]), *(len(row[column]) for row in listRow)) if listRow else len(listHeader[column]) for column in range(5)]
    listText = [" | ".join(listHeader[column].ljust(listWidth[column]) for column in range(5)),
                "-+-".join("-" * listWidth[column] for column in range(5))]
    for row in listRow:
        listText.append(" | ".join(row[column].ljust(listWidth[column]) for column in range(5)))
    return "\n".join(listText)


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="세션 성능 변경의 전/후를 번갈아 재서 표로 낸다(Release)")
    addAppRunArguments(parser, bMultipleBackends=False)
    parser.add_argument("--app-before", type=Path, default=None, help="전 커밋을 전체 빌드한 App(바이너리 A/B 케이스용)")
    parser.add_argument("--cases", nargs="+", default=None, choices=[case.name for case in kListCase], help="돌릴 케이스(기본: 전부)")
    parser.add_argument("--runs", type=int, default=3, help="쪽마다 판 수(중앙값을 쓴다)")
    parser.add_argument("--frames", type=int, default=600, help="App 케이스 한 판의 측정 프레임 수(60 프레임 워밍업 뒤)")
    parser.add_argument("--timeout", type=float, default=600.0, help="한 판의 시간 상한(초)")
    parser.add_argument("--list", action="store_true", help="케이스 목록만 보인다")
    args = parser.parse_args(listArgument)

    if args.list:
        for case in kListCase:
            print(f"{case.name:16} {case.kind:4} {case.mode:6} {case.note}")
        return ExitCode.Passed

    listSelected = [case for case in kListCase if args.cases is None or case.name in args.cases]
    listRow: list[tuple[str, str, str, str, str]] = []
    listSkipped: list[str] = []
    for case in listSelected:
        print(f"[PerfCompare] {case.name}: {case.note}", flush=True)
        measured = measureCaseInternal(case, args)
        if isinstance(measured, str):
            listSkipped.append(f"{case.name}: {measured}")
            continue
        sampleBefore, sampleAfter = measured
        for failure in [*sampleBefore.listFailure, *sampleAfter.listFailure]:
            listSkipped.append(f"{case.name}: {failure}")
        for key in sorted(set(sampleBefore.mapValue) | set(sampleAfter.mapValue)):
            listBefore = sampleBefore.mapValue.get(key, [])
            listAfter = sampleAfter.mapValue.get(key, [])
            before = statistics.median(listBefore) if listBefore else None
            after = statistics.median(listAfter) if listAfter else None
            ratio = f"{after / before:.2f}" if before and after is not None else "-"
            listRow.append((case.name, key, "-" if before is None else f"{before:.1f}", "-" if after is None else f"{after:.1f}", ratio))

    print(f"\n[PerfCompare] PC {platform.node()} | CPU {platform.processor() or platform.machine()} | backend {args.backend} | runs {args.runs} | frames {args.frames}")
    print(formatTableInternal(listRow))
    for skipped in listSkipped:
        print(f"[PerfCompare] skipped {skipped}")
    return ExitCode.Passed if listRow else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
