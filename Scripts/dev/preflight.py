"""
묶음 끝 전체 검증: Windows(린트, Release · ASan · 서버 · 배포 빌드, 단위 · ctest)가 모두 통과한 뒤에만 Linux 를 돌린다. 입구는 `bash Scripts/dev/preflight.sh` 하나다.

단계는 `preflight_stages.py` 의 클래스 하나씩이고(`--help` 끝에 차례대로 찍힌다), 돌릴 명령의 차례는 `--dry-run` 이 찍는다.

- 로그는 build/<프리셋>/Testing/Logs/verify.* · build/verify.*.log.
- 시작에 작업 트리가 깨끗해야 하고(검증한 것이 커밋 그 자체), 끝에 HEAD 가 그대로이고 추적 파일이 바뀌지 않았어야 한다. Windows 가 모두 통과하면 build/verify.ok 에 sha 를 적고
  끝 줄 WINDOWS_OK=<sha> 를 찍는다 — git pre-push 훅(Scripts/dev/verify_record.py)이 main 으로 가는 sha 가 그것인지 본다. Linux 까지 돌면 VERIFIED=<sha> 가 마지막 줄이다.
- 지면 끝 줄은 WINDOWS_FAILED · CHANGED_FAILED(뒤 단계를 시작하지 않는다), WINDOWS_OK 뒤에는 LOAD_FAILED · LINUX_FAILED · SANITIZE_FAILED(나머지를 돌고 끝에서 진다)다.
- --from 은 앞 단계를 건너뛴다. 앞 단계가 통과한 뒤 바뀐 것이 그 단계에 영향을 주지 않을 때만 쓴다(실패한 단계부터 다시 돌린다 — linux 는 Windows 가 이미 통과했고 그 뒤 고친 것이
  Linux 쪽 스크립트 · 환경뿐일 때). 끝 줄에 어디서 시작했는지 적는다. build 를 건너뛰어도 뒤 단계가 쓸 프리셋은 증분 빌드한다.
- 단계 기록: 단계가 통과하면 그 sha 를 build/verify.stages.json 에 적고, 다음 실행에서 그 sha 뒤 바뀐 파일이 모두 입력이 아닌 경로(Config/Dev/Verify.json 의 _nonInputPaths — 문서 ·
  스킬)면 그 단계를 건너뛴다. 거꾸로 적는 목록이라 빠뜨리면 더 돌 뿐이다. lint --all 은 문서 · 스킬도 읽어 build 범위면 늘 돈다.
- 이력(Saved/TestHistory/history.sqlite): ctest 보고는 단계 시작 표지 뒤의 것만 이 sha · 검증 실행(verify)으로 쌓고, 단계마다 걸린 시간을 쌓아 지난 p95 보다 느리면 경고한다.
- ctest 는 격리 표(Config/Rules/TestSelection/Quarantine.json)를 -E 로 빼고 돈다(만료가 지났으면 처음에 진다). 슬롯 수는 Config/Dev/Verify.json 의 _ctestJobs.
- Debug 는 짓지 않는다 — 단언 · 검사는 SW_DO_CHECKS 로 Release 에 있고, CPU 단위는 ASan(Release + clang-cl ASan)이 돈다(사용자 결정 2026-11-18).
- 체인(부하 측정 제외)은 그래픽 API 검증 층(-rhi-validation) · 교착 탐지(-deadlock-detection) · bindless 죽은 번호 검사를 켠다 — 검증 층 오류 · 경고는 로그 게이트에서 진다.
"""

from __future__ import annotations

import argparse
import inspect
import os
import sys

from common.command import Command, ScriptContext
from common.exit_code import ExitCode
from common.git import run_git_or_raise
from common.run_process import run_process
from dev.preflight_run import SCRIPTS, PreflightOptions, PreflightRun, PreflightShell, list_start_names, list_tree_changes, make_windows_failure
from dev.preflight_stages import MAX_TREE_LINES, list_stages
from dev.run_history import read_verify_config


class PreflightCommand(Command):
    name = "preflight"
    help = "묶음 끝 전체 검증(입구는 bash Scripts/dev/preflight.sh 하나) — 린트 · 빌드 · 단위 · 체인 · Linux, 끝 줄 WINDOWS_OK=<sha> · VERIFIED=<sha>(--from · --no-linux · --record · --show · --dry-run)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.prog = "bash Scripts/dev/preflight.sh"
        parser.allow_abbrev = False
        list_stage = list_stages()
        parser.epilog = f"{parser.epilog}\n단계(도는 차례):\n" + "\n".join(f"  {stage.name:<12} {inspect.cleandoc(type(stage).__doc__)}" for stage in list_stage)
        list_name = list_start_names(list_stage)
        parser.add_argument("--from", dest="start", choices=list_name, default=list_name[0], help="앞 단계를 건너뛰고 이 단계부터")
        parser.add_argument("--no-linux", action="store_true", help="Windows 까지만(Linux 골든이 곧 바뀌는 묶음 사이)")
        parser.add_argument("--record", action="store_true", help="Linux 골든을 기록한다(linux-scenarios all --record — 통과 기록을 남기지 않는다)")
        parser.add_argument("--show", action="store_true", help="게임 · 에디터 테스트 창을 화면에 띄우고 테스트를 하나씩 돌린다(사람이 지켜볼 때 — 판정 · 골든은 같다)")
        parser.add_argument("--dry-run", action="store_true", help="돌릴 명령만 차례대로 찍는다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        sys.stdout.reconfigure(newline="\n")   # 끝 줄을 읽는 쪽(셸 · 감시)이 CR 을 떼지 않아도 되게
        root = context.root
        try:
            ctest_jobs = int(read_verify_config(root, "_ctestJobs"))
        except SystemExit as error:
            print(error)
            return ExitCode.CHECK_INVALID
        # 빌드 · 구성 · ctest 는 메인 작업 폴더 하나에서만(공용 vcpkg · 디스크, 사용자 지시 2026-11-12) — 연결된 워크트리는 .git 이 파일이다.
        if (root / ".git").is_file() and not args.dry_run:
            print(f"워크트리({root})에서 묶음 검증을 시작하지 않는다 — 메인 작업 폴더에서 돌린다(--dry-run 은 된다)")
            return ExitCode.CHECK_INVALID
        if args.show:
            os.environ["SW_SHOW_WINDOWS"] = "1"   # 무인 실행의 창 숨김만 끈다(App 이 본다)
            ctest_jobs = 1
        sha = run_git_or_raise(["rev-parse", "HEAD"], cwd=root).strip()
        list_change = list_tree_changes(root)
        if list_change and not args.dry_run:
            print("작업 트리가 깨끗하지 않다 — 커밋한 뒤 돌린다(검증한 SHA 와 트리가 달라진다):", *list_change[:MAX_TREE_LINES], sep="\n")
            return ExitCode.CHECK_INVALID
        if list_change:
            print("[dry] 작업 트리가 깨끗하지 않다 — 실제 실행은 여기서 멈춘다", flush=True)

        shell = PreflightShell(root, sha, PreflightOptions(args.start, ctest_jobs, args.dry_run, args.no_linux, args.record), "")
        # 끝났다고 적힌 할 일 줄(커밋 꼬리말 `NEXT: <줄 앞말> 끝`)이 docs/NEXT.md 에 남아 있으면 여기서 멈춘다 — 줄을 지운 커밋이 검증할 sha 여야 한다.
        if not shell.run_record_command("next-done") and not args.dry_run:
            return ExitCode.CHECK_INVALID
        # 강제로 끝난 git 의 찌꺼기 잠금을 먼저 알린다 — 검증이 끝난 뒤의 커밋 · 병합이 거기서 멈춘다. 검증 자체는 막지 않는다.
        if not shell.run_record_command("git-locks"):
            shell.write_line("git 잠금 파일이 남아 있다(위 줄) - 도는 git 이 없으면 py -3 -m Scripts git-locks --remove")
        # 격리 표의 테스트를 ctest -E 로 뺀다. 만료가 지났거나 칸이 빠진 표는 검증 실패다(격리가 말없이 늘어지지 않게).
        quarantine = run_process([*SCRIPTS, "test-history", "quarantine", "--ctest-exclude"], cwd=root)
        if not quarantine.succeeded:
            shell.write_line(quarantine.stdout.strip())
            shell.write_line(make_windows_failure("quarantine table", sha))
            return ExitCode.VIOLATION
        shell.quarantine_exclude = quarantine.stdout.strip()
        if shell.quarantine_exclude:
            shell.write_line(f"격리로 뺀다: {shell.quarantine_exclude}")
        return PreflightRun(shell, list_stages()).run()
