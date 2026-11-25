"""
Scripts/dev/preflight_run.py

묶음 검증의 실행기 — 차례 · 겹침 · 통과 기록 · 실패 처리만 안다. 무엇을 도는지는 `preflight_stages.py` 의 단계 클래스가 적는다(언리얼 BuildGraph 의 노드,
Buildkite 의 step 과 같은 자리).

- `PreflightShell` : 명령 하나를 돌리는 자리(`--dry-run` 은 찍기만) — 빌드 · ctest · 로그 요약처럼 단계가 함께 쓰는 모양.
- `Stage` · `BackgroundStage` · `Job` · `BuildInput` : 단계의 형식. 단계 하나 = 클래스 하나, 차례는 파일에 적은 차례다.
- `PreflightRun` : 단계를 차례로 돌고, 갈래를 겹치고 합류하고, 통과 · 시간을 적고, 지면 끝 줄을 찍고 멈춘다.
"""

from __future__ import annotations

import abc
import os
import re
import sys
import time
import traceback
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from common.exit_code import ExitCode
from common.git import run_git_or_raise
from common.run_process import run_process

SCRIPTS = ("py", "-3", "-m", "Scripts")
NINJA_DENIED_TEXT = "failed recompaction: Permission denied"
RESULT_LINE = re.compile(r"(?:TEST_RESULT=|SCENARIO_RESULT=|per-backend failure: ).*")
PASSED_RESULT_LINE = re.compile(r"(?:TEST_RESULT=PASSED|SCENARIO_RESULT=(?:PASSED|SKIPPED)) ")
MAX_SHOWN_LINES = 30
MAX_RESULT_LINES = 40


class StageFailed(Exception):
    """단계가 졌다 — 글은 끝 줄의 괄호 안 까닭이다."""


class RunStopped(Exception):
    """검증을 멈춘다 — 글은 찍을 끝 줄이다."""


def make_windows_failure(reason: str, sha: str, suffix: str = "") -> str:
    return f"WINDOWS_FAILED ({reason}){suffix} - Linux 를 돌리지 않는다 sha={sha}"


def list_tree_changes(root: Path) -> list[str]:
    """추적 파일의 고친 것 · 무시되지 않은 새 파일 — 있으면 sha 가 아닌 트리를 검증하는 것이다."""
    return run_git_or_raise(["status", "--porcelain", "--untracked-files=normal"], cwd=root).splitlines()


def read_lines(path: Path) -> list[str]:
    return path.read_bytes().decode("utf-8", "replace").splitlines() if path.is_file() else []


def collect_results(list_path: Iterable[Path]) -> list[str]:
    """
    판정 요약 — 실행기가 판정 값 하나에서 찍은 끝 줄만 모은다: 단위 실행의 TEST_RESULT=, 시나리오 부모의 SCENARIO_RESULT=, 진 단계를 (단계 · 종류 · 까닭)으로 접어
    백엔드를 붙인 per-backend failure: 줄. 통과 · 건너뜀 끝 줄은 뺀다. 로그 문구를 해석하지 않는다.
    """
    set_line = {match.group() for path in list_path for line in read_lines(path) for match in RESULT_LINE.finditer(line)}
    return sorted(line for line in set_line if not PASSED_RESULT_LINE.match(line))[:MAX_RESULT_LINES]


@dataclass(frozen=True)
class PreflightOptions:
    start: str           # --from
    ctest_jobs: int
    dry_run: bool = False
    no_linux: bool = False
    record: bool = False   # --record


class PreflightShell:
    """명령을 돌리고 로그에서 볼 줄을 찍는다 — 갈래(스레드)도 부르므로 상태를 두지 않는다."""

    def __init__(self, root: Path, sha: str, options: PreflightOptions, quarantine_exclude: str) -> None:
        self.root = root
        self.sha = sha
        self.options = options
        self.quarantine_exclude = quarantine_exclude

    def spawn(self, command: Sequence[str], log: Path | None, cwd: Path, environment: dict[str, str] | None) -> int:
        """자식 하나 — 테스트는 이것만 갈아 끼운다."""
        program = str(self.root / command[0]) if "/" in command[0] else command[0]   # Windows 는 상대 경로 실행 파일을 cwd 가 아니라 부모의 폴더에서 찾는다
        return run_process([program, *command[1:]], cwd=cwd, capture=False, log=log, environment=environment).return_code

    def write_line(self, text: str) -> None:
        sys.stdout.write(text + "\n")   # 줄 하나를 한 번에 — 갈래끼리 줄 중간에서 섞이지 않게
        sys.stdout.flush()

    def announce(self, title: str) -> None:
        self.write_line(f"== {title} {time.strftime('%H:%M:%S')}")

    def run(self, command: Sequence[str], log: str | None = None, *, folder: str = "", utf8: bool = False) -> int:
        """명령 하나 — `log` 는 출력 파일(`folder` 기준, 없으면 화면). --dry-run 이면 찍기만 하고 통과로 친다."""
        if self.options.dry_run:
            self.write_line(f"[dry] {' '.join(command)}{f' > {log}' if log else ''}")
            return 0
        cwd = self.root / folder
        return self.spawn(command, cwd / log if log else None, cwd, {**os.environ, "PYTHONUTF8": "1"} if utf8 else None)

    def run_record_command(self, *arguments: str) -> bool:
        """기록 · 판정 명령(`py -3 -m Scripts …`) — --dry-run 도 돈다(적는 것은 부르는 쪽이 뺀다)."""
        return self.spawn([*SCRIPTS, *arguments], None, self.root, None) == 0

    def show_tail(self, log: str, count: int) -> None:
        for line in ([] if self.options.dry_run else read_lines(self.root / log)[-count:]):
            self.write_line(line)

    def show_matches(self, log: str, pattern: str, limit: int | None = None) -> None:
        if not self.options.dry_run:
            for line in [line for line in read_lines(self.root / log) if re.search(pattern, line)][:limit]:
                self.write_line(line)

    def run_logged(self, title: str, command: Sequence[str], log: str, tail: int, *, exit_name: str = "") -> int:
        """머리 줄 → 명령 → 로그 끝 `tail` 줄 → (`exit_name` 이 있으면) 종료 코드 줄."""
        self.announce(title)
        code = self.run(command, log)
        self.show_tail(log, tail)
        if exit_name:
            self.write_line(f"{exit_name} exit={code}")
        return code

    def build(self, preset: str) -> None:
        logs = f"build/{preset}/Testing/Logs"
        configure = ["cmake", "--preset", preset]
        self.announce(f"{preset} build")
        # 재압축이 바깥 프로세스(백신 검사)에 막혀 지면 — 이 문구일 때만 한 번 더 구성한다. 구성 시작이 기록을 제자리에 돌린다(cmake/Modules/Build/NinjaBuildLogs.cmake).
        if self.run(configure, f"{logs}/verify.configure.log") != 0 and not (
                any(NINJA_DENIED_TEXT in line for line in read_lines(self.root / logs / "verify.configure.log")) and self.run(configure, f"{logs}/verify.configure.log") == 0):
            raise StageFailed(f"{preset} configure")
        if self.run(["cmake", "--build", "--preset", preset, "--", "-k", "0"], f"{logs}/verify.build.log") != 0:
            self.show_matches(f"{logs}/verify.build.log", "error|FAILED", MAX_SHOWN_LINES)
            raise StageFailed(f"{preset} build")
        if not self.options.dry_run:
            self.write_line(f"build warnings: {sum('warning:' in line for line in read_lines(self.root / logs / 'verify.build.log'))}")

    def run_tests(self, preset: str, name: str, *filters: str) -> bool:
        """ctest 를 돌리고 이력에 쌓는다(진 실행도 — 흔들림 · p95 추세는 여러 묶음을 모아야 보인다, test_history.py report). 갈래도 부르므로 여기서 끝내지 않는다."""
        log = f"Testing/Logs/verify.ctest.{name}.log"
        folder = f"build/{preset}"
        marker = f"{folder}/{log}.start"   # 이번 실행이 쓴 로그 · 보고를 가르는 시각 표지 — 앞에 시작한 보고는 손 실행이라 sha 없이 manual 로 들어간다(흔들림 판정은 verify 만 본다)
        self.announce(f"{preset} ctest {name}")
        if not self.options.dry_run:
            (self.root / marker).parent.mkdir(parents=True, exist_ok=True)
            (self.root / marker).touch()
        exclude = ["-E", self.quarantine_exclude] if self.quarantine_exclude else []
        code = self.run(["ctest", f"-j{self.options.ctest_jobs}", "--output-on-failure", *exclude, *filters], log, folder=folder)
        self.write_line(f"{preset} ctest {name} exit={code}")
        self.show_matches(f"{folder}/{log}", r"tests passed|tests failed|\*\*\*Failed|Timeout|Not Run", MAX_SHOWN_LINES)
        if code != 0:
            started = (self.root / marker).stat().st_mtime
            list_log = [path for path in (self.root / folder / "Testing/Logs").glob("Scenario.*.log") if path.stat().st_mtime > started]
            for line in collect_results([self.root / folder / log, *list_log]):
                self.write_line(line)
        if self.run([*SCRIPTS, "test-history", "ingest", f"{folder}/Bin/Saved/Automation", "--commit", self.sha, "--kind", "verify", "--since", marker]) != 0:
            self.write_line(f"{preset} test history ingest 실패")
            return False
        return code == 0


class BuildInput(abc.ABC):
    """단계가 도는 데 있어야 하는 것 — 이것을 만드는 단계를 건너뛰면 실행기가 뒤 단계에 필요한 것만 다시 마련한다."""

    @abc.abstractmethod
    def prepare(self, shell: PreflightShell) -> None:
        """마련한다 — 지면 `StageFailed`."""


class Job(abc.ABC):
    """한 단계 안에서 겹쳐 도는 갈래."""

    name: str

    @abc.abstractmethod
    def run(self, shell: PreflightShell) -> bool:
        """통과했나."""


class Stage(abc.ABC):
    """단계 하나. 앞 단계는 파일에서 위에 적은 클래스다."""

    name: str
    last_start: str                                 # `--from` 이 이 이름까지면 돈다(제 이름이면 `--from` 으로 고를 수 있는 단계)
    record: str | None = None                       # 통과 기록 · 단계 시간의 이름(없으면 적지 않는다)
    runs_with: str | None = None                    # 제 기록 없이 이 이름의 기록을 따라 돈다
    blocks_push: bool = True                        # 거짓이면 WINDOWS_OK 뒤에 돌고, 져도 다음 단계를 돈다(끝에서 진다)
    inputs: tuple[BuildInput, ...] = ()             # 있어야 하는 것
    prepares: tuple[BuildInput, ...] = ()           # 이 단계가 만드는 것
    steps: tuple[BuildInput | Job, ...] = ()        # 차례 — 입력은 앞에서 마련하고 갈래는 뒤에서 띄운다(단계 끝에서 모두 합류)
    standing_jobs: tuple[Job, ...] = ()             # 통과 기록으로 건너뛰는 단계여도 도는 갈래

    def run(self, shell: PreflightShell) -> None:
        """갈래를 띄운 뒤 앞에서 도는 일 — 지면 `StageFailed`."""

    def make_skip_note(self, options: PreflightOptions) -> str | None:
        """실행 인자 때문에 건너뛰면 그 까닭(그러면 통과 기록도 남기지 않는다)."""
        return None

    def make_failure(self, reason: str, sha: str, suffix: str) -> str:
        """졌을 때의 끝 줄."""
        return make_windows_failure(reason, sha, suffix)


class BackgroundStage(Stage):
    """뒤에서 여러 단계와 겹쳐 도는 단계 — 입력이 마련되면 뜨고, `joined_before` 앞에서 합류하고, 졌으면 `failed_before` 앞에서 진다."""

    title: str
    log: str
    joined_before: str
    failed_before: str


class PreflightRun:
    def __init__(self, shell: PreflightShell, stages: Sequence[Stage]) -> None:
        self.shell = shell
        self.stages = stages
        self._start_names = list_start_names(stages)
        self._selected: list[Stage] = []
        self._executor = ThreadPoolExecutor(max_workers=16)   # 갈래는 자식을 기다리기만 한다 — 한 번에 도는 갈래 수보다 많으면 된다
        self._jobs: list[tuple[Job, Future[bool]]] = []
        self._prepared: set[BuildInput] = set()
        self._background: dict[str, tuple[Future[str | None] | None, float]] = {}   # 뜬 것 — 이름 → (까닭을 줄 일, 시작 시각)
        self._deferred: dict[str, str] = {}   # 진 뒤쪽 단계 — 이름 → 까닭
        self._current: tuple[str, float] | None = None

    @property
    def _dry(self) -> bool:
        return self.shell.options.dry_run

    def run(self) -> int:
        try:
            self._select()
            for stage in self.stages:
                if stage.blocks_push:
                    self._join_background(stage.name)
                    self._run_stage(stage)
            self._write_ok()
            return self._run_after_push()
        except RunStopped as stopped:
            if self._current is not None:
                self._record_time(self._current[0], "failed", self._current[1])
            self.shell.write_line(str(stopped))
            return ExitCode.VIOLATION
        finally:
            # 겹쳐 돌던 갈래(배포 빌드 · 쿠킹 · clang-tidy 등)를 끝까지 기다린다 — 두고 나가면 다음 실행과 같은 빌드 폴더를 다퉈 구성이
            # "failed recompaction: Permission denied" 로 진다(2026-11-17). clang-tidy 는 끝까지 돌면 통과한 TU 가 캐시에 남아 다음 실행이 빠르다.
            self._executor.shutdown(wait=True)

    # --- 고르기

    def _is_in_range(self, stage: Stage) -> bool:
        return self._start_names.index(self.shell.options.start) <= self._start_names.index(stage.last_start)

    def _select(self) -> None:
        """돌 단계 — 통과 기록이 이 sha 를 덮으면 건너뛰고 기록을 이 sha 로 옮긴다(같은 기록을 따르는 단계는 한 번만 묻는다)."""
        map_record: dict[str, bool] = {}
        for stage in self.stages:
            if not self._is_in_range(stage) or (self.shell.options.no_linux and not stage.blocks_push):
                continue
            record = stage.record or stage.runs_with
            if record is not None and record not in map_record:
                map_record[record] = not self.shell.run_record_command("verify-record", "skip", record, self.shell.sha)
                if not map_record[record]:
                    self._pass(record, "skipped", time.monotonic())
            if record is None or map_record[record]:
                self._selected.append(stage)

    # --- 단계

    def _run_stage(self, stage: Stage) -> None:
        if isinstance(stage, BackgroundStage):
            return   # 입력이 마련될 때 뜬다(_prepare)
        try:
            if self._is_in_range(stage):
                for job in stage.standing_jobs:
                    self._start_job(job)
            if stage not in self._selected:
                for item in stage.prepares:
                    if any(item in other.inputs for other in self._selected):
                        self._prepare(item)
                self._join_jobs()
                return
            if stage.record is not None:
                self._current = (stage.record, time.monotonic())
            for step in stage.steps:
                if isinstance(step, Job):
                    self._start_job(step)
                else:
                    self._prepare(step)
            stage.run(self.shell)
            self._join_jobs()
        except StageFailed as failed:
            raise RunStopped(stage.make_failure(str(failed), self.shell.sha, self._make_suffix())) from failed
        if self._current is not None:
            self._pass(self._current[0], "passed", self._current[1])

    def _make_suffix(self) -> str:
        return "".join(f" + {stage.title}" for stage in self.stages if isinstance(stage, BackgroundStage) and stage.name in self._deferred)

    def _stop(self, reason: str) -> RunStopped:
        return RunStopped(make_windows_failure(reason, self.shell.sha, self._make_suffix()))

    def _write_ok(self) -> None:
        if self._dry:
            self.shell.write_line("[dry] 통과 기록(build/verify.ok)과 WINDOWS_OK 줄은 실제 실행만 남긴다")
            return
        if not self.shell.run_record_command("verify-record", "ok", self.shell.sha):
            raise self._stop("build/verify.ok 쓰기")
        self.shell.write_line(f"WINDOWS_OK={self.shell.sha} (시작 단계 {self.shell.options.start})")

    def _run_after_push(self) -> int:
        """WINDOWS_OK 뒤 단계 — 져도 다음을 돌고, 끝 줄은 모아 마지막에 찍는다."""
        options, sha = self.shell.options, self.shell.sha
        if options.no_linux:
            self.shell.write_line("Linux 를 돌리지 않았다(--no-linux)")
            return ExitCode.PASSED
        list_stage = [stage for stage in self._selected if not stage.blocks_push]
        if not list_stage:
            if not self._dry:
                self.shell.write_line(f"VERIFIED={sha} (시작 단계 {options.start}, linux 는 단계 기록으로 건너뜀)")
            return ExitCode.PASSED
        list_failure: list[str] = []
        list_skipped: list[str] = []
        map_started: dict[str, float] = {}
        for stage in list_stage:
            note = stage.make_skip_note(options)
            if note is not None:
                self.shell.write_line(f"== {stage.name} 건너뜀({note})")
                list_skipped.append(stage.name)
                continue
            if stage.record is not None:
                map_started.setdefault(stage.record, time.monotonic())
            try:
                stage.run(self.shell)
            except StageFailed as failed:
                list_failure.append(stage.make_failure(str(failed), sha, ""))
        self.shell.announce("done")
        if list_failure:
            self.shell.write_line("\n".join(list_failure))
            return ExitCode.VIOLATION
        if not list_skipped:
            for record, started in map_started.items():
                self._pass(record, "passed", started)
        if not self._dry:
            self.shell.write_line(f"VERIFIED={sha} (시작 단계 {options.start}{''.join(f', {name} 건너뜀' for name in list_skipped)})")
        return ExitCode.PASSED

    # --- 겹침

    def _prepare(self, item: BuildInput) -> None:
        item.prepare(self.shell)
        self._prepared.add(item)
        for stage in self._selected:
            if isinstance(stage, BackgroundStage) and stage.name not in self._background and self._prepared.issuperset(stage.inputs):
                self._start_background(stage)

    def _start_job(self, job: Job) -> None:
        if self._dry:   # 차례로 찍는다(출력이 섞이지 않게)
            self.shell.write_line(f"[dry] -- 갈래 {job.name} (겹쳐 돈다)")
            job.run(self.shell)
            return
        self._jobs.append((job, self._executor.submit(self._run_job, job)))

    def _run_job(self, job: Job) -> bool:
        try:
            return job.run(self.shell)
        except StageFailed as failed:
            self.shell.write_line(make_windows_failure(str(failed), self.shell.sha))   # 까닭을 그 자리에서 — 갈래 이름은 합류가 찍는다
        except Exception:   # 갈래의 예외가 합류 전까지 묻히지 않게
            self.shell.write_line(traceback.format_exc())
        return False

    def _join_jobs(self) -> None:
        list_failed = [job.name for job, future in self._jobs if not future.result()]
        self._jobs = []
        if list_failed:
            raise StageFailed(" ".join(list_failed))

    def _start_background(self, stage: BackgroundStage) -> None:
        future: Future[str | None] | None = None
        if self._dry:
            names = [other.name for other in self.stages]
            overlapped = names[names.index(stage.name) + 1:names.index(stage.joined_before)]
            self.shell.write_line(f"[dry] -- 갈래 {stage.title} (뒤에서 — {' · '.join(overlapped)} 와 겹치고 {stage.joined_before} 앞에서 합류)")
            self._run_background(stage)
        else:
            future = self._executor.submit(self._run_background, stage)
        self._background[stage.name] = (future, time.monotonic())

    def _run_background(self, stage: BackgroundStage) -> str | None:
        """진 까닭(통과면 None)."""
        try:
            stage.run(self.shell)
        except StageFailed as failed:
            return str(failed)
        except Exception:
            self.shell.write_line(traceback.format_exc())
            return f"{stage.title} 예외"
        return None

    def _join_background(self, before: str) -> None:
        for stage in self._selected:
            if not isinstance(stage, BackgroundStage):
                continue
            if stage.joined_before == before:
                self.shell.announce(f"{stage.title} 합류")
                future, started = self._background[stage.name]
                reason = future.result() if future is not None else None
                if reason is None:
                    self._pass(stage.record, "passed", started)
                else:
                    self._record_time(stage.record, "failed", started)
                    self._deferred[stage.name] = reason
                    self.shell.write_line(f"{stage.title} 가 졌다({stage.log}) - {stage.joined_before} 는 돌고 끝에서 진다")
            if stage.failed_before == before and stage.name in self._deferred:
                raise self._stop(self._deferred[stage.name])

    # --- 기록

    def _pass(self, record: str, result: str, started: float) -> None:
        self._record_time(record, result, started)
        self._current = None
        if not self._dry and not self.shell.run_record_command("verify-record", "pass", record, self.shell.sha):
            raise self._stop(f"{record} 통과 기록")

    def _record_time(self, record: str, result: str, started: float) -> None:
        if not self._dry and not self.shell.run_record_command("test-history", "stage", record, "--commit", self.shell.sha, "--result", result,
                                                            "--seconds", str(int(time.monotonic() - started))):
            self.shell.write_line(f"단계 시간을 쌓지 못했다({record})")


def list_start_names(stages: Sequence[Stage]) -> list[str]:
    """`--from` 으로 고를 수 있는 이름 — 차례대로."""
    return [stage.name for stage in stages if stage.last_start == stage.name]
