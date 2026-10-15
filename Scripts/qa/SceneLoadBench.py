#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
씬 로드 측정 하니스 — 큰 씬을 만들어, 측정 창 안에서 비동기로 열고(로드 중 최악 프레임) 원본 XML 과 쿠킹본을 견준다.

    py -3 -m Scripts scene-load-bench --app build/Ninja-Release/Bin/App.exe                    # 엔티티 4000, XML 3 판 → 쿠킹 → 쿠킹본 3 판
    py -3 -m Scripts scene-load-bench --app build/Ninja-Release/Bin/App.exe --count 8000 --runs 5 --asset-report

무엇을 재는가(백로그 1-8 "직렬화기와 리소스 로드의 리플렉션 비용, 비동기 씬 로드 중 최악 프레임"):
  - 로드 자체: 로그 `'Scene.load.parse': N ms`(문서 읽기 — XML 이면 이름 대조 · 텍스트 파싱, 쿠킹본이면 바이너리)와
    `'Scene.load.instantiate': N ms`(오브젝트 · 컴포넌트 생성과 리플렉션 적용).
  - 로드 중 프레임: Empty 벤치 탐침 `-gv_benchSceneLoadPath` 가 `-gv_benchProbeFrame`(기본 180, 워밍업 60 뒤)에 비동기 로드를 걸고,
    표의 `GT.Frame` · `RT.Frame` p50 · p99 · max 를 읽는다. 활성 씬을 바꾸는 프레임이 max 에 들어간다.
  - `--asset-report` 면 `-gv_assetLoadReport=1` 을 더해 종료 때 에셋 로드 표를 남긴다(리소스 로드의 리플렉션 몫).

Dev 빌드는 `Cooked/` 를 마운트하지 않는다(docs/08_Verification.md 2 절). 그래서 쿠킹본은 `App --cook-scenes --cooked-dir=<repo>/Resource` 로
원본 옆에 쿠킹하고 재며, 끝나면 이 스크립트가 **새로 생긴 파일만** 지운다(이미 있던 파일을 덮어쓴 것은 지우지 않고 알린다).
쿠킹본을 읽었는지는 `[SceneDocument] Loaded … from binary <경로>.scene.bin` 줄로 가린다 — 표 아래 source 줄이 `binary` 이어야 한다
(`[SceneLoad]` 줄은 쿠킹본이어도 논리 경로 `.scene.xml` 을 찍으므로 판정에 쓰지 않는다).
만든 씬(`Resource/game/empty/maps/bench_stress<N>.scene.xml`)도 끝나면 지운다(`--keep` 이면 둔다). 커밋하지 않는다.
쿠킹 상태가 파일 시스템 전체라 XML 과 쿠킹본을 판마다 번갈아 잴 수 없다 — 같은 실행 안에서 XML 판들을 먼저, 쿠킹본 판들을 뒤에 잰다.
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import getProjectRoot  # noqa: E402
from common.AppRun import addAppRunArguments, kBackendSwitch, parseProfileTable, runApp  # noqa: E402
from common.Process import runProcess  # noqa: E402

_kTimerRe = re.compile(r"'(Scene\.load\.(?:parse|instantiate))': ([0-9.]+) ms")
_kSceneLoadedRe = re.compile(r"\[SceneDocument\].*Loaded '[^']*' \(\d+ entities\) from (binary )?(\S+)")
_kTrackedScope: tuple[str, ...] = ("GT.Frame", "RT.Frame")


def collectFilesInternal(root: Path) -> set[Path]:
    return {path for path in root.rglob("*") if path.is_file()}


def measureVariantInternal(args: argparse.Namespace, sceneResourcePath: str, runCount: int) -> tuple[dict[str, list[float]], set[str], list[str]]:
    """판마다 로드 시간과 프레임 표를 모읍니다. (값 목록, 읽은 경로들, 실패)."""
    mapValue: dict[str, list[float]] = {}
    uniqueSource: set[str] = set()
    listFailure: list[str] = []
    listArgument = [f"-gv_benchMeshes={args.bench_meshes}", f"-gv_benchSceneLoadPath={sceneResourcePath}", f"-gv_benchProbeFrame={args.probe_frame}",
                    f"-gv_profileFrames={args.frames}", kBackendSwitch[args.backend]]
    if args.asset_report:
        listArgument.append("-gv_assetLoadReport=1")
    for runIndex in range(runCount):
        result = runApp(args.app, listArgument, timeoutSeconds=args.timeout)
        if result.bTimedOut or result.exitCode != 0:
            listFailure.append(f"run {runIndex + 1}: exit {result.exitCode}{' (timeout)' if result.bTimedOut else ''}")
            continue
        for line in result.listLine:
            timer = _kTimerRe.search(line)
            if timer is not None:
                mapValue.setdefault(f"{timer.group(1)} ms", []).append(float(timer.group(2)))
            loaded = _kSceneLoadedRe.search(line)
            if loaded is not None:
                uniqueSource.add(("binary " if loaded.group(1) else "xml ") + loaded.group(2))
            if args.asset_report and "[AssetLoad" in line:
                print(f"    {line}")
        mapProfile = parseProfileTable(result.listLine)
        for scope in _kTrackedScope:
            row = mapProfile.get(scope)
            if row is None:
                continue
            mapValue.setdefault(f"{scope} p50 us", []).append(float(row.p50Micro))
            mapValue.setdefault(f"{scope} p99 us", []).append(float(row.p99Micro))
            mapValue.setdefault(f"{scope} max us", []).append(float(row.maxMicro))
        print(f"[SceneLoadBench] run {runIndex + 1}/{runCount} done", flush=True)
    return mapValue, uniqueSource, listFailure


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="큰 씬의 비동기 로드 — 로드 시간과 로드 중 최악 프레임을 XML 과 쿠킹본으로 견준다(Release, Empty 게임)")
    addAppRunArguments(parser, bMultipleBackends=False)
    parser.add_argument("--count", type=int, default=4000, help="만들 씬의 메시 엔티티 수(MakeStressScene)")
    parser.add_argument("--runs", type=int, default=3, help="변형마다 판 수(중앙값)")
    parser.add_argument("--frames", type=int, default=600, help="한 판의 측정 프레임 수(60 프레임 워밍업 뒤)")
    parser.add_argument("--probe-frame", type=int, default=180, help="비동기 로드를 거는 벤치 프레임")
    parser.add_argument("--bench-meshes", type=int, default=100, help="로드 전에 도는 벤치 큐브 수(벤치를 켜는 스위치이기도 하다)")
    parser.add_argument("--timeout", type=float, default=600.0, help="한 판의 시간 상한(초)")
    parser.add_argument("--skip-cooked", action="store_true", help="쿠킹본 변형을 재지 않는다")
    parser.add_argument("--asset-report", action="store_true", help="-gv_assetLoadReport=1 의 에셋 로드 표를 함께 찍는다")
    parser.add_argument("--keep", action="store_true", help="만든 씬과 쿠킹 산출물을 지우지 않는다")
    args = parser.parse_args(listArgument)

    repositoryRoot = getProjectRoot()
    resourceRoot = repositoryRoot / "Resource"
    sceneResourcePath = f"game/empty/maps/bench_stress{args.count}.scene.xml"
    scenePath = resourceRoot / sceneResourcePath
    bSceneExisted = scenePath.is_file()
    made = runProcess([sys.executable, repositoryRoot / "Scripts/dev/MakeStressScene.py", "--count", str(args.count), "--out", scenePath], cwd=repositoryRoot)
    if made.returnCode != 0:
        print(f"[SceneLoadBench] FAIL MakeStressScene: {made.stdout}{made.stderr}", file=sys.stderr)
        return 1

    mapResult: dict[str, tuple[dict[str, list[float]], set[str], list[str]]] = {}
    listCreated: list[Path] = []
    try:
        mapResult["xml"] = measureVariantInternal(args, sceneResourcePath, args.runs)
        if not args.skip_cooked:
            uniqueBefore = collectFilesInternal(resourceRoot)
            cooked = runApp(args.app, ["--cook-scenes", f"--cooked-dir={resourceRoot.as_posix()}"], timeoutSeconds=args.timeout)
            listCreated = sorted(collectFilesInternal(resourceRoot) - uniqueBefore)
            if cooked.exitCode != 0:
                print(f"[SceneLoadBench] cook failed (exit {cooked.exitCode}) — cooked variant skipped", flush=True)
            else:
                mapResult["cooked"] = measureVariantInternal(args, sceneResourcePath, args.runs)
    finally:
        if not args.keep:
            for path in listCreated:
                path.unlink(missing_ok=True)
            if not bSceneExisted:
                scenePath.unlink(missing_ok=True)
        changed = runProcess(["git", "status", "--porcelain", "--untracked-files=no", "--", "Resource"], cwd=repositoryRoot)
        if not changed.bSucceeded:
            raise RuntimeError(f"[SceneLoadBench] git status 가 실패했습니다: {changed.stderr.strip()}")
        if changed.stdout.strip():
            print("[SceneLoadBench] note: cooking rewrote tracked files under Resource — check `git status`:\n" + changed.stdout, flush=True)

    print(f"\n[SceneLoadBench] scene {sceneResourcePath} ({args.count} entities) | backend {args.backend} | runs {args.runs}")
    listKey = sorted({key for values, _, _ in mapResult.values() for key in values})
    listVariant = list(mapResult)
    widthKey = max([len("metric"), *(len(key) for key in listKey)])
    print("metric".ljust(widthKey) + "".join(f" | {variant:>10}" for variant in listVariant))
    for key in listKey:
        listCell = []
        for variant in listVariant:
            values = mapResult[variant][0].get(key, [])
            listCell.append(f"{statistics.median(values):>10.1f}" if values else f"{'-':>10}")
        print(key.ljust(widthKey) + "".join(f" | {cell}" for cell in listCell))
    for variant, (_, uniqueSource, listFailure) in mapResult.items():
        print(f"[SceneLoadBench] {variant} source: {', '.join(sorted(uniqueSource)) or '-'}")
        for failure in listFailure:
            print(f"[SceneLoadBench] {variant} {failure}")
    return 0 if mapResult.get("xml", ({}, set(), []))[0] else 1


if __name__ == "__main__":
    sys.exit(main())
