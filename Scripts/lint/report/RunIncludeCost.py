#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
헤더마다 **모든 TU 에서 파싱하는 데 쓴 시간의 합**을 잰다 — 헤더 다이어트(docs/plans/BuildSpeed.md 2 단계)가 어디부터 줄일지 고르는 표.

[무엇을 재나]
컴파일 DB 의 TU 마다 진짜 빌드 플래그로 `-fsyntax-only -ftime-trace` 를 한 번 돌리고(PCH 는 뗀다 — 두면 PCH 에 든 헤더가 0 으로 보인다),
trace 의 `Source` 구간(헤더 하나를 읽고 파싱한 구간, 그 헤더가 include 한 것까지 포함)을 헤더별로 더한다. 같은 소스가 여러 타깃에 있으면
타깃마다 센다 — 빌드도 그만큼 다시 파싱한다. 머리 줄의 "헤더 파싱 비율" 은 `Total Source` / `Total ExecuteCompiler` 의 합이다
(구문 검사만 돌리므로 코드 생성 · 최적화 시간은 들지 않는다 — 진짜 빌드의 비율보다 높게 나온다).

[읽는 법]
- 맨 위에는 `pch.h` · `CoreMinimal.h` 처럼 모든 TU 가 읽는 묶음 헤더가 온다 — PCH 를 뗀 비용이다. 줄일 대상은 그 아래 개별 헤더다.
- 누적(s): 그 헤더 구간의 합(포함 시간). 부모 헤더는 자식을 포함하므로 표의 합은 전체보다 크다.
- TU: 그 헤더를 읽은 TU 수. 평균(ms) = 누적 / TU — 평균이 크면 헤더 자체가 무겁고, TU 가 많으면 퍼진 범위가 넓다.
- `--granularity-us` 보다 짧은 구간은 trace 가 버린다. 상위 헤더는 길어서 영향이 없지만, 작은 헤더는 덜 잡힌다.

[언제 쓰나]
빌드를 한 번 지은 트리에서(코드젠 산출물이 있어야 한다 — `RunHeaderSelfContained` 와 같은 조건), 다른 빌드가 돌지 않을 때.
TU 가 2 천여 개라 16 스레드에서 수 분이다. `--filter` 로 폴더 하나만 볼 수 있다.

사용법:
  py -3 Scripts/lint/report/RunIncludeCost.py                          # Ninja-Debug, 상위 20
  py -3 Scripts/lint/report/RunIncludeCost.py --top 40 --filter Source/Engine
  py -3 Scripts/lint/report/RunIncludeCost.py --out build/IncludeCost.md --keep-traces build/IncludeCostTraces
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import TranslationUnitSweep, mapConcurrent  # noqa: E402
from common.HeaderSelfContained import findHeaderProbeProblem, runSyntaxOnly  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

#: 헤더 하나를 읽는 구간 — `args.detail` 이 그 파일 경로다.
_kSourceEventName = "Source"
#: trace 끝의 합계 사건 — 헤더 파싱 전체와 컴파일 전체.
_kTotalSourceName = "Total Source"
_kTotalCompilerName = "Total ExecuteCompiler"


@dataclass
class HeaderCost:
    """헤더 하나의 누적."""

    totalMicroseconds: int = 0
    setUnit: set[str] = field(default_factory=set)


@dataclass
class TraceSummary:
    """trace 하나에서 뽑은 것 — 헤더별 포함 시간과 합계 둘."""

    mapHeaderMicroseconds: dict[str, int] = field(default_factory=dict)
    sourceMicroseconds: int = 0
    compilerMicroseconds: int = 0


def readTraceSummary(tracePath: Path) -> TraceSummary:
    """`-ftime-trace` 의 JSON 에서 `Source` 구간을 헤더별로 더하고 합계 사건을 읽는다(읽지 못하면 빈 요약)."""
    summary = TraceSummary()
    try:
        data = json.loads(tracePath.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError):
        return summary
    # `Source` 는 비동기 사건 쌍(`ph` 가 `b` → `e`)으로 적힌다 — clang 은 구간 하나를 끝낼 때 두 줄을 이어 쓰므로 바로 다음 `e` 가 짝이다.
    pendingDetail = ""
    pendingStart = 0
    for event in data.get("traceEvents", []):
        name = event.get("name")
        phase = event.get("ph")
        if name == _kSourceEventName:
            if phase == "b":
                pendingDetail = str(event.get("args", {}).get("detail", "")).replace("\\", "/")
                pendingStart = int(event.get("ts", 0) or 0)
                continue
            duration = int(event.get("dur", 0) or 0)
            detail = str(event.get("args", {}).get("detail", "")).replace("\\", "/")
            if phase == "e":
                duration = int(event.get("ts", 0) or 0) - pendingStart
                detail = pendingDetail
                pendingDetail = ""
            if detail:
                summary.mapHeaderMicroseconds[detail] = summary.mapHeaderMicroseconds.get(detail, 0) + max(duration, 0)
            continue
        duration = int(event.get("dur", 0) or 0)
        if name == _kTotalSourceName:
            summary.sourceMicroseconds = duration
        elif name == _kTotalCompilerName:
            summary.compilerMicroseconds = duration
    return summary


def makeDisplayPath(headerPath: str, listRoot: list[Path]) -> str:
    """소스 트리 안이면 그 기준, vcpkg 설치 트리면 `vcpkg/…`, MSVC 헤더면 `MSVC/…`, 나머지는 그대로."""
    normalized = headerPath.replace("\\", "/")
    for root in listRoot:
        rootText = root.as_posix().rstrip("/") + "/"
        if normalized.lower().startswith(rootText.lower()):
            normalized = normalized[len(rootText):]
            break
    for marker, label in (("vcpkg_installed/", "vcpkg/"), ("/VC/Tools/MSVC/", "MSVC/")):
        index = normalized.lower().find(marker.lower())
        if index >= 0:
            # 트리플릿 · 판 폴더 하나를 건너뛴다(`x64-windows/…` · `14.51.36231/…`).
            normalized = label + normalized[index + len(marker):].split("/", 1)[-1]
            break
    return normalized


class RunIncludeCostReport(LintReport):
    description = "전 TU 를 -ftime-trace 로 구문 검사해 헤더별 누적 파싱 시간 상위를 보고한다 (게이트 아님)."
    bUsesBuildTree = True
    bUsesJobs = True
    bUsesFilter = True
    bUsesOut = True

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--top", type=int, default=20, help="찍을 헤더 수(기본 20)")
        parser.add_argument("--granularity-us", type=int, default=100, help="trace 가 남기는 가장 짧은 구간(마이크로초, 기본 100)")
        parser.add_argument("--keep-traces", type=Path, default=None, help="TU 마다의 trace JSON 을 이 폴더에 남긴다(기본은 임시 폴더를 지운다)")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        tree = context.buildTree
        sweep = TranslationUnitSweep(tree, tag=self.name)
        if not sweep.bHasDatabase:
            sweep.reportMissingDatabase()
            return 0
        problem = findHeaderProbeProblem(tree.path)
        if problem:
            print(f"[{self.name}] {problem}", file=sys.stderr)
            return 0

        listEntry = sweep.selectUnits(args.filter)
        if not listEntry:
            print(f"[{self.name}] 잴 TU 가 없습니다.")
            return 0

        if args.keep_traces:
            args.keep_traces.mkdir(parents=True, exist_ok=True)
            return self.measure(context, args, sweep, listEntry, args.keep_traces.resolve())
        with tempfile.TemporaryDirectory(prefix="sw_include_cost_") as traceDir:
            return self.measure(context, args, sweep, listEntry, Path(traceDir))

    def measure(self, context: ReportContext, args: argparse.Namespace, sweep: TranslationUnitSweep, listEntry: list[dict], traceDir: Path) -> int:
        tree = context.buildTree
        print(f"[{self.name}] {tree.name}: TU {len(listEntry)} 개를 -ftime-trace 로 구문 검사합니다 (jobs {context.jobs})", flush=True)

        def measureUnitInternal(indexedEntry: tuple[int, dict]) -> tuple[str, TraceSummary, str | None]:
            index, entry = indexedEntry
            sourcePath = Path(str(entry.get("file", "")))
            if not sourcePath.is_absolute():
                sourcePath = Path(str(entry.get("directory", ""))) / sourcePath
            tracePath = traceDir / f"{index:05d}_{sourcePath.stem}.json"
            # 드라이버는 `-fsyntax-only` 에 `-ftime-trace` 를 넘기지 않는다(출력 파일이 없는 작업) — cc1 에 바로 준다(clang · clang-cl 공통).
            listFlag = ["-Xclang", f"-ftime-trace=\"{tracePath}\"", "-Xclang", f"-ftime-trace-granularity={args.granularity_us}"]
            failure = runSyntaxOnly(entry, sourcePath, Path(str(entry.get("directory", tree.path))), listFlag)
            return sourcePath.as_posix(), readTraceSummary(tracePath), failure

        # 결과가 문자열이 아니라 요약이라 `sweep.run`(출력 이어 붙이기) 대신 동시 실행만 빌려 쓴다.
        listResult = list(mapConcurrent(measureUnitInternal, list(enumerate(listEntry)), workerCount=context.jobs,
                                        onProgress=lambda done, total: print(f"  ... {done}/{total}") if done % 200 == 0 else None))

        mapCost: dict[str, HeaderCost] = defaultdict(HeaderCost)
        sourceMicroseconds = 0
        compilerMicroseconds = 0
        listFailure: list[str] = []
        for unitPath, summary, failure in listResult:
            if failure:
                listFailure.append(f"{unitPath}: {failure}")
            sourceMicroseconds += summary.sourceMicroseconds
            compilerMicroseconds += summary.compilerMicroseconds
            for headerPath, microseconds in summary.mapHeaderMicroseconds.items():
                cost = mapCost[headerPath]
                cost.totalMicroseconds += microseconds
                cost.setUnit.add(unitPath)

        listRoot = [context.repositoryRoot]
        sourceRoot = tree.readCacheValue("CMAKE_HOME_DIRECTORY")
        if sourceRoot:
            listRoot.append(Path(sourceRoot))
        listRanked = sorted(mapCost.items(), key=lambda item: item[1].totalMicroseconds, reverse=True)[:args.top]
        ratio = (sourceMicroseconds / compilerMicroseconds * 100.0) if compilerMicroseconds else 0.0
        listLine = [
            f"# 헤더 누적 파싱 시간 — {tree.name}, TU {len(listEntry)} 개(PCH 없이 구문 검사)",
            "",
            f"헤더 파싱 합 {sourceMicroseconds / 1e6:.1f} s / 컴파일러 합 {compilerMicroseconds / 1e6:.1f} s = {ratio:.0f} %",
            "",
            "| 순위 | 누적(s) | TU | 평균(ms) | 헤더 |",
            "|---:|---:|---:|---:|---|",
        ]
        for rank, (headerPath, cost) in enumerate(listRanked, start=1):
            unitCount = len(cost.setUnit)
            averageMs = cost.totalMicroseconds / unitCount / 1000.0 if unitCount else 0.0
            listLine.append(f"| {rank} | {cost.totalMicroseconds / 1e6:.1f} | {unitCount} | {averageMs:.1f} | "
                            f"`{makeDisplayPath(headerPath, listRoot)}` |")
        if listFailure:
            listLine += ["", f"구문 검사가 진 TU {len(listFailure)} 개(그 trace 는 빠졌거나 일부다):"]
            listLine += [f"- {line}" for line in listFailure[:20]]
        if sweep.listMissingFile:
            listLine += ["", f"컴파일 DB 에 있으나 디스크에 없는 TU {len(sweep.listMissingFile)} 개를 건너뛰었다(낡은 DB)."]

        text = "\n".join(listLine) + "\n"
        print(text)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(text, encoding="utf-8")
            print(f"[{self.name}] {args.out} 에 남겼습니다")
        return 0


main = RunIncludeCostReport.run


if __name__ == "__main__":
    sys.exit(main())
