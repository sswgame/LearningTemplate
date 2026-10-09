#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunBuildBaseline.py
@brief 빌드 시간 기준선 — 풀 빌드 · 헤더 하나 수정 · `.cpp` 하나 수정 · 워크트리 콜드를 각 N 번 재서 중앙값 표를 낸다(docs/plans/BuildSpeed.md 0 단계).

이 저장소의 빌드 시간은 PC 마다 다르다. 그래서 표 머리에 PC 이름 · CPU · 논리 코어 수 · 커밋을 적고, 같은 PC 의 전후만 견준다.
**다른 빌드가 도는 동안 재지 않는다** — 코어를 나눠 쓰면 숫자가 오염된다. ninja · cmake 프로세스가 보이면 멈춘다(`--force` 로 무시).

| 시나리오 | 무엇을 재나 | 어디서 |
| --- | --- | --- |
| `full` | `clean` 뒤 기본 타깃 전체(sccache 없이 — 진짜 콜드) | `build/<프리셋>-Baseline`(`SW_USE_SCCACHE=OFF` 로 따로 구성) |
| `header` | 헤더 하나의 mtime 을 올린 뒤 증분 빌드(`--header`, 기본 `Core/Container/vector.h`) | 같은 폴더 |
| `cpp` | `.cpp` 하나의 mtime 을 올린 뒤 증분 빌드(`--source`) | 같은 폴더 |
| `worktree` | 새 워크트리(같은 커밋)에서 구성 + 빌드, sccache 웜 | `<워크트리 루트>/baseline-bench`, 첫 회는 데우기라 세지 않는다 |

`full` · `header` · `cpp` 는 sccache 를 끈 별도 빌드 폴더에서 재므로 개발 트리(`build/<프리셋>`)와 공유 sccache 캐시를 건드리지 않는다.
`worktree` 는 HEAD 커밋으로 워크트리를 만든다 — 커밋하지 않은 변경은 들지 않는다. 끝나면 그 워크트리를 지운다.

사용법:
    py -3 Scripts/dev/RunBuildBaseline.py                                 # Ninja-Debug, 네 시나리오 × 3 회
    py -3 Scripts/dev/RunBuildBaseline.py --preset Ninja-Debug --preset Ninja-Release --out build/BuildBaseline.md
    py -3 Scripts/dev/RunBuildBaseline.py --scenario header --scenario cpp --repeat 5
    py -3 Scripts/dev/RunBuildBaseline.py --with-tests                     # 기본 타깃에 시험(AllTests)까지
"""

from __future__ import annotations

import argparse
import datetime
import os
import platform
import shutil
import statistics
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import BuildTree, getProjectRoot, kTestBuildTargets, runGit, runProcess  # noqa: E402

kScenarioNames: tuple[str, ...] = ("full", "header", "cpp", "worktree")
kDefaultHeader = "Source/Core/Container/vector.h"
kDefaultSource = "Source/Engine/Object/GameObject/GameObjectManager.cpp"
#: 기준선을 재는 별도 빌드 폴더의 접미 — 개발 트리와 sccache 를 건드리지 않는다.
kBaselineDirSuffix = "-Baseline"
#: 워크트리 콜드를 재는 임시 워크트리 이름(`py -3 -m Scripts worktree-make`).
kWorktreeName = "baseline-bench"
#: 재기 전에 이 프로세스가 보이면 다른 빌드가 도는 것이다.
kBusyProcessNames: tuple[str, ...] = ("ninja.exe", "cmake.exe", "clang-cl.exe") if os.name == "nt" else ("ninja", "cmake")


@dataclass
class ScenarioResult:
    """프리셋 하나 × 시나리오 하나의 회차별 초."""

    preset: str
    scenario: str
    listSecond: list[float] = field(default_factory=list)

    @property
    def median(self) -> float:
        return statistics.median(self.listSecond) if self.listSecond else 0.0


def describeMachine() -> str:
    """`PC 이름 (CPU, 논리 코어 N)` — 표 머리에 적는다."""
    cpuName = platform.processor() or "알 수 없는 CPU"
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
                cpuName = str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
        except OSError:
            pass
    else:
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("model name"):
                    cpuName = line.split(":", 1)[1].strip()
                    break
        except OSError:
            pass
    return f"{platform.node()} ({cpuName}, 논리 코어 {os.cpu_count()})"


def findBusyProcesses() -> list[str]:
    """지금 도는 빌드 프로세스 이름들(있으면 재지 않는다)."""
    if os.name == "nt":
        result = runProcess(["tasklist", "/FO", "CSV", "/NH"])
        listName = [line.split(",", 1)[0].strip('"').lower() for line in result.stdout.splitlines() if line]
    else:
        result = runProcess(["ps", "-eo", "comm="])
        listName = [line.strip().lower() for line in result.stdout.splitlines()]
    return sorted({name for name in listName if name in kBusyProcessNames})


def runTimed(command: list[str], cwd: Path | None = None) -> float:
    """명령 하나를 돌려 걸린 초를 돌려준다. 지면 출력 끝을 찍고 `RuntimeError`."""
    start = time.perf_counter()
    result = runProcess(command, cwd=cwd, bMergeStderr=True)
    elapsed = time.perf_counter() - start
    if not result.bSucceeded:
        print(result.stdout[-3000:], file=sys.stderr)
        raise RuntimeError(f"실패({result.returnCode}): {' '.join(command)}")
    return elapsed


def touchFile(path: Path) -> None:
    """mtime 을 지금으로 — ninja 가 그 파일에 기대는 TU 를 다시 짓는다."""
    if not path.is_file():
        raise RuntimeError(f"파일이 없습니다: {path}")
    os.utime(path, None)


def configureBaselineTree(repositoryRoot: Path, preset: str) -> BuildTree:
    """`build/<프리셋>-Baseline` 을 sccache 없이 구성한다(이미 구성돼 있으면 다시 구성만)."""
    tree = BuildTree.fromPreset(f"{preset}{kBaselineDirSuffix}", repositoryRoot)
    runTimed(["cmake", "--preset", preset, "-B", str(tree.path), "-DSW_USE_SCCACHE=OFF"], cwd=repositoryRoot)
    return tree


def measureLocalScenarios(repositoryRoot: Path, preset: str, listScenario: list[str], args: argparse.Namespace,
                          listTarget: list[str]) -> list[ScenarioResult]:
    """full · header · cpp — sccache 없는 별도 빌드 폴더에서."""
    listResult: list[ScenarioResult] = []
    tree = configureBaselineTree(repositoryRoot, preset)
    buildCommand = tree.buildCommand(listTarget, args.jobs)
    if "full" in listScenario:
        result = ScenarioResult(preset, "full")
        for repeat in range(args.repeat):
            runTimed(tree.buildCommand(["clean"]))
            result.listSecond.append(runTimed(buildCommand))
            print(f"[Baseline] {preset} full #{repeat + 1}: {result.listSecond[-1]:.1f} s", flush=True)
        listResult.append(result)
    else:
        runTimed(buildCommand)   # 증분 시나리오는 최신 트리에서 시작한다
    for scenario, relativePath in (("header", args.header), ("cpp", args.source)):
        if scenario not in listScenario:
            continue
        result = ScenarioResult(preset, scenario)
        for repeat in range(args.repeat):
            touchFile(repositoryRoot / relativePath)
            result.listSecond.append(runTimed(buildCommand))
            print(f"[Baseline] {preset} {scenario} #{repeat + 1}: {result.listSecond[-1]:.1f} s", flush=True)
        listResult.append(result)
    return listResult


def measureWorktreeScenario(repositoryRoot: Path, preset: str, args: argparse.Namespace, listTarget: list[str]) -> ScenarioResult:
    """같은 커밋의 새 워크트리에서 구성 + 빌드(sccache 웜). 첫 회는 데우기라 세지 않는다."""
    commit = runGit(["rev-parse", "HEAD"], cwd=repositoryRoot).stdout.strip()
    worktreeRoot = repositoryRoot.parent / "LT-wt" / kWorktreeName
    scriptsCommand = [sys.executable, "-m", "Scripts"]
    if not worktreeRoot.is_dir():
        runTimed([*scriptsCommand, "worktree-make", kWorktreeName, "--base", commit], cwd=repositoryRoot)
    result = ScenarioResult(preset, "worktree")
    try:
        tree = BuildTree.fromPreset(preset, worktreeRoot)
        for repeat in range(args.repeat + 1):
            if tree.path.is_dir():
                shutil.rmtree(tree.path)
            elapsed = runTimed(["cmake", "--preset", preset], cwd=worktreeRoot)
            elapsed += runTimed(tree.buildCommand(listTarget, args.jobs))
            if repeat == 0:
                print(f"[Baseline] {preset} worktree 데우기: {elapsed:.1f} s", flush=True)
                continue
            result.listSecond.append(elapsed)
            print(f"[Baseline] {preset} worktree #{repeat}: {elapsed:.1f} s", flush=True)
    finally:
        if not args.keep_worktree:
            runProcess([*scriptsCommand, "worktree-remove", kWorktreeName, "--force"], cwd=repositoryRoot, bCapture=False)
    return result


def formatTable(listResult: list[ScenarioResult], args: argparse.Namespace, listTarget: list[str], repositoryRoot: Path) -> str:
    commit = runGit(["rev-parse", "--short", "HEAD"], cwd=repositoryRoot).stdout.strip()
    today = datetime.date.today().isoformat()
    listLine = [
        f"# 빌드 기준선 — {describeMachine()}",
        "",
        f"{today} · 커밋 `{commit}` · 타깃 `{' '.join(listTarget)}` · 회차 {args.repeat} · 헤더 `{args.header}` · 소스 `{args.source}`",
        "",
        "| 프리셋 | 시나리오 | " + " | ".join(f"{index + 1}회(s)" for index in range(args.repeat)) + " | 중앙값(s) |",
        "|---|---|" + "---:|" * (args.repeat + 1),
    ]
    for result in listResult:
        cells = [f"{second:.1f}" for second in result.listSecond] + [""] * (args.repeat - len(result.listSecond))
        listLine.append(f"| {result.preset} | {result.scenario} | " + " | ".join(cells) + f" | **{result.median:.1f}** |")
    return "\n".join(listLine) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="빌드 시간 기준선(풀 · 헤더 수정 · .cpp 수정 · 워크트리 콜드) 중앙값 표")
    parser.add_argument("--preset", action="append", default=None, help="configure 프리셋(여러 번, 기본 Ninja-Debug)")
    parser.add_argument("--scenario", action="append", choices=kScenarioNames, default=None, help="잴 시나리오(여러 번, 기본 전부)")
    parser.add_argument("--repeat", type=int, default=3, help="시나리오마다 잴 횟수(기본 3 — 중앙값)")
    parser.add_argument("--jobs", type=int, default=0, help="빌드 병렬 수(0 이면 ninja 기본)")
    parser.add_argument("--header", default=kDefaultHeader, help=f"header 시나리오가 건드릴 헤더(기본 {kDefaultHeader})")
    parser.add_argument("--source", default=kDefaultSource, help=f"cpp 시나리오가 건드릴 소스(기본 {kDefaultSource})")
    parser.add_argument("--with-tests", action="store_true", help="기본 타깃(all)에 시험(AllTests)까지 짓는다")
    parser.add_argument("--keep-worktree", action="store_true", help="worktree 시나리오의 워크트리를 지우지 않는다")
    parser.add_argument("--force", action="store_true", help="다른 빌드 프로세스가 보여도 잰다(숫자가 오염된다)")
    parser.add_argument("--out", type=Path, default=None, help="표를 남길 파일(Markdown)")
    args = parser.parse_args(argv)

    repositoryRoot = getProjectRoot()
    listPreset = args.preset or ["Ninja-Debug"]
    listScenario = args.scenario or list(kScenarioNames)
    listTarget = list(kTestBuildTargets) if args.with_tests else ["all"]
    if args.repeat < 1:
        parser.error("--repeat 는 1 이상")

    listBusy = findBusyProcesses()
    if listBusy and not args.force:
        print(f"[Baseline] 다른 빌드가 돌고 있습니다({', '.join(listBusy)}) — 끝난 뒤 재거나 --force", file=sys.stderr)
        return 2

    listResult: list[ScenarioResult] = []
    try:
        for preset in listPreset:
            if any(name in listScenario for name in ("full", "header", "cpp")):
                listResult += measureLocalScenarios(repositoryRoot, preset, listScenario, args, listTarget)
            if "worktree" in listScenario:
                listResult.append(measureWorktreeScenario(repositoryRoot, preset, args, listTarget))
    except RuntimeError as error:
        print(f"[Baseline] {error}", file=sys.stderr)
        return 1

    text = formatTable(listResult, args, listTarget, repositoryRoot)
    print(text)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
        print(f"[Baseline] {args.out} 에 남겼습니다")
    return 0


if __name__ == "__main__":
    sys.exit(main())
