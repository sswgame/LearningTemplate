#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
보고서 하나 = 클래스 하나. `report/` 의 스크립트는 찍어 줄 뿐이고(종료 코드 0 — `--fail-on` 을 명시한 것만 예외) 껍데기는 같다:
저장소 루트(`--root`, 기본은 이 저장소 — 작업 폴더가 아니다), 빌드 폴더(`--preset` · `--build-dir`), 병렬 수(`--jobs`, 기본은 `common.Parallel` 의 정책),
경로 거르기(`--filter`), 결과 파일(`--out`). 보고서는 `produce()` 하나를 쓴다.

    class RunSomethingReport(LintReport):
        description = "무엇을 보고하는가"
        bUsesBuildTree = True        # --preset / --build-dir
        bUsesJobs = True             # --jobs
        def addArguments(self, parser): parser.add_argument("--detail", action="store_true")
        def produce(self, context, args) -> int:
            for entry in context.buildTree.readCompileDatabase(): ...
            return 0

    main = RunSomethingReport.run

게이트(`LintGate`) · 픽서(`LintFixer`)와 같은 자리다 — `findReportClass` 가 모듈에서 보고서 클래스를 찾는다(`selftest/CheckReportsRun.py`).
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import BuildTree, BuildTreeError, addBuildTreeArguments, getProcessWorkerCount, getProjectRoot  # noqa: E402


@dataclass
class ReportContext:
    """보고서 한 번에 기반이 풀어 주는 것."""

    repositoryRoot: Path
    buildTree: BuildTree | None = None
    listBuildTree: list[BuildTree] = field(default_factory=list)   # bMultiplePresets 일 때 프리셋마다
    jobs: int = 1


class LintReport:
    """
    - `description`      : `--help` 한 줄.
    - `bUsesBuildTree`   : `--preset` · `--build-dir` 를 받는다. `defaultPreset` 이 기본(None 이면 `.clangd` 가 가리키는 트리).
    - `bMultiplePresets` : `--preset` 을 여러 번(구성마다 도는 보고서). 주지 않으면 `listDefaultPreset`.
    - `bUsesJobs`        : `--jobs`(기본 `getProcessWorkerCount()` — 자식 프로세스를 띄우는 보고서의 정책 한 자리).
    - `bUsesFilter`      : `--filter <경로 부분 문자열>`.
    - `bUsesOut`         : `--out <파일>`(원본 · 표를 남긴다).
    """

    name: str = ""
    description: str = ""
    bUsesBuildTree: bool = False
    bMultiplePresets: bool = False
    defaultPreset: str | None = "Ninja-Debug"
    listDefaultPreset: tuple[str, ...] = ()
    bUsesJobs: bool = False
    bUsesFilter: bool = False
    bUsesOut: bool = False

    def __init_subclass__(cls, **kwargs) -> None:
        super().__init_subclass__(**kwargs)
        if not cls.name:
            cls.name = cls.__name__.removesuffix("Report")

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        """기반 인자 말고 더 받을 것."""

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        raise NotImplementedError

    @classmethod
    def run(cls, argv: list[str] | None = None) -> int:
        """진입점. 각 보고서 모듈은 `main = XxxReport.run` 한 줄로 내보낸다."""
        return cls().main(argv)

    def main(self, argv: list[str] | None = None) -> int:
        parser = argparse.ArgumentParser(description=self.description)
        parser.add_argument("--root", type=Path, default=None, help="저장소 루트(기본: 이 저장소)")
        if self.bUsesBuildTree:
            addBuildTreeArguments(parser, defaultPreset=self.defaultPreset, bMultiple=self.bMultiplePresets)
        if self.bUsesJobs:
            parser.add_argument("--jobs", type=int, default=0, help="동시 실행 수(0 이면 common.Parallel 의 정책)")
        if self.bUsesFilter:
            parser.add_argument("--filter", default="", help="경로에 이 문자열이 든 것만")
        if self.bUsesOut:
            parser.add_argument("--out", type=Path, default=None, help="결과를 남길 파일")
        self.addArguments(parser)
        args = parser.parse_args(argv)

        context = ReportContext(repositoryRoot=(args.root or getProjectRoot()).resolve())
        if self.bUsesBuildTree:
            if self.bMultiplePresets and not args.build_dir:
                listPreset = args.preset or list(self.listDefaultPreset)
                context.listBuildTree = [BuildTree.fromPreset(name, context.repositoryRoot) for name in listPreset]
            else:
                context.buildTree = BuildTree.fromArguments(args, context.repositoryRoot)
                context.listBuildTree = [context.buildTree]
        if self.bUsesJobs:
            context.jobs = args.jobs or getProcessWorkerCount()
        try:
            return self.produce(context, args)
        except BuildTreeError as error:
            print(f"[{self.name}] {error}", file=sys.stderr)
            return 2


def findReportClass(module) -> type[LintReport] | None:
    """모듈 안에 정의된 보고서 클래스(한 파일에 하나) — `findGateClass` · `findFixerClass` 의 짝."""
    for value in vars(module).values():
        if isinstance(value, type) and issubclass(value, LintReport) and value is not LintReport and value.__module__ == module.__name__:
            return value
    return None
