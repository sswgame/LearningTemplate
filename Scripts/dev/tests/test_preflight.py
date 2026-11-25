"""
묶음 검증(`preflight.py`) — 단계 클래스마다 명령 조립 · 건너뛰기 판정 · 실패 때 끝 줄, 실행기의 차례 · 겹침 · 기록.

실행: python -X utf8 Scripts/dev/tests/test_preflight.py (CTest `Scripts.Preflight`). 빌드 · 테스트를 띄우지 않는다(자식 자리를 가짜로 바꾼다).
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from typing import Callable, Sequence
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from dev import preflight_stages  # noqa: E402
from dev.preflight_run import NINJA_DENIED_TEXT, BackgroundStage, PreflightOptions, PreflightRun, PreflightShell, Stage, StageFailed, collect_results, list_start_names  # noqa: E402
from dev.preflight_stages import (  # noqa: E402
    ChangedStage, CpuStage, FlakyStage, GpuReleaseStage, ImportedAssets, LintJob, LinuxStage, LoadStage, SanitizeStage, SanitizerCpuJob, ServerToolsJob, ShaderWarmJob,
    ShippingBuildJob, StdContainerJob, TidyStage, TreeStage, list_stages,
)

SHA = "a" * 40
Fails = Callable[[str], bool]


class FakeShell(PreflightShell):
    """자식을 띄우지 않고 부른 줄을 모은다 — `fails` 가 참인 줄은 진다. 기록 판정(`verify-record skip`)은 `skipped` 의 이름만 건너뛴다."""

    def __init__(self, root: Path, fails: Fails = lambda line: False, skipped: Sequence[str] = (), **options: object) -> None:
        super().__init__(root, SHA, PreflightOptions(**{"start": "build", "ctest_jobs": 6, **options}), "")
        self.fails = fails
        self.skipped = skipped
        self.list_call: list[str] = []
        self.list_line: list[str] = []

    def spawn(self, command: Sequence[str], log: Path | None, cwd: Path, environment: dict[str, str] | None) -> int:
        line = " ".join(command)
        self.list_call.append(line)
        if "verify-record skip" in line:
            return 0 if command[-2] in self.skipped else 1
        if log is not None:
            log.parent.mkdir(parents=True, exist_ok=True)
            log.write_text("", encoding="utf-8")
        return 1 if self.fails(line) else 0

    def write_line(self, text: str) -> None:
        self.list_line.append(text)


class PreflightTest(unittest.TestCase):
    def setUp(self) -> None:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def shell(self, fails: Fails | str = lambda line: False, skipped: Sequence[str] = (), **options: object) -> FakeShell:
        return FakeShell(self.root, (lambda line: fails in line) if isinstance(fails, str) else fails, skipped, **options)

    def run_all(self, shell: FakeShell) -> int:
        with mock.patch.object(preflight_stages, "run_git_or_raise", return_value=SHA), mock.patch.object(preflight_stages, "list_tree_changes", return_value=[]):
            return PreflightRun(shell, list_stages()).run()

    def commands(self, shell: FakeShell) -> list[str]:
        """단계가 부른 명령 — 실행기의 기록 · 이력 줄을 뺀다."""
        return [line for line in shell.list_call if not any(word in line for word in ("verify-record", "test-history stage", "test-history ingest"))]


class StageTest(PreflightTest):
    """단계 · 갈래 클래스 하나씩."""

    def check_failure(self, stage: Stage, failing: str, want: str) -> None:
        with self.assertRaises(StageFailed) as raised:
            stage.run(self.shell(failing))
        self.assertEqual(stage.make_failure(str(raised.exception), SHA, ""), want)

    def test_every_class_is_a_stage_in_file_order(self) -> None:
        list_name = [stage.name for stage in list_stages()]
        self.assertEqual(list_name, ["build", "tidy", "changed", "cpu", "gpu-release", "flaky", "tree", "load", "linux", "sanitize"])
        self.assertEqual(list_start_names(list_stages()), ["build", "changed", "cpu", "gpu-release", "linux"])
        for stage in list_stages():
            with self.subTest(stage=stage.name):
                self.assertIn(stage.last_start, list_start_names(list_stages()))
                if isinstance(stage, BackgroundStage):
                    self.assertLess(list_name.index(stage.name), list_name.index(stage.joined_before))
                    self.assertLess(list_name.index(stage.joined_before), list_name.index(stage.failed_before))

    def test_build(self) -> None:
        shell = self.shell()
        shell.build("P")
        self.assertEqual(shell.list_call, ["cmake --preset P", "cmake --build --preset P -- -k 0"])
        for failing, reason in (("cmake --preset P", "P configure"), ("cmake --build", "P build")):
            with self.assertRaisesRegex(StageFailed, f"^{reason}$"):
                self.shell(failing).build("P")

    def test_configure_is_retried_only_for_denied_log_rewrite(self) -> None:
        shell = self.shell()
        with mock.patch("dev.preflight_run.read_lines", return_value=[NINJA_DENIED_TEXT]):
            shell.fails = lambda line: line == "cmake --preset P" and shell.list_call.count(line) == 1
            shell.build("P")
        self.assertEqual(shell.list_call.count("cmake --preset P"), 2)

    def test_run_tests_records_failed_runs_and_excludes_quarantine(self) -> None:
        shell = self.shell("ctest -j")
        shell.quarantine_exclude = "^(A)$"
        self.assertFalse(shell.run_tests("P", "all", "-LE", "^load$"))
        self.assertEqual(shell.list_call, ["ctest -j6 --output-on-failure -E ^(A)$ -LE ^load$",
                                           f"py -3 -m Scripts test-history ingest build/P/Bin/Saved/Automation --commit {SHA} --kind verify --since build/P/Testing/Logs/verify.ctest.all.log.start"])
        self.assertFalse(self.shell("ingest").run_tests("P", "all"))
        self.assertTrue(self.shell().run_tests("P", "all"))

    def test_imported_assets(self) -> None:
        shell = self.shell()
        ImportedAssets().prepare(shell)
        self.assertIn("ctest -j6 --output-on-failure -L import", shell.list_call)
        with self.assertRaisesRegex(StageFailed, "^Ninja-Release import$"):
            ImportedAssets().prepare(self.shell("-L import"))

    def test_jobs(self) -> None:
        (self.root / "Source/Core/String/Tests").mkdir(parents=True)
        for name in ("B.cpp", "A.cpp"):
            (self.root / "Source/Core/String/Tests" / name).touch()
        (self.root / ShaderWarmJob.folder).mkdir(parents=True)
        (self.root / ShaderWarmJob.folder / "a.shaderrequests").touch()
        for job, want in (
            (LintJob(), ["py -3 -m Scripts lint --all"]),
            (StdContainerJob(), ["py -3 -m Scripts syntax --changed origin/main --direct --define SW_ENABLE_STL_CONTAINER --files Source/Core/String/Tests/A.cpp Source/Core/String/Tests/B.cpp"]),
            (ServerToolsJob(), ["build/Ninja-Release-Server/Bin/Server.exe --test -unattended", "py -3 Tools/Scripts/ExtensionTool.py test Tools/launch-args Tools/comment-fold"]),
            (SanitizerCpuJob(), ["ctest -j6 --output-on-failure -L nogpu"]),
            (ShaderWarmJob(), ["build/Ninja-Release/Bin/App.exe --cook-shaders -shader-request-dir=build/Ninja-Release/Testing/ShaderRequests -unattended "
                                "-log-file=build/Ninja-Release/Testing/Logs/ShaderWarm.log"]),
            (ShippingBuildJob(), [line for preset in ("Ninja-Shipping", "Ninja-Release-Server", "Ninja-Shipping-Server") for line in (f"cmake --preset {preset}", f"cmake --build --preset {preset} -- -k 0")]
             + ["py -3 -m Scripts shipping-check --skip-build"]),
        ):
            with self.subTest(job=job.name):
                shell = self.shell()
                self.assertTrue(job.run(shell))
                self.assertEqual(self.commands(shell), want)
                self.assertFalse(job.run(self.shell(want[-1])), "마지막 명령이 지면 갈래가 진다")

    def test_shader_warm_without_requests_has_nothing_to_do(self) -> None:
        shell = self.shell()
        self.assertTrue(ShaderWarmJob().run(shell))
        self.assertEqual(shell.list_call, [])

    def test_server_job_checks_shipping_only_when_build_is_skipped_by_from(self) -> None:
        shell = self.shell(start="cpu")
        self.assertTrue(ServerToolsJob().run(shell))
        self.assertEqual(shell.list_call[-1], "py -3 -m Scripts shipping-check")
        self.assertFalse(ServerToolsJob().run(self.shell("shipping-check", start="cpu")))

    def test_sanitizer_job_counts_unit_shard_reports_only(self) -> None:
        folder = self.root / "build/Ninja-Release-ASan/Testing/Reports"
        folder.mkdir(parents=True)
        for name, count in (("UnitTests.0", 3), ("UnitTests.1", 4), ("UnitTests.Playground", 100)):
            (folder / f"{name}.json").write_text(json.dumps({"runCount": count, "failedCount": 0}), encoding="utf-8")
        shell = self.shell()
        SanitizerCpuJob().run(shell)
        self.assertEqual(shell.list_line[-1], "App --test: 7 case run(s), 0 failed, 0 skipped (2 report(s))")

    def test_tidy(self) -> None:
        shell = self.shell()
        TidyStage().run(shell)
        self.assertEqual(shell.list_call, ["py -3 -m Scripts tidy --changed origin/main"])
        self.check_failure(TidyStage(), "tidy", f"WINDOWS_FAILED (clang-tidy - build/Ninja-Release-ASan/Testing/Logs/verify.tidy.log) - Linux 를 돌리지 않는다 sha={SHA}")

    def test_changed(self) -> None:
        shell = self.shell()
        ChangedStage().run(shell)
        self.assertEqual(shell.list_call, ["py -3 -m Scripts test-history changed --preset Ninja-Release"])
        self.check_failure(ChangedStage(), "changed", f"CHANGED_FAILED sha={SHA} - build/verify.changed.log (cpu · gpu-release 를 시작하지 않는다)")

    def test_gpu_release(self) -> None:
        shell = self.shell()
        GpuReleaseStage().run(shell)
        self.assertEqual(self.commands(shell), ["ctest -j6 --output-on-failure -LE ^load$"])
        self.check_failure(GpuReleaseStage(), "-LE", f"WINDOWS_FAILED (Ninja-Release ctest) - Linux 를 돌리지 않는다 sha={SHA}")

    def test_flaky(self) -> None:
        shell = self.shell()
        FlakyStage().run(shell)
        self.assertEqual(shell.list_call, ["py -3 -m Scripts test-history flaky"])
        self.check_failure(FlakyStage(), "flaky", f"WINDOWS_FAILED (흔들림이 문턱을 넘었다 - build/verify.flaky.log) - Linux 를 돌리지 않는다 sha={SHA}")

    def test_tree(self) -> None:
        for head, changes, reason in ((SHA, [], None), ("b" * 40, [], "HEAD 가 검증 중에 바뀌었다"), (SHA, [" M a"], "추적 파일이 바뀌었다")):
            with self.subTest(reason=reason), mock.patch.object(preflight_stages, "run_git_or_raise", return_value=head + "\n"), \
                    mock.patch.object(preflight_stages, "list_tree_changes", return_value=changes):
                if reason is None:
                    TreeStage().run(self.shell())
                else:
                    with self.assertRaisesRegex(StageFailed, reason):
                        TreeStage().run(self.shell())
                TreeStage().run(self.shell(dry_run=True))

    def test_load(self) -> None:
        shell = self.shell()
        LoadStage().run(shell)
        self.assertEqual(self.commands(shell), ["ctest -j6 --output-on-failure -L ^load$"])
        self.check_failure(LoadStage(), "-L ^load$", f"LOAD_FAILED sha={SHA} - build/Ninja-Release/Testing/Logs/verify.ctest.load.log")

    def test_linux(self) -> None:
        for record, want in ((False, "py -3 -m Scripts linux-scenarios all"), (True, "py -3 -m Scripts linux-scenarios all --record")):
            shell = self.shell(record=record)
            LinuxStage().run(shell)
            self.assertEqual(shell.list_call, [want])
        self.check_failure(LinuxStage(), "all", f"LINUX_FAILED sha={SHA}")

    def test_sanitize(self) -> None:
        shell = self.shell()
        SanitizeStage().run(shell)
        self.assertEqual(shell.list_call, ["py -3 -m Scripts linux-scenarios sanitize address"])
        self.check_failure(SanitizeStage(), "sanitize", f"SANITIZE_FAILED (address) sha={SHA} - build/verify.sanitize.log")
        self.assertIsNone(SanitizeStage().make_skip_note(shell.options))
        self.assertIsNotNone(SanitizeStage().make_skip_note(self.shell(record=True).options))

    def test_results_keep_failed_lines_only(self) -> None:
        (self.root / "a.log").write_text("[2026-11-18 10:00:00] [Info] [Scenario] per-backend failure: game.vfxsort: GoldenMismatch dx11,dx12,vk,gl - differs\n"
                                         "[2026-11-18 10:00:01] [Info] [Scenario] SCENARIO_RESULT=FAILED stages=8 failed=4 notRun=0\n", encoding="utf-8")
        (self.root / "b.log").write_bytes(b"TEST_RESULT=PASSED run=10 failed=0 skipped=1\r\nSCENARIO_RESULT=PASSED stages=2 failed=0 notRun=0\r\n")
        self.assertEqual(collect_results([self.root / "a.log", self.root / "b.log", self.root / "missing.log"]),
                         ["SCENARIO_RESULT=FAILED stages=8 failed=4 notRun=0", "per-backend failure: game.vfxsort: GoldenMismatch dx11,dx12,vk,gl - differs"])


class RunTest(PreflightTest):
    """실행기 — 차례 · 겹침 · 기록 · 멈춤."""

    BUILD = ["lint --all", "--preset Ninja-Release", "-L import", "--preset Ninja-Shipping", "--preset Ninja-Release-Server", "--preset Ninja-Shipping-Server", "shipping-check --skip-build",
             "syntax", "--preset Ninja-Release-ASan", "tidy"]
    REST = ["test-history changed", "-L nogpu", "Server.exe", "ExtensionTool", "--cook-shaders", "-LE ^load$", "flaky"]
    AFTER = ["-L ^load$", "linux-scenarios all", "sanitize address"]

    def order(self, **options: object) -> list[str]:
        """--dry-run 이 찍는 명령 차례 — 알아볼 낱말로 줄인다."""
        shell = self.shell(skipped=options.pop("skipped", ()), dry_run=True, **options)
        self.assertEqual(self.run_all(shell), 0)
        list_key: list[str] = []
        for line in shell.list_line:
            if line.startswith("[dry] ") and not any(word in line for word in ("-- 갈래", "ingest", "cmake --build", "통과 기록")):
                command = f"{line.removeprefix('[dry] ').split(' > ')[0]} "
                list_key.append(next(key for key in [self.REST[0], *self.BUILD, "shipping-check", *self.REST, *self.AFTER] if f"{key} " in command or (key in command and not key.startswith("--preset"))))
        return list_key

    def test_order_per_arguments(self) -> None:
        self.assertEqual(self.order(), [*self.BUILD, *self.REST, *self.AFTER])
        self.assertEqual(self.order(no_linux=True), [*self.BUILD, *self.REST])
        inputs = ["--preset Ninja-Release", "--preset Ninja-Release-ASan", "tidy", "--preset Ninja-Release-Server", "-L import"]
        shipping = [*self.REST[:4], "shipping-check", *self.REST[4:]]
        self.assertEqual(self.order(start="changed", no_linux=True), [*inputs, *shipping])
        self.assertEqual(self.order(start="cpu", no_linux=True), [*inputs, *shipping[1:]])
        self.assertEqual(self.order(start="gpu-release", no_linux=True), ["--preset Ninja-Release", "--preset Ninja-Release-ASan", "tidy", "-LE ^load$", "flaky"])
        self.assertEqual(self.order(start="linux"), ["flaky", *self.AFTER])
        self.assertEqual(self.order(start="linux", record=True), ["flaky", *self.AFTER[:2]])
        self.assertEqual(self.order(skipped=["build", "tidy", "cpu"], no_linux=True), ["lint --all", "--preset Ninja-Release", "-L import", self.REST[0], "-LE ^load$", "flaky"])
        self.assertEqual(self.order(skipped=["linux"]), [*self.BUILD, *self.REST])

    def test_passed_run_records_every_stage(self) -> None:
        shell = self.shell()
        self.assertEqual(self.run_all(shell), 0)
        self.assertEqual([line.split()[-2] for line in shell.list_call if "verify-record pass" in line], ["build", "changed", "cpu", "tidy", "gpu-release", "linux"])
        self.assertIn(f"py -3 -m Scripts verify-record ok {SHA}", shell.list_call)
        self.assertEqual([line for line in shell.list_line if "=" + SHA in line], [f"WINDOWS_OK={SHA} (시작 단계 build)", f"VERIFIED={SHA} (시작 단계 build)"])

    def test_skipped_stage_moves_its_record(self) -> None:
        shell = self.shell(skipped=["cpu"], no_linux=True)
        self.assertEqual(self.run_all(shell), 0)
        self.assertIn(f"py -3 -m Scripts verify-record pass cpu {SHA}", shell.list_call)
        self.assertIn(f"py -3 -m Scripts test-history stage cpu --commit {SHA} --result skipped --seconds 0", shell.list_call)
        self.assertFalse(any("nogpu" in line for line in shell.list_call))

    def test_dry_run_writes_no_record(self) -> None:
        shell = self.shell(dry_run=True)
        self.run_all(shell)
        self.assertTrue(all("verify-record skip" in line for line in shell.list_call), shell.list_call)

    def test_failed_jobs_are_joined_and_named(self) -> None:
        shell = self.shell(lambda line: "lint --all" in line or "--build --preset Ninja-Shipping " in line)
        self.assertEqual(self.run_all(shell), 1)
        self.assertIn(f"WINDOWS_FAILED (Ninja-Shipping build) - Linux 를 돌리지 않는다 sha={SHA}", shell.list_line, "갈래 안의 까닭")
        self.assertIn(f"WINDOWS_FAILED (lint --all shipping build) - Linux 를 돌리지 않는다 sha={SHA}", shell.list_line, "뒤에서 돌던 tidy 는 끝 줄 뒤에 끝날 수 있다")
        self.assertFalse(any("verify-record pass build" in line or "test-history changed" in line for line in shell.list_call))
        self.assertTrue(any("stage build" in line and "--result failed" in line for line in shell.list_call))

    def test_changed_failure_stops_before_cpu(self) -> None:
        shell = self.shell("test-history changed")
        self.assertEqual(self.run_all(shell), 1)
        self.assertTrue(any(line.startswith(f"CHANGED_FAILED sha={SHA}") for line in shell.list_line))
        self.assertFalse(any("nogpu" in line for line in shell.list_call))

    def test_gpu_runs_after_failed_tidy_and_stops_before_windows_ok(self) -> None:
        shell = self.shell("Scripts tidy")
        self.assertEqual(self.run_all(shell), 1)
        self.assertTrue(any("-LE" in line for line in shell.list_call))
        self.assertFalse(any("flaky" in line or "verify-record ok" in line or "verify-record pass tidy" in line for line in shell.list_call))
        self.assertEqual(shell.list_line[-1], f"WINDOWS_FAILED (clang-tidy - {TidyStage.log}) + clang-tidy - Linux 를 돌리지 않는다 sha={SHA}")

    def test_tidy_overlaps_changed_and_cpu_and_ends_before_gpu(self) -> None:
        released = threading.Event()
        shell = self.shell()

        def fails(line: str) -> bool:
            if "Scripts tidy" in line:
                return not released.wait(60)   # cpu 갈래가 떠야 끝난다 — 겹치지 않으면 여기서 진다
            if "-L nogpu" in line:
                released.set()
            return False

        shell.fails = fails
        self.assertEqual(self.run_all(shell), 0)
        position = {key: next(i for i, line in enumerate(shell.list_call) if key in line) for key in ("verify-record pass cpu", "verify-record pass tidy", "-LE ^load$")}
        self.assertEqual(sorted(position, key=position.get), list(position))

    def test_after_push_failures_run_the_rest_and_keep_windows_ok(self) -> None:
        for failing, want in (("-L ^load$", "LOAD_FAILED"), ("linux-scenarios all", "LINUX_FAILED"), ("sanitize address", "SANITIZE_FAILED (address)")):
            with self.subTest(failing=failing):
                shell = self.shell(failing)
                self.assertEqual(self.run_all(shell), 1)
                self.assertTrue(shell.list_line[-1].startswith(f"{want} sha={SHA}"), shell.list_line[-1])
                self.assertIn(f"WINDOWS_OK={SHA} (시작 단계 build)", shell.list_line)
                self.assertTrue(any("sanitize address" in line for line in shell.list_call))
                self.assertFalse(any("verify-record pass linux" in line for line in shell.list_call))

    def test_golden_record_run_leaves_no_linux_record(self) -> None:
        shell = self.shell(record=True)
        self.assertEqual(self.run_all(shell), 0)
        self.assertFalse(any("verify-record pass linux" in line or "sanitize" in line for line in shell.list_call))
        self.assertEqual(shell.list_line[-1], f"VERIFIED={SHA} (시작 단계 build, sanitize 건너뜀)")

    def test_record_write_failure_stops(self) -> None:
        for failing, reason in (("verify-record pass build", "build 통과 기록"), ("verify-record pass changed", "changed 통과 기록"), ("verify-record ok", "build/verify.ok 쓰기")):
            shell = self.shell(failing)
            self.assertEqual(self.run_all(shell), 1)
            self.assertIn(f"WINDOWS_FAILED ({reason}) - Linux 를 돌리지 않는다 sha={SHA}", shell.list_line)


class CommandTest(unittest.TestCase):
    def test_arguments(self) -> None:
        from dev.preflight import PreflightCommand
        for argv in (["--from", "nowhere"], ["--bogus"], ["--no-lin"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                PreflightCommand.main(argv)
            self.assertEqual(raised.exception.code, 2)

    def test_shell_entry_passes_arguments_through(self) -> None:
        text = (Path(__file__).resolve().parents[1] / "preflight.sh").read_text(encoding="utf-8")
        self.assertIn('exec py -3 -m Scripts preflight "$@"', text)


if __name__ == "__main__":
    unittest.main()
